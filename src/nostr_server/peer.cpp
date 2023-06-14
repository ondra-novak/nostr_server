#include <coroserver/http_ws_server.h>
#include <coroserver/static_lookup.h>
#include <docdb/json.h>

#include "peer.h"

#include <sstream>
namespace nostr_server {

Peer::Peer(coroserver::http::ServerRequest &req, PApp app)
:_req(req)
,_app(std::move(app))
,_subscriber(_app->get_publisher())
{
}

using namespace coroserver::ws;

cocls::future<bool> Peer::client_main(coroserver::http::ServerRequest &req, PApp app) {
    std::exception_ptr e;
    Peer me(req, app);
    bool res =co_await Server::accept(me._stream, me._req);
    if (!res) co_return true;

    me._req.log_message("Connected client");

    auto listener = me.listen_publisher();
    try {
        while (true) {
            Message msg = co_await me._stream.read();
            if (msg.type == Type::text) me.processMessage(msg.payload);
            else if (msg.type == Type::connClose) break;
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

NAMED_ENUM(Command,
        unknown,
        REQ,
        EVENT,
        CLOSE,
        COUNT,
        EOSE,
        NOTICE,
        OK
);
constexpr NamedEnum_Command commands={};


cocls::future<void> Peer::listen_publisher() {
    bool nx = co_await _subscriber.next();
    while (nx) {
        const Event &v = _subscriber.value();
        filter_event(v, [&](const std::string_view &s){
            send({commands[Command::EVENT], s, &v});
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
    switch (cmd) {
        case Command::EVENT: on_event(msg); break;
        case Command::REQ: on_req(msg);break;
        case Command::COUNT: on_count(msg);break;
        case Command::CLOSE: on_close(msg);break;
        default: {
            send({commands[Command::NOTICE], std::string("Unknown command: ").append(cmd_text)});
        }
    }

}

cocls::suspend_point<bool> Peer::send(const docdb::Structured &msgdata) {
    std::string json = msgdata.to_json(docdb::Structured::flagUTF8);
    _req.log_message([&](auto emit){
        std::string msg = "Send: ";
        msg.append(json);
        emit(msg);
    }, 0);
    return _stream.write({json});
}


void Peer::on_event(docdb::Structured &msg) {
    std::string id;
    try {
        auto &storage = _app->get_storage();
        docdb::Structured event(std::move(msg.at(1)));
        id = event["id"].as<std::string>();
        if (!_secp.has_value()) {
            _secp.emplace();
        }
        if (!_secp->verify(event)) {
            throw std::invalid_argument("Signature verification failed");
        }
        auto kind = event["kind"].as<unsigned int>();
        if (kind >= 20000 && kind < 30000) { //empheral event  - do not store
            _app->get_publisher().publish(std::move(event));
            return;
        }
        if (kind == 5) {
            event_deletion(std::move(event));
            return;
        }
        auto to_replace = _app->doc_to_replace(event);
        if (to_replace != docdb::DocID(-1)) {
            storage.put(event, to_replace);
        }
        _app->get_publisher().publish(std::move(event));
        send({commands[Command::OK], id, true, ""});
    } catch (const std::invalid_argument &e) {
        send({commands[Command::OK], id, false, std::string("invalid:") + e.what()});
    } catch (const docdb::DuplicateKeyException &e) {
        send({commands[Command::OK], id, true, "duplicate:"});
    } catch (const std::bad_cast &e) {
        send({commands[Command::OK], id, false, "invalid: Malformed event"});
    } catch (const std::exception &e) {
        send({commands[Command::OK], id, false, std::string("error:") + e.what()});
    }

}



void Peer::on_req(const docdb::Structured &msg) {
    std::lock_guard _(_mx);

    const auto &rq = msg.array();
    std::size_t limit = 0;
    std::vector<IApp::Filter> flts;
    std::string subid = rq[1].as<std::string>();
    for (std::size_t pos = 2; pos < rq.size(); ++pos) {
        auto filter = IApp::Filter::create(rq[pos]);
        if (filter.limit.has_value()) {
            limit = std::max<std::size_t>(limit, *filter.limit);
        } else {
            limit = static_cast<std::size_t>(-1);
        }
        flts.push_back(filter);
    }


    IApp::FTRList ftlist;
    auto &storage = _app->get_storage();
    bool fnd = _app->find_in_index(_rscalc, flts, std::move(ftlist));

    auto filter_docs = [&](auto fn) {
        _rscalc.documents(storage, [&](docdb::DocID id, const auto &doc){
            if (doc) {
                for (const auto &f: flts) {
                    if (f.test(doc->content)) {
                       fn(doc->content);
                       break;
                    }
                }
            } else {
                std::string error = "Document missing: " + std::to_string(id);
                _req.log_message(error, 10);
            }
        });
    };


    if (fnd) {
        if (!ftlist.empty()) {
            auto top = _rscalc.pop();
            std::sort(top.begin(), top.end(), [&](docdb::DocID a, docdb::DocID b){
               auto it1 = std::lower_bound(ftlist.begin(),ftlist.end(), IApp::FulltextRelevance{a,0});
               auto it2 = std::lower_bound(ftlist.begin(),ftlist.end(), IApp::FulltextRelevance{b,0});
               return it1->second < it2->second;
            });
            if (limit && limit < top.size()) {
                top.resize(limit);
            }
            _rscalc.push(std::move(top));
            filter_docs([&](const auto &doc){
                if (!send({commands[Command::EVENT], subid, &doc})) return;
            });
        }else if (limit > 0 && _rscalc.top().size()>limit) {
            std::vector<Event> events;
            filter_docs([&](auto &doc){
                events.push_back(std::move(doc));
            });
            std::sort(events.begin(), events.end(), [](const Event &a, const Event &b){
                return a["created_at"].as<std::time_t>() < b["created_at"].as<std::time_t>();
            });
            auto iter = events.begin();
            if (events.size()>limit) {
                std::advance(iter, events.size()-limit);
            }
            while (iter != events.end()) {
                if (!send({commands[Command::EVENT], subid, &(*iter)})) return;
                ++iter;
            }
        } else {
            filter_docs([&](const auto &doc){
                if (!send({commands[Command::EVENT], subid, &doc})) return;
            });
        }
    }

    send({commands[Command::EOSE], subid});

    _subscriptions.emplace_back(subid, std::move(flts));

}

void Peer::on_count(const docdb::Structured &msg) {
    const auto &rq = msg.array();
    std::vector<IApp::Filter> flts;
    std::string subid = rq[1].as<std::string>();
    for (std::size_t pos = 2; pos < rq.size(); ++pos) {
       auto filter = IApp::Filter::create(rq[pos]);
       flts.push_back(filter);
    }
    bool fnd = _app->find_in_index(_rscalc, flts, {});
    std::intmax_t count = 0;
    if (fnd) {
        count = _rscalc.top().size();
    }
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
}

void Peer::event_deletion(Event &&event) {
    bool deleted_something = false;
    auto pubkey = event["pubkey"].as<std::string_view>();
    IApp::Filter flt;
    for (const auto &item: event["tags"].array()) {
        if (item[0].as<std::string_view>() == "e") {
           flt.ids.push_back(item[1].as<std::string>());
        }
    }
    auto &storage = _app->get_storage();
    docdb::Batch b;
    if (!flt.ids.empty()) {
        bool ok= _app->find_in_index(_rscalc, {flt}, {});
        if (ok) {
            _rscalc.documents(_app->get_storage(), [&](docdb::DocID id, const auto &r){
               if (r)  {
                   const Event &ev = r->content;
                   std::string p = ev["pubkey"].as<std::string>();
                  if (p != pubkey) {
                      throw std::invalid_argument("pubkey missmatch");
                  }
                  storage.erase(b, id);
                  deleted_something = true;
               }
            });
        }
    }

    if (deleted_something) {
        storage.put(b, event);
        storage.get_db()->commit_batch(b);
        _app->get_publisher().publish(std::move(event));
    }
    send({commands[Command::OK], true});
}

}
