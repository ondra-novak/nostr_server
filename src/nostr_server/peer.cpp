#include "kinds.h"

#include <coroserver/http_ws_server.h>
#include <coroserver/http_stringtables.h>
#include <docdb/json.h>

#include "peer.h"
#include "protocol.h"

#include <openssl/sha.h>
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
/*    _sensor.enable(std::move(ident), std::move(user_agent));
    _shared_sensor.enable();*/
    _app->client_counter(1, _req.get_url());
}
Peer::~Peer() {
    _app->client_counter(-1, _req.get_url());
}



using namespace coroserver::ws;

cocls::future<bool> Peer::client_main(coroserver::http::ServerRequest &req, PApp app, const ServerOptions & options) {
    std::exception_ptr e;
    auto ua = req[coroserver::http::strtable::hdr_user_agent];
    std::string ident;
    if (!options.http_header_ident.empty())  ident = req[options.http_header_ident];
    if (ident.empty()) ident = req.get_peer_name().to_string();
    Peer me(req, app, options,ua,ident);
    bool res =co_await Server::accept(me._stream, me._req,{50000,50000},{false, std::max(options.max_message_size, options.attachment_max_size)});
    if (!res) co_return true;

    me._req.log_message("Connected client: "+std::string(ua), static_cast<int>(PeerServerity::progress));

    //auth is always requested, but it is not always checked
    me.prepare_auth_challenge();
    me.send({commands[Command::AUTH], me._auth_nonce});

    auto listener = me.listen_publisher();
    try {
        bool rep = true;
        while (rep) {
            Message msg = co_await me._stream.read();
            switch (msg.type) {
                case Type::text:me.processMessage(msg.payload);break;
                case Type::binary:me.processBinaryMessage(msg.payload);break;
                case Type::connClose: rep = false; continue;
                case Type::largeFrame:
                    me.send_notice("Message too large");
                    break;
                default: break;
            }
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
void Peer::filter_event(const Event &doc, Fn fn) const  {
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
        filter_event(v.first, [&](std::string_view s){
            JSON doc = v.first.toStructured();
            send({commands[Command::EVENT], s, &doc});
        });
        nx = co_await _subscriber.next();
    }
    co_return;
}


void Peer::processMessage(std::string_view msg_text) {
    if (msg_text.size() > _options.max_message_size) {
        send_notice("Text message is too long");
    }
    try {
        _req.log_message([&](auto logger){
            std::ostringstream buff;
            buff << "Received: " << msg_text;
            logger(buff.view());
        });

        auto msg = docdb::Structured::from_json(msg_text);
        std::string_view cmd_text = msg[0].as<std::string_view>();


        Command cmd = commands[cmd_text];

/*        _sensor.update([&](ClientSensor &szn){
            ++szn.command_counter;
        });*/

        switch (cmd) {
            case Command::EVENT:
                on_event(msg); break;
            case Command::REQ:
                on_req(msg);break;
            case Command::COUNT:
                on_count(msg);break;
            case Command::CLOSE:
                on_close(msg);break;
            case Command::AUTH:
                process_auth(msg[1]);
                break;
            case Command::FILE:
                on_file(msg);break;
                break;
            case Command::RETRIEVE:
                on_retrieve(msg);break;
                break;
            default: {
                send_notice(std::string("Unknown command: ").append(cmd_text));
            }
        }
    } catch (std::exception &e) {
        send_notice("error: Internal error - command ignored");
        _req.log_message([&](auto emit){
            std::string msg = "Peer exception:";
            msg.append(e.what());
            emit(msg);
        },static_cast<int>(PeerServerity::warn));
    }
}

cocls::suspend_point<bool> Peer::send(const JSON &msgdata) {
    std::string json = msgdata.to_json(JSON::flagUTF8);
    _req.log_message([&](auto emit){
        std::string msg = "Send: ";
        msg.append(json);
        emit(msg);
    }, 0);
    return _stream.write({json});
}

class Blocked: public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};


void Peer::send_error(std::string_view id, std::string_view text) {
    send({commands[Command::OK], id, false, text});
/*    _sensor.update([&](ClientSensor &szn){
        szn.error_counter++;
    });*/
    _req.log_message(text, static_cast<int>(PeerServerity::warn));

}

void Peer::on_event(const JSON &msg) {
    on_event_generic(msg[1],[&](const std::string &id, Event &&event){
        _app->publish(std::move(event), this);
         send({commands[Command::OK], id, true, ""});
    },false);
}
template<typename Fn>
void Peer::on_event_generic(const JSON &msg, Fn &&on_verify, bool no_special_events) {
    std::string id;
    try {
        id = msg["id"].as<std::string>();
        Event event = Event::fromStructured(msg);
        if (_app->find_event_by_id(event.id) && !no_special_events) {
            send({commands[Command::OK], id, true, "duplicate: ok"});
/*            _shared_sensor.update([](SharedStats &stats){++stats.duplicated_post;});*/
            return;
        }
//        std::string_view pubkey = event["pubkey"].as<std::string_view>();
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
        if (_authent && _app->is_home_user(_auth_pubkey)) {
            event.trusted = true;
        }
        if (!event.verify(*_secp)) {
            throw std::invalid_argument("Signature verification failed");
        }
        const auto &k = event.kind;
        if (k >= kind::Ephemeral_Begin && k < kind::Ephemeral_End) { //Ephemeral event  - do not store
            if (no_special_events) throw std::invalid_argument("This event is not allowed here");
            _app->get_publisher().publish(EventSource{std::move(event),this});
 /*           _sensor.update([&](ClientSensor &szn){szn.report_kind(kind);});*/
            send({commands[Command::OK], id, true, ""});
            return;
        }
        if (!_no_limit && _options.read_only /*&& _options.replicators.find(pubkey) == _options.replicators.npos*/) {
            throw Blocked("Sorry, server is in read_only mode");
        }
        if (!_no_limit && _options.whitelisting && !event.trusted) {
            if (!_app->check_whitelist(event.author)) {
                if (k == kind::Encrypted_Direct_Messages || k == kind::Gift_Wrap_Event) {  //receiver must be a local user
                    auto target = event.get_tag_content("p");
                    auto pk = Event::Pubkey::from_hex(target);
                    if (!_app->is_home_user(pk)) throw Blocked("Target user not found");
                } else {
                    throw Blocked("Not invited");
                }
            }
        }
        if (k == 5) {
            if (no_special_events) throw std::invalid_argument("This event is not allowed here");
            event_deletion(event);
            return;
        }
        on_verify(id, std::move(event));
/*        _sensor.update([&](ClientSensor &szn){szn.report_kind(kind);});*/
    } catch (const EventParseException &e) {
        send_error(id, std::string("invalid: ")+e.what());
    } catch (const docdb::DuplicateKeyException &e) {
/*        _shared_sensor.update([](SharedStats &stats){++stats.duplicated_post;});*/
        send({commands[Command::OK], id, true, "duplicate:"});
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
                const EventOrAttachment &evatt = doc->document;
                if (std::holds_alternative<Event>(evatt)) {
                    const Event &ev = std::get<Event>(evatt);
                    for (const auto &f: flts) {
                        if (f.test(ev)) {
                            auto sevent = ev.toStructured();
                            if (ev.nip97) {
                                std::string url ("http");
                                url.append(std::string_view(_req.get_url()).substr(2));
                                url.append(_app->get_attachment_link(ev.id, ev.get_tag_content("m")));
                                sevent.set("file_url", url);
                                sevent.set("nip97", true);
                            }
                            sevent.set("karma",_app->get_karma(ev.author));
                            if (!send({commands[Command::EVENT], subid, sevent})) return;
                            --limit;
                            break;
                        }
                    }
                } else{
                    _req.log_message("ID doesn't point to event:"+std::to_string(cd.id), static_cast<int>(PeerServerity::error));
                }
            } else {
                _req.log_message("Event missing for ID:"+std::to_string(cd.id), static_cast<int>(PeerServerity::error));
            }
        }


    }

    send({commands[Command::EOSE], subid});

    _subscriptions.emplace_back(subid, std::move(flts));
