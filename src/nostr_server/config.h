#pragma once
#ifndef SRC_NOSTR_SERVER_CONFIG_H_
#define SRC_NOSTR_SERVER_CONFIG_H_
#include <string>

namespace nostr_server {


struct Config {

    std::string listen_addr;
    int threads;
    std::string web_document_root;

};

}




#endif /* SRC_NOSTR_SERVER_CONFIG_H_ */
