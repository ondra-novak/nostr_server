#include <coroserver/http_ws_server.h>
#include <coroserver/http_stringtables.h>
#include <docdb/json.h>

#include "peer.h"

#include <sstream>
namespace nostr_server {

Peer::Peer(coroserver::http::ServerRequest &req, PApp app, const ServerOptions & options,
        std::string user_agent, std::string ident
)
:_req(req)
,_app(std::move(app))
,_options(std::move(options))
,_subscriber(_app->get_publisher())
,_rate_limiter(options.event_rate_window, options.event_rate_limit)
{
    _sensor.enable(std::move(ident), std::move(user_agent));
    _shared_sensor.enable();
    _app->client_counter(1);
}
Peer::~Peer() {
    _app->client_counter(-1);
}


using namespace coroserver::ws;

cocls::future<bool> Peer::client_main(coroserver::http::ServerRequest &req, PApp app, const ServerOptions & options) {
    std::exception_ptr e;
    auto ua = req[coroserver::http::strtable::hdr_user_agent];
    std::string ident;
    if (!options.http_header_ident.empty())  ident = req[options.http_header_ident];
    if (ident.empty()) ident = req.get_peer_name().to_string();
    Peer me(req, app, options,ua,ident);
    bool res =co_await Server::accept(me._stream, me._req);
    if (!res) co_return true;

    me._req.log_message("Connected client: "+std::string(ua));

    //auth is always requested, but it is not always checked
    me.prepare_auth_challenge();
    me.send(Command::AUTH, {me._auth_pubkey});

    auto listener = me.listen_publisher();
    try {
        while (true) {
            Message msg = co_await me._stream.read();
            if (msg.type == Type::text) me.processMessage(msg.payload);
            else if (msg.type == Type::connClose) break;
            co_await me._stream.wait_for_flush();
        }
    } catch (...) {
        e = std::current_exception();
    }
    me._subscriber.kick_me();
    co_await listener;
    if (e) std::rethrow_exception(e);
    co_return true;
}

template<typename Fn>
void Peer::filter_event(const docdb::Structured &doc, Fn fn) const  {
    std::lock_guard _(_mx);
    for (const auto &sbs: _subscriptions) {
        bool found = false;
        for (const auto &f: sbs.second) {
            if (f.test(doc)) {
                found = true;
                break;
            }
        }
        if (found) {
            fn(sbs.first);
        }
    }
}


cocls::future<void> Peer::listen_publisher() {
    bool nx = co_await _subscriber.next();
    while (nx) {
        const EventSource &v = _subscriber.value();
        filter_event(v.event, [&](const std::string_view &s){
            send(Command::EVENT, {s, &v.event});
        });
        nx = co_await _subscriber.next();
    }
    co_return;
}


void Peer::processMessage(std::string_view msg_text) {
    _req.log_message([&](auto logger){
        std::ostringstream buff;
        buff << "Received: " << msg_text;
        logger(buff.view());
    });

    auto msg = docdb::Structured::from_json(msg_text);
    std::string_view cmd_text = msg[0].as<std::string_view>();


    Command cmd = commands[cmd_text];

    _sensor.update([&](ClientSensor &szn){
        ++szn.command_counter;
    });

    switch (cmd) {
        case Command::EVENT:
            if (!check_for_auth()) return;
            on_event(msg); break;
        case Command::REQ:
            if (!check_for_auth()) return;
            on_req(msg);break;
        case Command::COUNT:
            if (!check_for_auth()) return;
            on_count(msg);break;
        case Command::CLOSE:
            if (!check_for_auth()) return;
            on_close(msg);break;
        case Command::HELLO:
            _hello_recv = true;
            _client_capabilities = msg[1];
            if (!check_for_auth()) return;
            send_welcome();
            break;
        case Command::AUTH:
            process_auth(msg[1]);
            break;
        default: {
            send_notice(std::string("Unknown command: ").append(cmd_text));
        }
    }
}

cocls::suspend_point<bool> Peer::send(Command command, std::initializer_list<docdb::Structured> args) {
    return ::nostr_server::send(_stream, command, args, [this](std::string_view text){
        _req.log_message([&](auto emit){
            docdb::Buffer<char, 270> line;
            line.append("Send:");
            line.append(text);
            emit(line);
        });
    });
}

class Blocked: public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};


void Peer::send_error(std::string_view id, std::string_view text) {
    send(Command::OK, {id, false, text});
    _sensor.update([&](ClientSensor &szn){
        szn.error_counter++;
    });

}

