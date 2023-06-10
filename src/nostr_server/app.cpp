/*
 * server.cpp
 *
 *  Created on: 9. 6. 2023
 *      Author: ondra
 */

#include "app.h"

#include <coroserver/http_ws_server.h>

namespace nostr_server {

App::App(const Config &cfg)
        :static_page(cfg.web_document_root, "index.html")
{

}



void App::init_handlers(coroserver::http::Server &server) {
    server.set_handler("/", coroserver::http::Method::GET, [me = shared_from_this()](coroserver::http::ServerRequest &req, std::string_view vpath) -> cocls::future<bool> {
        if (vpath.empty() && req[coroserver::http::strtable::hdr_upgrade] == coroserver::http::strtable::val_websocket) {
            return me->client_main(req);
        } else {
            return me->static_page(req, vpath);
        }
    });
}

cocls::future<bool> App::client_main(coroserver::http::ServerRequest &req) {
    //hold instance
    auto me = shared_from_this();

    coroserver::ws::Stream stream;
    bool res =co_await coroserver::ws::Server::accept(stream, req);
    if (!res) co_return true;

    req.log_message("Connected websocket");
    co_return true;

}


} /* namespace nostr_server */
