#pragma once
#ifndef SRC_NOSTR_SERVER_PEER_H_
#define SRC_NOSTR_SERVER_PEER_H_

#include "iapp.h"
#include "signature.h"
#include "config.h"
#include "rate_limiter.h"

#include "telemetry_def.h"

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
    IApp::RecordSetCalculator _rscalc;
    mutable std::mutex _mx;
    std::optional<SignatureTools> _secp;
    RateLimiter _rate_limiter;
    bool _authent = false;
    bool _no_limit = false;
    Event::Pubkey _auth_pubkey;
    std::string _auth_nonce;
    JSON _client_capabilities;


    Subscriptions _subscriptions;

    cocls::future<void> listen_publisher();


    void processMessage(std::string_view msg_text);

    cocls::suspend_point<bool> send(const docdb::Structured &msgdata);

    telemetry::UniqueSensor<ClientSensor> _sensor;
    telemetry::SharedSensor<SharedStats> _shared_sensor;


    void on_event(const JSON &msg);
    void on_req(const JSON &msg);
    void on_count(const JSON &msg);
    void on_close(const JSON &msg);

    void event_deletion(const Event &event);

    template<typename Fn>
    void filter_event(const Event &doc, Fn fn) const;

    bool check_pow(std::string_view id) const;
    void prepare_auth_challenge();
    void process_auth(const JSON &jmsg);
    void send_error(std::string_view id, std::string_view text);
    void send_notice(std::string_view text);
};

}



#endif /* SRC_NOSTR_SERVER_PEER_H_ */
