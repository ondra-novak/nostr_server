#include "follower.h"
#include "protocol.h"
#include <docdb/aggregator.h>
#include <docdb/structured_document.h>
#include <docdb/json.h>
#include <coroserver/http_client_request.h>
#include <coroserver/http_ws_client.h>
#include <coroserver/websocket_stream.h>
#include "app.h"
#include "shared/logOutput.h"

using ondra_shared::logProgress;
using ondra_shared::logWarning;

namespace nostr_server {

using namespace coroserver;



FollowerService::FollowerService(PApp app, coroserver::ContextIO ctx, Event::Depth max_depth)
:_app(app)
,_ctx(ctx)
,_max_depth(max_depth)
,_central_task(main_task())
,_sslctx(coroserver::ssl::Context::init_client())
,_httpc(ctx, _sslctx, App::software_url)
{
}

cocls::future<void> FollowerService::start(std::stop_token stp, PApp app, coroserver::ContextIO ctx, Event::Depth max_depth, unsigned int monitor_timeout_s) {
    coroserver::AsyncSupport asyncsup = ctx;

    FollowerService me(app,ctx,max_depth);

    while (!stp.stop_requested()) {
        std::stop_source fetcher_stop;
        auto asyncs = me.prepare_tasks(fetcher_stop.get_token());
        std::vector<std::unique_ptr<cocls::future<void> > > tasks;
        tasks.reserve(asyncs.size());
        std::transform(asyncs.begin(), asyncs.end(), std::back_inserter(tasks), [&](cocls::async<void> &s){
           return new auto(s.start());
        });
        auto f = asyncsup.wait_for(std::chrono::seconds(monitor_timeout_s), &me);
        std::stop_callback cb(stp, [&]{
            asyncsup.cancel_wait(&me);
        });
        coroserver::WaitResult rs = co_await f;
        if (rs != coroserver::WaitResult::timeout) co_return;
        fetcher_stop.request_stop();
        for (auto &x: tasks) {
            co_await *x;
        }
    }

}

std::vector<cocls::async<void> > FollowerService::prepare_tasks(std::stop_token token) {
    auto relays = _app->get_known_relays();
    std::vector<std::string> useful_relays;
    useful_relays.reserve(relays.size());
    bool unknown_users = false;
    for (const auto &x: relays) {
        if (x.first.empty()) unknown_users = true;
        else if (x.second < _max_depth) useful_relays.push_back(x.first);
    }
    if (useful_relays.empty()) return {};



}



static auto aggrFn = [sum = 0](const docdb::Row &) mutable -> int  {
    return sum++;
};


cocls::future<void> FollowerService::main_task() {
    std::stop_token stp =  _central_stop.get_token();
    coroserver::AsyncSupport asyncsup = _ctx;

    while (!stp.stop_requested()) {
        std::stop_source fetcher_stop;
        std::vector<std::unique_ptr<cocls::future<void> > > tasks;
        for (const auto &res: docdb::AggregateBy<std::tuple<std::string> >::Recordset(_idx.select_all(), aggrFn)) {
            tasks.emplace_back(new auto(start_fetcher(std::get<0>(res.key), fetcher_stop.get_token())));
        }
    }

}

std::string FollowerService::get_relay_url(std::string_view relay) {
    if (relay.empty()) return {};
    if (relay.back() == '/') relay = relay.substr(0, relay.size()-1);
    if (relay.compare(0,5,"ws://")) return std::string("http://").append(relay.substr(5));
    if (relay.compare(0,6,"wss://")) return std::string("https://").append(relay.substr(6));
    return {};
}

cocls::future<std::size_t> FollowerService::get_relay_max_message_size(std::string_view relay) {
    try {
        http::ClientRequest req(co_await _httpc.open(coroserver::http::Method::GET, relay));
        req("Accept","application/nostr+json");
        Stream s = co_await req.send();
        std::string data;
        co_await s.read_block(data, 256000);
        auto doc = docdb::Structured::from_json(data);
        auto lim = doc["limitation"];
        if (!lim.contains<docdb::Structured::KeyPairs>()) co_return defaul_max_message_size;
        auto mml = lim["max_message_length"];
        if (!mml.contains<std::size_t>()) co_return defaul_max_message_size;
        co_return mml.as<std::size_t>();
    } catch (...) {
        co_return defaul_max_message_size;;
    }
}

template<typename T>
static std::vector<std::vector<T>> convertTo2DVector(const std::vector<T>& input, size_t max_row_size) {
    std::vector<std::vector<T>> output;

    for (size_t i = 0; i < input.size(); i += max_row_size) {
        output.emplace_back(input.begin() + i, input.begin() + std::min(i + max_row_size, input.size()));
    }

    return output;
}

static void subscribe_users(ws::Stream &stream, const std::vector<Event::Pubkey> &pubkeys,
        std::time_t time, unsigned int max_pubkeys) {
    if (pubkeys.empty()) return;

    docdb::Structured::Array authors;
    std::transform(pubkeys.begin(), pubkeys.end(), std::back_inserter(authors),
            [&](const Event::Pubkey &pk){
        return pk.to_hex();
    });

    auto sets = convertTo2DVector(authors, max_pubkeys);
    int i = 0;
    for (const auto &s: sets) {
        docdb::Structured::KeyPairs filter;
        filter.emplace("authors", s);
        if (time) filter.emplace("since", time);
        std::string id = std::to_string(time)+"_"+std::to_string(i);
        ++i;
        docdb::Structured::Array cmd = {commands[Command::REQ], id, filter};
        stream.write({docdb::Structured(std::move(cmd)).to_json(), ws::Type::text});
    }
 }


cocls::future<void> FollowerService::start_fetcher(std::string relay, std::stop_token stop) {
    try {
        std::string relay_url = get_relay_url(relay);
        std::size_t max_msg_size = co_await get_relay_max_message_size(relay_url);
        std::size_t max_pubkeys = max_msg_size/70;
        if (max_pubkeys <= 3) co_return;
        max_pubkeys -= 3;

        std::map<Event::Pubkey, unsigned char> all_users;
        for (const auto &row: _idx.select(relay)) {
            auto [r, pubkey] = row.key.get<std::string_view, Event::Pubkey>();
            auto [ref_level] = row.value.get<unsigned char>();
            if (ref_level >= _cfg.max_depth) continue;
            all_users.emplace(pubkey, ref_level);
        }

        std::time_t max_timestamp;
        std::vector<Event::Pubkey> new_users, existing_users;
        for (const auto &[pubkey, ref]: all_users) {
            auto rs = _pubkey_idx.select(pubkey, docdb::Direction::backward);
            auto beg = rs.begin();
            auto end = rs.end();
            if (beg == end) new_users.push_back(pubkey);
            else {
                auto [p, t] = beg->value.get<Event::Pubkey, std::time_t>();
                max_timestamp = std::max(max_timestamp, t);
                existing_users.push_back(p);
            }
        }

        http::ClientRequest req(co_await _httpc.open(coroserver::http::Method::GET, relay));
        ws::Stream stream;
        bool res =  co_await coroserver::ws::Client::connect(stream, req);
        if (!res) {
            logWarning("[Follower] Failed to open websocket connection to relay $1 - status $2", relay, req.get_status());
            co_return;
        }
        subscribe_users(stream, new_users, 0, max_pubkeys);
        subscribe_users(stream, existing_users, max_timestamp - 300, max_pubkeys);

        std::stop_callback stpcp(stop, [&]{
            stream.close();
        });

        bool rep = true;
        while (rep) {
            ws::Message msg = co_await stream.read();
            switch (msg.type) {
                case ws::Type::connClose: rep = false; break;
                case ws::Type::text:
                    process_text_response(msg.payload, all_users);
                    break;
                case ws::Type::binary:
                    //todo;
                    break;
                default:
                    break;
            }
        }



    } catch (const std::exception &e) {
        logWarning("[Follower] Exception connecting relay: $1 - $2", relay, e.what());
    }

}

void FollowerService::process_text_response(std::string_view msg,
        const std::map<Event::Pubkey, unsigned char> &all_user_map) {
    auto resp = docdb::Structured::from_json(msg);
    auto cmd = commands[resp[0].as<std::string_view>()];
    if (cmd != Commands::EVENT) return;
    auto event_js = resp[2].as<docdb::Structured::KeyPairs>();
    Event ev = Event::fromJSON(event_js);
    if (!ev.verify(_signature_tools)) {
        logWarning("Signature is not valid $1", ev.id.to_hex());
    }
    auto iter = all_user_map.find(ev.author);
    if (iter == all_user_map.end()) return;
    ev.ref_level = iter->second+1;
    _app->publish(std::move(ev), nullptr);
}

}
