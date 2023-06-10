#include <coroserver/http_ws_server.h>

#include "peer.h"

#include <sstream>
namespace nostr_server {

Peer::Peer(coroserver::http::ServerRequest &req, PApp app)
:_req(req)
,_app(std::move(app))
,_subscriber(_app->get_publisher())
{
}

using namespace coroserver::ws;

cocls::future<bool> Peer::client_main(coroserver::http::ServerRequest &req, PApp app) {
    Peer me(req, app);
    bool res =co_await Server::accept(me._stream, me._req);
    if (!res) co_return true;

    me._req.log_message("Connected client");

    auto listener = me.listen_publisher();
    while (true) {
        Message msg = co_await me._stream.read();
        if (msg.type == Type::text) me.processMessage(msg.payload);
        else if (msg.type == Type::connClose) break;
    }
    me._subscriber.kick_me();
    co_await listener;
    co_return true;
}

cocls::future<void> Peer::listen_publisher() {
    bool nx = co_await _subscriber.next();
    while (nx) {
        nx = co_await _subscriber.next();
    }
    co_return;
}

void Peer::processMessage(std::string_view msg_text) {
    _req.log_message([&](auto logger){
        std::ostringstream buff;
        buff << "Received: " << msg_text;
        logger(buff.view());
    });
}

}
