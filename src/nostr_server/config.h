#pragma once
#ifndef SRC_NOSTR_SERVER_CONFIG_H_
#define SRC_NOSTR_SERVER_CONFIG_H_
#include <string>
#include <leveldb/options.h>
#include <optional>
#include <coroserver/ssl_common.h>

namespace nostr_server {

struct ServerDescription {
    std::string name;
    std::string desc;
    std::string pubkey;
    std::string contact;
};

struct FollowerConfig {
    unsigned int max_depth = 0;
    unsigned int max_users_per_connection = 100;
    unsigned int refresh_period_minutes=10;
};

struct ServerOptions {
    int pow = 0; //specifies count of bits for Proof of work (0 - disabled)
    int event_rate_window = 10;
    int event_rate_limit = 10;
    std::size_t attachment_max_size = 1*1024*1024;
    std::size_t max_message_size=64*1024;
    bool read_only;
    bool whitelisting;
    std::string replicators;
    std::string http_header_ident;
};


struct ReplicationTask {
    std::string relay_url;

};

struct OpenMetricConf {
    bool enable;
    std::string auth;
};


using ReplicationConfig = std::vector<ReplicationTask>;


struct RelayBotConfig {
    std::string nsec;
    std::string admin;
    std::string this_relay_url;
    std::string groups;

};

struct Config {

    std::string listen_addr;
    int threads;
    std::string web_document_root;
    std::string database_path;

    std::optional<coroserver::ssl::Certificate> cert;
    std::string ssl_listen_addr;

    leveldb::Options leveldb_options;

    ServerDescription description;

    ServerOptions options;

    std::string private_key;
    ReplicationConfig replication_config;
    OpenMetricConf metric;
    RelayBotConfig botcfg;
    FollowerConfig followercfg;


};

}




#endif /* SRC_NOSTR_SERVER_CONFIG_H_ */
