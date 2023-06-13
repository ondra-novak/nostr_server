#pragma once
#ifndef SRC_NOSTR_SERVER_REPLICATION_H_
#define SRC_NOSTR_SERVER_REPLICATION_H_

#include "iapp.h"

#include <docdb/database.h>
#include <docdb/map.h>
#include <coroserver/io_context.h>
#include <coroserver/ssl_common.h>
#include <coroserver/websocket_stream.h>

namespace nostr_server {

struct ReplicationTask {
    std::string task_name;
    std::string relay_url;
};


using ReplicationConfig = std::vector<ReplicationTask>;

class ReplicationService {
public:
    ReplicationService(PApp app, ReplicationConfig &&cfg);

    cocls::future<void> start(coroserver::ContextIO ctx);




protected:

    using Index = docdb::Map<docdb::RowDocument>;

    PApp _app;
    ReplicationConfig _cfg;
    Index _index;
    coroserver::ssl::Context _sslctx;

    cocls::future<void> start_replication_task(coroserver::ContextIO ctx, std::string name, std::string relay);
    cocls::future<void> run_replication(std::string name, coroserver::ws::Stream &&stream);
    static cocls::suspend_point<bool> send(coroserver::ws::Stream &stream, const docdb::Structured &msgdata);
};



}




#endif /* SRC_NOSTR_SERVER_REPLICATION_H_ */
