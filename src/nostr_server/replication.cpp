#include "replication.h"

#include <coroserver/https_client.h>
#include <coroserver/http_client_request.h>
#include <coroserver/http_ws_client.h>
#include <set>
namespace nostr_server {

ReplicationService::ReplicationService(PApp app, ReplicationConfig &&cfg)
:_app(app)
,_cfg(std::move(cfg))
,_index(_app->get_database(), "replication")
,_sslctx(coroserver::ssl::Context::init_client())
{

}


cocls::future<void> ReplicationService::start(coroserver::ContextIO ctx) {
    if (_cfg.empty()) co_return;
    std::vector<std::unique_ptr<cocls::future<void > > > tasks;
    std::set<std::string, std::less<> > active;
    for (std::size_t i = 0; i<_cfg.size(); ++i) {
        active.insert(_cfg[i].task_name);
        tasks.push_back(std::unique_ptr<cocls::future<void > >(
                new auto(start_replication_task(ctx, _cfg[i].task_name, _cfg[i].relay_url))));
    }
    docdb::Batch b;
    for(const auto &row: _index.select_all()) {
        auto [name] =row.key.get<std::string_view>();
        if (active.find(name) == active.end()) {
            docdb::Key k = row.key;
            _index.erase(b, k);
        }
    }
    _index.get_db()->commit_batch(b);
    for (auto &f: tasks) {
        co_await *f;
    }
    co_return;
}

cocls::future<void> ReplicationService::start_replication_task(
        coroserver::ContextIO ctx, std::string name, std::string relay) {

    coroserver::AsyncSupport async(ctx);
    if (relay.compare(0,3,"ws:") == 0) relay = "http:"+relay.substr(3);
    else if (relay.compare(0,4,"wss:") == 0) relay = "https:"+relay.substr(4);
    else co_return;

    do {
        coroserver::WaitResult r = co_await async.wait_for(std::chrono::seconds(5), nullptr);
        if (r == coroserver::WaitResult::closed) break;

        coroserver::https::Client httpsclient(ctx, _sslctx, "novacisko_nostr_server");
        auto p = co_await httpsclient.open(coroserver::http::Method::GET, relay);
        coroserver::http::ClientRequest req(p);
        coroserver::ws::Stream stream;
        if (co_await coroserver::ws::Client::connect(stream, req)) {
            co_await run_replication(name, std::move(stream));
        }

    } while (true);

}

cocls::suspend_point<bool> ReplicationService::send(coroserver::ws::Stream &stream, const docdb::Structured &msgdata) {
    Event ev = {"EVENT", &msgdata};
    std::string json = ev.to_json(docdb::Structured::flagUTF8);
    return stream.write({json});
}


cocls::future<void> ReplicationService::run_replication(std::string name, coroserver::ws::Stream &&stream) {

    EventSubscriber subscriber(_app->get_publisher());

    auto state = _index.find(name);
    docdb::DocID id = 0;
    if (state) {
        auto [x] = state->get<docdb::DocID>();
        id = x;
    }
    for (const auto &docinfo: _app->get_storage().select_from(id)) {
        co_await send(stream, docinfo->content);
        //TODO goon;
    }


}

}