void Peer::on_event(docdb::Structured &msg) {
    std::string id;
    try {
        docdb::Structured event(std::move(msg.at(1)));
        id = event["id"].as<std::string_view>();
        std::string_view pubkey = event["pubkey"].as<std::string_view>();
        auto now = std::chrono::system_clock::now();
        if (!_no_limit && !_rate_limiter.test_and_add(now)) {
            send_error(id,
                "rate-limited: you can only post "+std::to_string(_options.event_rate_limit)
                +" events every " + std::to_string(_options.event_rate_window) + " seconds");
            return;
        }
        if (!_no_limit && _options.pow>0 && !check_pow(id)){
            send_error(id,"pow: required difficulty " + std::to_string(_options.pow));
            return;
        }
        if (!_secp.has_value()) {
            _secp.emplace();
        }
        if (!_secp->verify(event)) {
            throw std::invalid_argument("Signature verification failed");
        }
        auto kind = event["kind"].as<unsigned int>();
        if (kind >= 20000 && kind < 30000) { //Ephemeral event  - do not store
            _app->get_publisher().publish(EventSource{std::move(event),{}});
            _sensor.update([&](ClientSensor &szn){szn.report_kind(kind);});
            return;
        }
        if (!_no_limit && _options.read_only && _options.replicators.find(pubkey) == _options.replicators.npos) {
            throw Blocked("Sorry, server is in read_only mode");
        }
        if (kind == 5) {
            event_deletion(std::move(event));
            return;
        }
        if (!_no_limit && !_options.foreign_relaying && _options.auth) {
            if (pubkey != _auth_pubkey) {
                throw Blocked("Relaying foreign events is not allowed here");
            }
        }
        if (!_no_limit && _options.block_strangers) {
            bool unblock = _app->is_home_user(pubkey);
            if (!unblock) {
                const Event::Array &tags = event["tags"].array();
                for (const auto &t : tags) {
                    std::string_view tname = t[0].as<std::string_view>();
                    if (tname == "p") {
                        unblock = _app->is_home_user(t[1].as<std::string_view>());
                    } else if (tname == "delegation") {
                        unblock = _app->is_home_user(t[1].as<std::string_view>());
                    }
                    if (unblock) break;
                }
                if (!unblock) {
                    throw Blocked("This place is not for strangers");
                }
            }
        }
        _app->publish(std::move(event), _source_ident);
        send(Command::OK, {id, true, ""});
        _sensor.update([&](ClientSensor &szn){szn.report_kind(kind);});
    } catch (const docdb::DuplicateKeyException &e) {
        _shared_sensor.update([](SharedStats &stats){++stats.duplicated_post;});
        send(Command::OK, {id, true, "duplicate:"});
    } catch (const Blocked &e) {
        send_error(id, std::string("blocked: ") + e.what());
    } catch (const std::invalid_argument &e) {
        send_error(id, std::string("invalid: ") + e.what());
    } catch (const std::bad_cast &e) {
        send_error(id, "invalid: Malformed event");
    } catch (const std::exception &e) {
        send_error(id, std::string("error:") + e.what());
    }
}



void Peer::on_req(const docdb::Structured &msg) {
    std::lock_guard _(_mx);

    const auto &rq = msg.array();
    std::size_t limit = 0;
    std::vector<Filter> flts;
    std::string subid = rq[1].as<std::string>();
    for (std::size_t pos = 2; pos < rq.size(); ++pos) {
        auto filter = Filter::create(rq[pos]);
        if (filter.limit.has_value()) {
            limit = std::max<std::size_t>(limit, *filter.limit);
        } else {
            limit = static_cast<std::size_t>(-1);
        }
        flts.push_back(filter);
    }


    _app->find_in_index(_rscalc, flts);

    auto candidates = _rscalc.pop();
    if (!candidates.is_inverted()) {

        const auto &storage = _app->get_storage();

        std::sort(candidates.begin(), candidates.end(), [&](const auto &a, const auto &b){
            return a.value > b.value;
        });

        for (const auto &cd: candidates) {
            if (!limit) break;
            auto doc = storage.find(cd.id);
            if (doc) {
                const Event &ev = doc->content;
                for (const auto &f: flts) {
                    if (f.test(ev)) {
                        if (!send(Command::EVENT, {subid, ev})) return;
                        --limit;
                        break;
                    }
                }
            } else {
                _req.log_message("Event missing for ID:"+std::to_string(cd.id), 10);
            }
        }


    }

    send(Command::EOSE, {subid});

    _subscriptions.emplace_back(subid, std::move(flts));
    _sensor.update([&](ClientSensor &szn){
        ++szn.query_counter;
        szn.subscriptions = _subscriptions.size();
     });

}

void Peer::on_count(const docdb::Structured &msg) {
    const auto &rq = msg.array();
    std::vector<Filter> flts;
    std::string subid = rq[1].as<std::string>();
    for (std::size_t pos = 2; pos < rq.size(); ++pos) {
       auto filter = Filter::create(rq[pos]);
       flts.push_back(filter);
    }
    _app->find_in_index(_rscalc, flts);
    std::intmax_t count = _rscalc.top().size();
    send(Command::COUNT, {subid, {{"count", count}}});
}

