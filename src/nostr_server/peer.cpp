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
    Peer me(req, app);
    bool res =co_await Server::accept(me._stream, me._req);
    if (!res) co_return true;

    me._req.log_message("Connected client");

    auto listener = me.listen_publisher();
    while (true) {
        Message msg = co_await me._stream.read();
        if (msg.type == Type::text) me.processMessage(msg.payload);
        else if (msg.type == Type::connClose) break;
    }
    me._subscriber.kick_me();
    co_await listener;
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

void Peer::send(const docdb::Structured &msgdata) {
    std::string json = msgdata.to_json();
    _req.log_message([&](auto emit){
        std::string msg = "Send: ";
        msg.append(json);
        emit(msg);
    }, 0);
    _stream.write({json});
}

void Peer::on_event(docdb::Structured &msg) {
    try {
        auto &storage = _app->get_storage();
        docdb::Structured doc(std::move(msg.at(1)));
        auto kind = doc["kind"].as<unsigned int>();
        if (kind >= 20000 && kind < 30000) { //empheral event  - do not store
            _app->get_publisher().publish(std::move(doc));
            return;
        }
        auto to_replace = _app->doc_to_replace(doc);
        if (to_replace != docdb::DocID(-1)) {
            storage.put(doc, to_replace);
        }
        _app->get_publisher().publish(std::move(doc));
        send({commands[Command::OK], true});
    } catch (const std::exception &e) {
        send({commands[Command::OK], false, e.what()});
    }

}



void Peer::on_req(const docdb::Structured &msg) {
    std::lock_guard _(_mx);

    const auto &rq = msg.array();
    std::vector<IApp::Filter> flts;
    std::string subid = rq[1].as<std::string>();
    auto filter = IApp::Filter::create(rq[2]);
    flts.push_back(filter);
    std::vector<docdb::DocID> ids = _app->find_in_index(filter);
    std::vector<docdb::DocID> b;
    for (std::size_t i = 3; i < ids.size(); i++) {
        auto filter = IApp::Filter::create(rq[i]);
        flts.push_back(filter);
        std::vector<docdb::DocID> a = _app->find_in_index(filter);
        merge_ids(ids, a, b);
    }
    if (filter.limit.has_value() && *filter.limit < ids.size()) {
        ids.resize(*filter.limit);
    }

    auto &storage = _app->get_storage();
    for (const auto &id: ids) {
        auto doc = storage.find(id);
        if (doc) {
            for (const auto &f: flts) {
                if (f.test(doc->content)) {
                    send({commands[Command::EVENT], subid, &doc->content});
                    break;
                }
            }
        }
    }

    send({commands[Command::EOSE], subid});

    _subscriptions.emplace_back(subid, std::move(flts));

}

void Peer::on_count(const docdb::Structured &msg) {
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

}
