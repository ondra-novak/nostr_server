#pragma once
#ifndef SRC_NOSTR_SERVER_PEER_H_
#define SRC_NOSTR_SERVER_PEER_H_

#include "iapp.h"

#include <coroserver/websocket_stream.h>
#include <coroserver/http_server_request.h>


namespace nostr_server {


class Peer {
public:

    static cocls::future<bool> client_main(coroserver::http::ServerRequest &req, PApp app);

protected:
    Peer(coroserver::http::ServerRequest &req, PApp app);

    coroserver::http::ServerRequest &_req;
    PApp _app;
    EventSubscriber _subscriber;
    coroserver::ws::Stream _stream;


    cocls::future<void> listen_publisher();


    void processMessage(std::string_view msg_text);

    std::string _log_buffer;

};

}



#endif /* SRC_NOSTR_SERVER_PEER_H_ */