/*    _sensor.update([&](ClientSensor &szn){
        ++szn.query_counter;
        szn.subscriptions = _subscriptions.size();
     });*/

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
    send({commands[Command::COUNT], subid, {{"count", count}}});
}

void Peer::on_close(const docdb::Structured &msg) {
    std::lock_guard _(_mx);
    std::string id = msg[1].as<std::string>();
    auto iter = std::remove_if(_subscriptions.begin(), _subscriptions.end(),
            [&](const auto &s) {
        return s.first == id;
    });
    _subscriptions.erase(iter, _subscriptions.end());
/*    _sensor.update([&](ClientSensor &szn){
        szn.subscriptions = _subscriptions.size();
        szn.max_subscriptions = std::max(szn.max_subscriptions, szn.subscriptions);
    });*/
}

void Peer::event_deletion(const Event &event) {
    bool deleted_something = false;
    std::vector<Filter> flts(1);
    auto evtodel = event.get_tag_content("e");
    auto id = Event::ID::from_hex(evtodel);
    flts[0].ids.push_back({id, id.size()});
    auto &storage = _app->get_storage();
    docdb::Batch b;
    _app->find_in_index(_rscalc, flts);
    for(const auto &row: _rscalc.top()) {
        auto fdoc = storage.find(row.id);
        if (fdoc && std::holds_alternative<Event>(fdoc->document)) {
            const Event &ev = std::get<Event>(fdoc->document);
            if (ev.author != event.author) {
                throw std::invalid_argument("pubkey missmatch");
            }
            if (deleted_something) {
                storage.erase(b, row.id);
            } else {
                storage.put(b, event, row.id);
                deleted_something = true;
            }
        }
    }
    if (deleted_something) {
        storage.get_db()->commit_batch(b);
        _app->get_publisher().publish(EventSource{event,this});
    }
    send({commands[Command::OK], event.id.to_hex(), true, ""});
    /*_sensor.update([&](ClientSensor &szn){szn.report_kind(5);});*/
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
    _auth_nonce.clear();
    std::random_device rdev;
    std::default_random_engine rnd(rdev());
    std::uniform_int_distribution<int> rnum(33,126);
    for (int i = 0; i<15; i++) {
        _auth_nonce.push_back(rnum(rnd));
    }
}

