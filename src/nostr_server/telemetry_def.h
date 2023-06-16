#pragma once
#ifndef SRC_NOSTR_SERVER_TELEMETRY_DEF_H_
#define SRC_NOSTR_SERVER_TELEMETRY_DEF_H_

#include <docdb/database.h>
#include <docdb/storage.h>
#include "../telemetry/sensor.h"

#include "publisher.h"

#include <map>
namespace telemetry {
namespace open_metrics {
class Collector;
}}



namespace nostr_server {

using DatabaseSensor = telemetry::SharedSensor<docdb::PDatabase>;
struct StorageSensor {
    const docdb::Storage<EventType> *storage = nullptr;
};

struct ClientSensor {
    using DefaultLock = std::mutex;
    ClientSensor(std::string ident, std::string user_agent):_connectionID(++connectionIDCounter), _ident(ident), _user_agent(user_agent) {}
    unsigned int _connectionID;
    std::string _ident;
    std::string _user_agent;
    unsigned int command_counter = 0;
    unsigned int query_counter = 0;
    unsigned int error_counter = 0;
    unsigned int subscriptions = 0;
    unsigned int max_subscriptions = 0;
    std::vector<std::pair<unsigned int, unsigned int> >event_created_kinds;
    static std::atomic<unsigned int> connectionIDCounter;
    void report_kind(unsigned int kind) {
        auto iter = std::lower_bound(event_created_kinds.begin(), event_created_kinds.end(), std::pair(kind, 0U));
        if (iter == event_created_kinds.end() || iter->first != kind) {
            event_created_kinds.insert(iter, {kind,1});
        } else {
            iter->second++;
        }
    }
};

struct SharedStats {
    std::atomic<unsigned int> duplicated_post;
};




void register_scavengers(telemetry::open_metrics::Collector &col);
}



#endif /* SRC_NOSTR_SERVER_TELEMETRY_DEF_H_ */