void Peer::on_close(const docdb::Structured &msg) {
    std::lock_guard _(_mx);
    std::string id = msg[1].as<std::string>();
    auto iter = std::remove_if(_subscriptions.begin(), _subscriptions.end(),
            [&](const auto &s) {
        return s.first == id;
    });
    _subscriptions.erase(iter, _subscriptions.end());
    _sensor.update([&](ClientSensor &szn){
        szn.subscriptions = _subscriptions.size();
        szn.max_subscriptions = std::max(szn.max_subscriptions, szn.subscriptions);
    });
}

void Peer::event_deletion(Event &&event) {
    bool deleted_something = false;
    auto id = event["id"].as<std::string_view>();
    auto pubkey = event["pubkey"].as<std::string_view>();
    Filter flt;
    for (const auto &item: event["tags"].array()) {
        if (item[0].as<std::string_view>() == "e") {
           flt.ids.push_back(item[1].as<std::string>());
        }
    }
    auto &storage = _app->get_storage();
    docdb::Batch b;
    if (!flt.ids.empty()) {
        _app->find_in_index(_rscalc, {flt});
        _rscalc.list(_app->get_storage(), [&](docdb::DocID id, const auto &, const auto &r){
           if (r)  {
               const Event &ev = r->content;
               std::string p = ev["pubkey"].as<std::string>();
              if (p != pubkey) {
                  throw std::invalid_argument("pubkey missmatch");
              }
              if (deleted_something) {
                  storage.erase(b, id);
              } else {
                  storage.put(b, event, id);
              }
              deleted_something = true;
           }
        });
    }

    if (deleted_something) {
        storage.get_db()->commit_batch(b);
        _app->get_publisher().publish(EventSource{std::move(event),{}});
    }
    send(Command::OK, {id, true, ""});
    _sensor.update([&](ClientSensor &szn){szn.report_kind(5);});
}

bool Peer::check_pow(std::string_view id) const {
    int bits = 0;
    static constexpr int nibble_sizes[16] = {4,3,2,2,1,1,1,1,0,0,0,0,0,0,0,0};
    for (char c: id) {
        int nibble;
        if (c >= '0' && c<='9') nibble = c - '0';
        else if (c >= 'A' && c<='F') nibble = c - 'A'+10;
        else if (c >= 'a' && c<='f') nibble = c - 'a'+10;
        else return false;
        if (nibble != 0) {
            bits += nibble_sizes[nibble];
            return (bits >= _options.pow);
        } else {
            bits+=4;
        }
    }
    return true;
}

void Peer::prepare_auth_challenge() {
    _auth_pubkey.clear();
    std::random_device rdev;
    std::default_random_engine rnd(rdev());
    std::uniform_int_distribution<int> rnum(33,126);
    for (int i = 0; i<15; i++) {
        _auth_pubkey.push_back(rnum(rnd));
    }
}

void Peer::process_auth(const Event &event) {
    auto id = event["id"].as<std::string_view>();
    try {
        auto now = std::chrono::system_clock::now();
        if (!_secp.has_value()) {
            _secp.emplace();
        }
        if (!_secp->verify(event)) {
            throw std::invalid_argument("Signature verification failed");
        }
        auto kind = event["kind"].as<unsigned int>();
        if (kind != 22242) {
            throw std::invalid_argument("Unsupported kind");
        }
        auto created_at = std::chrono::system_clock::from_time_t(event["created_at"].as<std::time_t>());
        if (std::chrono::duration_cast<std::chrono::minutes>(now - created_at).count() > 10) {
            throw std::invalid_argument("Challenge expired");
        }
        auto tags = event["tags"].array();
        auto chiter = std::find_if(tags.begin(), tags.end(), [&](const Event &x) {
           return x[0].as<std::string_view>() == "challenge";
        });
        if (chiter == tags.end() || (*chiter)[1].as<std::string_view>() != _auth_pubkey) {
            throw std::invalid_argument("Invalid challenge");
        }
        _authent = true;
        _auth_pubkey = event["pubkey"].as<std::string>();
        if (event["my_url"].contains<std::string>()) {
            _source_ident =event["my_url"].as<std::string>();
        }

        if (_options.replicators.find(_auth_pubkey) != _options.replicators.npos) {
            _no_limit = true;
        }

        if (_hello_recv) {
            send_welcome();
        } else {
            send(Command::OK,{ id, true, "Welcome on relay!"});
        }
    } catch (const std::exception &e) {
        send_error(id,std::string("restricted:") + e.what());
        if (_hello_recv) {
            _stream.close();
        }
    }

}

void Peer::send_notice(std::string_view text) {
    send(Command::NOTICE,{text});
    _sensor.update([&](ClientSensor &szn){
        szn.error_counter++;
    });
}

bool Peer::check_for_auth() {
    if (!_options.auth) return true;
    if (!_authent) {
        send_notice("restricted: Authentication is mandatory on this relay");
        return false;
    }
    return true;
}

void Peer::send_welcome() {
    send(Command::WELCOME,{ _app->get_server_capabilities()});
}

}
