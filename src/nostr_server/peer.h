#pragma once
#ifndef SRC_NOSTR_SERVER_PEER_H_
#define SRC_NOSTR_SERVER_PEER_H_

#include "iapp.h"

#include <coroserver/websocket_stream.h>
#include <coroserver/http_server_request.h>

#include <array>

namespace nostr_server {


class Peer {
public:

    static cocls::future<bool> client_main(coroserver::http::ServerRequest &req, PApp app);

    using Subscriptions = std::vector<std::pair<std::string,std::vector<IApp::Filter> > >;

protected:
    Peer(coroserver::http::ServerRequest &req, PApp app);

    coroserver::http::ServerRequest &_req;
    PApp _app;
    EventSubscriber _subscriber;
    coroserver::ws::Stream _stream;
    docdb::RecordSetCalculator _rscalc;
    mutable std::mutex _mx;


    Subscriptions _subscriptions;

    cocls::future<void> listen_publisher();


    void processMessage(std::string_view msg_text);

    void send(const docdb::Structured &msgdata);

    std::string _log_buffer;


    void on_event(docdb::Structured &msg);
    void on_req(const docdb::Structured &msg);
    void on_count(const docdb::Structured &msg);
    void on_close(const docdb::Structured &msg);

    void event_deletion(Event &&event);

    template<typename Fn>
    void filter_event(const docdb::Structured &doc, Fn fn) const;



};

}



#endif /* SRC_NOSTR_SERVER_PEER_H_ */
