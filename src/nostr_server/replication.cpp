#include "replication.h"

#include <coroserver/https_client.h>
#include <coroserver/http_client_request.h>
#include <coroserver/http_ws_client.h>

#include "commands.h"
#include "signature.h"

#include "event_tools.h"

#include <set>
namespace nostr_server {

ReplicationService::ReplicationService(PApp app, ReplicationConfig &&cfg)
:_app(app)
,_cfg(std::move(cfg))
,_index(_app->get_database(), "replication")
,_sslctx(coroserver::ssl::Context::init_client())

{
    if (!cfg.private_key.empty()) {
        SignatureTools::from_nsec(_cfg.private_key, _pk);
    }
}

ReplicationService::TasksFutures ReplicationService::init_tasks(coroserver::ContextIO ctx) {
    TasksFutures tasks;
    std::set<std::string, std::less<> > outbound_active;
    for (const auto &t: _cfg.tasks) {
        if (t.inbound) {
            /* not implemented yet */
        } else {
            outbound_active.insert(t.relay_url);
            std::unique_ptr<cocls::future<void > > tfut (new auto(start_outbound_task(ctx, t)));
            tasks.push_back(std::move(tfut));
        }
    }

    docdb::Batch b;
    for(const auto &row: _index.select_all()) {
        auto [relay] =row.key.get<std::string_view>();
        if (outbound_active.find(relay) == outbound_active.end()) {
            docdb::Key k = row.key;
            _index.erase(b, k);
        }
    }
    _index.get_db()->commit_batch(b);
    return tasks;
}


cocls::future<void> ReplicationService::start(coroserver::ContextIO ctx, PApp app, ReplicationConfig &&cfg) {
    ReplicationService service(app, std::move(cfg));

    auto tasks = service.init_tasks(ctx);

    for (auto &f: tasks) {
        co_await *f;
    }
    co_return;
}

cocls::future<void> ReplicationService::start_outbound_task(coroserver::ContextIO ctx, const ReplicationTask &cfg) {

    coroserver::AsyncSupport async(ctx);
    std::string relay;
    if (cfg.relay_url.compare(0,3,"ws:") == 0) relay = "http:"+cfg.relay_url.substr(3);
    else if (cfg.relay_url.compare(0,4,"wss:") == 0) relay = "https:"+cfg.relay_url.substr(4);
    else co_return;

    do {
        try {
            coroserver::WaitResult r = co_await async.wait_for(std::chrono::seconds(5), nullptr);
            if (r == coroserver::WaitResult::closed) break;

            coroserver::https::Client httpsclient(ctx, _sslctx, "novacisko_nostr_server");
            auto p = co_await httpsclient.open(coroserver::http::Method::GET, relay);
            coroserver::http::ClientRequest req(p);
            coroserver::ws::Stream stream;
            if (co_await coroserver::ws::Client::connect(stream, req)) {
                co_await run_outbound(cfg, std::move(stream));
            }
        } catch (...) {
            //todo log error
        }

    } while (true);

}


cocls::future<void> ReplicationService::run_outbound_recv(const ReplicationTask &cfg, coroserver::ws::Stream stream) {
    Command cmd;
    std::vector<docdb::Structured> args;
    std::set<docdb::DocID> err_ids;
    do {
        const coroserver::ws::Message wsmsg = co_await stream.read();
        bool state = receive(wsmsg, cmd, args, [](auto){});
        if (!state) break;
        switch (cmd) {
            case Command::AUTH: if (!args.empty()) {
                SignatureTools signtool;
                std::string challenge = args[0].to_string();
                auto ev = create_event(22242, "", {
                        {"relay",cfg.relay_url},
                        {"challenge", challenge},
                        {"my_url", _cfg.this_relay_url}
                    });
                signtool.sign(_pk, ev);
                co_await send(stream, Command::AUTH, {ev}, [](auto){});
            } else {
                co_return;
            }break;

            case Command::OK: if (args.size()>1){
                std::string_view id = args[0].as<std::string_view>();
                bool state = args[1].as<bool>();
                docdb:: DocID did = _app->find_by_id(id);
                if (did) {
                    if (state) {
                        _index.put(cfg.relay_url, did);
                    } else {
                        auto iter = err_ids.find(did);
                        if (iter == err_ids.end()) {
                            err_ids.emplace(did);
                            auto docinfo = _app->get_storage().find(did);
                            if (docinfo) {
                                bool b = co_await send(stream, Command::EVENT, {docinfo->document}, [](auto){});
                                if (!b) co_return;
                            }
                        }
                    }
                }
            }break;
            default:
                break;
        }

    }while (true);


}

cocls::future<void> ReplicationService::run_outbound(const ReplicationTask &cfg, coroserver::ws::Stream stream) {

    EventSubscriber subscriber(_app->get_publisher());

    auto state = _index.find(cfg.relay_url);
    docdb::DocID id = 0;
    if (state) {
        auto [x] = state->get<docdb::DocID>();
        id = x;
    }
    auto rcv = run_outbound_recv(cfg, stream);
    try {
        for (const auto &row : _app->get_storage().select_from(id)) {
            if (row.has_value) {
                bool b = co_await send(stream, Command::EVENT, {row.document}, [](auto){});
                if (!b) throw std::runtime_error("SYNC: Connection reset");
                co_await stream.wait_for_flush();
            }
        }

        bool b = co_await subscriber.next();
        while (b) {
            const EventSource &evs = subscriber.value();
            if (evs.relay != cfg.relay_url) {
                b = co_await send(stream, Command::EVENT, {evs.event}, [](auto){});
                if (!b) throw std::runtime_error("REPLAY: Connection reset");
            }
            b = co_await subscriber.next();
        }

    } catch (...) {
        stream.close();
        //TODO log error;
    }
    co_await rcv;
}

}
