#pragma once
#ifndef SRC_NOSTR_SERVER_APP_H_
#define SRC_NOSTR_SERVER_APP_H_

#include <memory>

#include <docdb/json.h>
#include <cocls/publisher.h>
#include <coroserver/http_server.h>
#include <coroserver/websocket_stream.h>
#include <coroserver/http_static_page.h>
#include "config.h"

namespace nostr_server {


class App: public std::enable_shared_from_this<App> {
public:

    using Event = docdb::Structured;

    App(const Config &cfg);

    void init_handlers(coroserver::http::Server &server);

protected:
    coroserver::http::StaticPage static_page;


    cocls::publisher<Event> event_publish;

    cocls::future<bool> client_main(coroserver::http::ServerRequest &req);


};

} /* namespace nostr_server */

#endif /* SRC_NOSTR_SERVER_APP_H_ */
