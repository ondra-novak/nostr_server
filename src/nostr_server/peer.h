#pragma once
#ifndef SRC_NOSTR_SERVER_PEER_H_
#define SRC_NOSTR_SERVER_PEER_H_

#include "iapp.h"
#include "signature.h"
#include "config.h"
#include "rate_limiter.h"

#include "telemetry_def.h"
#include "commands.h"

#include "filter.h"
#include <coroserver/websocket_stream.h>
#include <coroserver/http_server_request.h>

#include <array>

namespace nostr_server {


class Peer {
public:

    static cocls::future<bool> client_main(coroserver::http::ServerRequest &req, PApp app, const ServerOptions &options);

    using Subscriptions = std::vector<std::pair<std::string,std::vector<Filter> > >;



protected:
    Peer(coroserver::http::ServerRequest &req, PApp app, const ServerOptions & options,
            std::string user_agent, std::string ident
    );
    ~Peer();



    coroserver::http::ServerRequest &_req;
    PApp _app;
    const ServerOptions & _options;
    EventSubscriber _subscriber;
    coroserver::ws::Stream _stream;
    IApp::RecordsetCalculator _rscalc;
    mutable std::mutex _mx;
    std::optional<SignatureTools> _secp;
    RateLimiter _rate_limiter;
    bool _authent = false;
    bool _hello_recv = false;
    bool _no_limit = false;
    std::string _auth_pubkey;
    Event _client_capabilities;
    std::string _source_ident;


    Subscriptions _subscriptions;

    cocls::future<void> listen_publisher();


    void processMessage(std::string_view msg_text);

    cocls::suspend_point<bool> send(Command command, std::initializer_list<docdb::Structured> args);

    telemetry::UniqueSensor<ClientSensor> _sensor;
    telemetry::SharedSensor<SharedStats> _shared_sensor;


    void on_event(docdb::Structured &msg);
    void on_req(const docdb::Structured &msg);
    void on_count(const docdb::Structured &msg);
    void on_close(const docdb::Structured &msg);

    void event_deletion(Event &&event);

    template<typename Fn>
    void filter_event(const docdb::Structured &doc, Fn fn) const;

    bool check_pow(std::string_view id) const;
    void prepare_auth_challenge();
    void process_auth(const Event &msg);
    bool check_for_auth();
    void send_welcome();

    void send_error(std::string_view id, std::string_view text);
    void send_notice(std::string_view text);
};

}



#endif /* SRC_NOSTR_SERVER_PEER_H_ */
