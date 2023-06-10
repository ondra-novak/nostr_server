/*
 * server.cpp
 *
 *  Created on: 9. 6. 2023
 *      Author: ondra
 */

#include "app.h"
#include "peer.h"

#include <coroserver/http_ws_server.h>

namespace nostr_server {

App::App(const Config &cfg)
        :static_page(cfg.web_document_root, "index.html")
{

}



void App::init_handlers(coroserver::http::Server &server) {
    server.set_handler("/", coroserver::http::Method::GET, [me = shared_from_this()](coroserver::http::ServerRequest &req, std::string_view vpath) -> cocls::future<bool> {
        if (vpath.empty() && req[coroserver::http::strtable::hdr_upgrade] == coroserver::http::strtable::val_websocket) {
            return Peer::client_main(req, me);
        } else {
            return me->static_page(req, vpath);
        }
    });
}



} /* namespace nostr_server */
