#pragma once
#ifndef SRC_NOSTR_SERVER_FOLLOWER_H_
#define SRC_NOSTR_SERVER_FOLLOWER_H_

#include "iapp.h"
#include "signature.h"
#include <docdb/index_view.h>
#include <coroserver/io_context.h>
#include <coroserver/https_client.h>
#include <stop_token>
#include <map>

namespace nostr_server {


class FollowerService {
public:

    static constexpr std::size_t defaul_max_message_size = 65536;

    static cocls::future<void> start(std::stop_token stop, PApp app, coroserver::ContextIO ctx,
            Event::Depth max_depth, unsigned int monitor_timeout_s);



protected:

    FollowerService(PApp app, coroserver::ContextIO ctx, Event::Depth max_depth);



    PApp _app;
    coroserver::ContextIO _ctx;
    Event::Depth _max_depth;

    std::stop_source _central_stop;
    cocls::future<void> _central_task;
    coroserver::ssl::Context _sslctx;
    coroserver::https::Client _httpc;
    SignatureTools _signature_tools;

    std::vector<cocls::async<void> > prepare_tasks(std::stop_token token);

    cocls::future<void> main_task();

    cocls::future<void> start_fetcher(std::string relay, std::stop_token stop);


    cocls::future<std::size_t> get_relay_max_message_size(std::string_view relay);
    static std::string get_relay_url(std::string_view relay_url);

    void process_text_response(std::string_view msg, const std::map<Event::Pubkey, unsigned char> &all_user_map);




};

}


#endif /* SRC_NOSTR_SERVER_FOLLOWER_H_ */
