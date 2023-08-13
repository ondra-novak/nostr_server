#pragma once
#ifndef SRC_NOSTR_SERVER_FOLLOWER_H_
#define SRC_NOSTR_SERVER_FOLLOWER_H_

#include "config.h"
#include "iapp.h"
#include "signature.h"
#include <docdb/index_view.h>
#include <coroserver/io_context.h>
#include <coroserver/https_client.h>
#include <stop_token>
#include <map>

namespace nostr_server {


enum class RelayPriority {
    unknown = 0,
    mention = 1,
    contact_list = 2,
    old_relay_list = 3,
    relay_list = 4
};

struct RoutingTable {

    static constexpr int max_relays_per_user = 3;

    using UserRelayInfo  = std::pair<std::string_view, RelayPriority>;
    struct UserInfo {
        UserRelayInfo relays[max_relays_per_user];
        unsigned char ref_level = static_cast<unsigned char>(-1);
        void add_relay(const UserRelayInfo nfo, unsigned char ref_level);
    };

    using RelaySet = std::set<std::string, std::less<> >;
    using UserSet = std::map<Event::Pubkey, UserInfo>;

    RelaySet relays;
    UserSet users;


    void build(PApp app);
};


class FollowerService {
public:



    static constexpr std::size_t defaul_max_message_size = 65536;


    FollowerService(PApp app, coroserver::ContextIO ctx, FollowerConfig cfg);


    ~FollowerService();

protected:





    PApp _app;
    coroserver::ContextIO _ctx;
    FollowerConfig _cfg;
    std::stop_source _central_stop;
    cocls::future<void> _central_task;
    coroserver::ssl::Context _sslctx;
    coroserver::https::Client _httpc;
    SignatureTools _signature_tools;

    cocls::future<void> main_task();

    cocls::future<void> start_fetcher(std::string relay, std::stop_token stop);


    cocls::future<std::size_t> get_relay_max_message_size(std::string_view relay);
    static std::string get_relay_url(std::string_view relay_url);

    void process_text_response(std::string_view msg, const std::map<Event::Pubkey, unsigned char> &all_user_map);




};

}


#endif /* SRC_NOSTR_SERVER_FOLLOWER_H_ */
