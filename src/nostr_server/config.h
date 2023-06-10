#pragma once
#ifndef SRC_NOSTR_SERVER_CONFIG_H_
#define SRC_NOSTR_SERVER_CONFIG_H_
#include <string>
#include <leveldb/options.h>

namespace nostr_server {


struct Config {

    std::string listen_addr;
    int threads;
    std::string web_document_root;
    std::string database_path;

    leveldb::Options leveldb_options;

};

}




#endif /* SRC_NOSTR_SERVER_CONFIG_H_ */
