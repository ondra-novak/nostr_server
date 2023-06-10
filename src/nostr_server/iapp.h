#pragma once
#ifndef SRC_NOSTR_SERVER_IAPP_H_
#define SRC_NOSTR_SERVER_IAPP_H_
#include "publisher.h"


namespace nostr_server {

class IApp {
public:

    virtual ~IApp() = default;
    virtual EventPublisher &get_publisher() = 0;
};

using PApp = std::shared_ptr<IApp>;


}



#endif /* SRC_NOSTR_SERVER_IAPP_H_ */