void Peer::process_auth(const JSON &jevent) {
    auto id = jevent["id"].as<std::string_view>();
    try {
        Event event = Event::fromStructured(jevent);
        auto now = std::chrono::system_clock::now();
        if (!_secp.has_value()) {
            _secp.emplace();
        }
        if (!event.verify(*_secp)) {
            throw std::invalid_argument("Signature verification failed");
        }
        auto k = event.kind;
        if (k != kind::Client_Authentication) {
            throw std::invalid_argument("Unsupported kind");
        }
        auto created_at = std::chrono::system_clock::from_time_t(event.created_at);
        if (std::chrono::duration_cast<std::chrono::minutes>(now - created_at).count() > 10) {
            throw std::invalid_argument("Challenge expired");
        }
        std::string challenge = event.get_tag_content("challenge");
        if (challenge != _auth_nonce) {
            throw std::invalid_argument("Invalid challenge");
        }
        _authent = true;
        _auth_pubkey = event.author;
/*
        if (_options.replicators.find(_auth_pubkey) != _options.replicators.npos) {
            _no_limit = true;
        }
*/
        send({commands[Command::OK], id, true, "Welcome to relay!"});
    } catch (const std::exception &e) {
        send_error(id,std::string("restricted:") + e.what());
    }

}


void Peer::processBinaryMessage(std::string_view msg_text) {
    if (!_file_event.has_value()) {
        send_notice("unsupported binary message");
        return;
    }

    Event &ev = *_file_event;

    Attachment::ID hash;
    SHA256(reinterpret_cast<const unsigned char *>(msg_text.data()), msg_text.size(), hash.data());

    Attachment::ID need_hash = Attachment::ID::from_hex(ev.get_tag_content("x"));
    std::size_t sz = std::strtoull(ev.get_tag_content("size").c_str(),nullptr,10);
    if (sz != msg_text.size() || hash != need_hash) {
        send({commands[Command::OK], ev.id.to_hex(), false, "invalid: file mismatch"});
        return;
    }

    ev.nip97 = true;

    if (_app->find_event_by_id(_file_event->id) ) {
        send({commands[Command::OK], ev.id.to_hex(), true, "duplicate"});
        return;
    }

    try {
        auto id = _app->find_attachment(hash);
        if (id) {
          _app->publish(std::move(ev), nullptr);
        } else {
          _app->publish(std::move(ev),Attachment{hash,  std::string(msg_text)}, nullptr);
        }
        _file_event.reset();
        send({commands[Command::OK], ev.id.to_hex(), true, ""});
    } catch (std::exception &e) {
        _file_event.reset();
        send({commands[Command::OK], ev.id.to_hex(), false, "error: internal error"});
       throw;
    }
}

