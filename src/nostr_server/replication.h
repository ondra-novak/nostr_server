#pragma once
#ifndef SRC_NOSTR_SERVER_REPLICATION_H_
#define SRC_NOSTR_SERVER_REPLICATION_H_

#include "config.h"
#include "iapp.h"

#include <docdb/database.h>
#include <docdb/map.h>
#include <coroserver/io_context.h>
#include <coroserver/ssl_common.h>
#include <coroserver/websocket_stream.h>

#include "signature.h"
namespace nostr_server {

class ReplicationService {
public:

    static cocls::future<void> start(coroserver::ContextIO ctx, PApp app, ReplicationConfig &&cfg);




protected:

    ReplicationService(PApp app, ReplicationConfig &&cfg);

    using Index = docdb::Map<docdb::RowDocument>;

    PApp _app;
    ReplicationConfig _cfg;
    Index _index;
    coroserver::ssl::Context _sslctx;
    SignatureTools::PrivateKey _pk;

    using TasksFutures = std::vector<std::unique_ptr<cocls::future<void > > >;

    cocls::future<void> start_outbound_task(coroserver::ContextIO ctx, const ReplicationTask &cfg);
    cocls::future<void> run_outbound(const ReplicationTask &cfg, coroserver::ws::Stream stream);
    cocls::future<void> run_outbound_recv(const ReplicationTask &cfg, coroserver::ws::Stream stream);

    TasksFutures init_tasks(coroserver::ContextIO ctx);



};



}




#endif /* SRC_NOSTR_SERVER_REPLICATION_H_ */