void Peer::on_file(const JSON &msg) {
        on_event_generic(msg[1], [&](const std::string &id, Event &&event){
            try {
                if (event.kind != kind::File_Header) throw FileError::unsupported_kind;

                std::string mime;
                std::string hash;
                std::string size;

                for (const auto &x: event.tags) {
                    if (x.tag == "x") {
                        if (hash.empty()) hash = x.content; else throw FileError::malformed;
                    } else if (x.tag == "m") {
                        if (mime.empty()) mime = x.content; else throw FileError::malformed;
                    } else if (x.tag == "size") {
                        if (size.empty()) size = x.content; else throw FileError::malformed;
                    }
                }
                if (size.empty() || mime.empty() || hash.size() != 64) throw FileError::malformed;

                char *s_end = nullptr;
                std::size_t sz = std::strtoul(size.c_str(), &s_end, 10);
                if (*s_end) throw FileError::malformed;

                if (sz > _options.attachment_max_size) throw FileError::max_size;
                _file_event = std::move(event);

                send({commands[Command::OK], id, true, "continue"});
            } catch (FileError e) {

                std::string msg;
                switch(e) {
                    default:
                    case FileError::malformed: msg = "invalid: malformed"; break;
                    case FileError::unsupported_kind: msg = "invalid: unsupported kind"; break;
                    case FileError::max_size: msg = "max_size: "+std::to_string(_options.attachment_max_size); break;
                }
                send({commands[Command::OK], id, false, msg});
            }

        }, true);
}

void Peer::on_retrieve(const JSON &msg) {
    const auto &stor = _app->get_storage();
    std::string id = msg[1].as<std::string>();
    auto ev_id = Binary<32>::from_hex(id);
    auto docid = _app->find_event_by_id(ev_id);
    do {
        if (!docid) break;
        auto doc = stor.find(docid);
        if (!doc || !std::holds_alternative<Event>(doc->document)) break;
        const Event &event = std::get<Event>(doc->document);
        if (!event.nip97) break;
        std::string hash = event.get_tag_content("x");
        auto att_hex = Binary<32>::from_hex(hash);
        auto att_id = _app->find_attachment(att_hex);
        if (!att_id) break;
        auto att_doc = stor.find(att_id);
        if (!att_doc || !std::holds_alternative<Attachment>(att_doc->document)) break;
        const Attachment &att = std::get<Attachment>(att_doc->document);
        send({commands[Command::OK], id, true, ""});
        _stream.write({att.data, Type::binary});
        return;
    } while (false);
    send({commands[Command::RETRIEVE], id, false, "missing: not found"});
}


void Peer::send_notice(std::string_view text) {
    send({commands[Command::NOTICE],text});
/*    _sensor.update([&](ClientSensor &szn){
        szn.error_counter++;
    });*/
}


}
