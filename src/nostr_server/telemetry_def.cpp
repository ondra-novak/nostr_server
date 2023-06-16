#include "telemetry_def.h"
#include "../telemetry/open_metrics/Collector.h"

#include <docdb/storage.h>
#include <iostream>

namespace nostr_server {

std::atomic<unsigned int> ClientSensor::connectionIDCounter = {};
using namespace telemetry;
using namespace telemetry::open_metrics;

template<typename UniqueSensorType>
struct AggregateSensor {
    std::vector<UniqueSensorType> _data;
};

using KindAttrMapItem = std::pair<unsigned int,std::shared_ptr<const AttributeList> > ;



void register_scavengers(telemetry::open_metrics::Collector &col) {
    auto database_collection_size = defMetric(MetricType::gauge,"nostr_database_collection_size","","bytes");
    auto database_level_size = defMetric(MetricType::gauge,"nostr_database_level_size","","bytes");
    auto database_memory_size = defMetric(MetricType::gauge,"nostr_database_memory_usage","","bytes");
    auto database_events = defMetric(MetricType::counter,"nostr_database_events","","");
    auto database_duplicated = defMetric(MetricType::counter,"nostr_database_duplicated_posts","","");
    auto client_info = defMetric(MetricType::info,"nostr_client_info","","");
    auto client_command_counts = defMetric(MetricType::counter,"nostr_client_command_count","","");
    auto client_query_counts = defMetric(MetricType::counter,"nostr_client_query_count","","");
    auto client_error_counts = defMetric(MetricType::counter,"nostr_client_error_count","","");
    auto client_subscripions = defMetric(MetricType::gauge,"nostr_client_active_subscriptions","","");
    auto client_max_subscriptions = defMetric(MetricType::gauge,"nostr_client_max_active_subscriptions","","");
    auto client_event_kind_counts= defMetric(MetricType::counter,"nostr_client_post_events","","");

    col.shared_sensors+=[=](docdb::PDatabase &db){
        return [&](auto emit) {
            auto &ldb = db->get_level_db();
            std::string out;
            std::string line;
            ldb.GetProperty("leveldb.sstables", &out);
            std::istringstream input(out);
            std::size_t prev_level = -1;
            std::size_t prev_level_size = 0;
            std::size_t prev_level_count = 0;
            auto flush_out = [&] {
                if (prev_level != static_cast<std::size_t>(-1)) {
                    auto attr = defAttributes({{"level",prev_level}});
                    emit(database_level_size, attr, prev_level_size);
                }
            };
            while (true) {
                std::getline(input, line);
                if (line.empty()) break;
                if (line.compare(0,10,"--- level ") == 0) {
                    flush_out();
                    prev_level = std::strtoul(line.data()+10,nullptr,10);
                    prev_level_size = 0;
                    prev_level_count = 0;
                } else {
                    auto sep1 = line.find(':');
                    if (sep1 != line.npos) {
                        prev_level_size += std::strtoul(line.data()+sep1+1,nullptr,10);
                    }
                    ++prev_level_count;
                }
            }
            flush_out();

            auto tables = db->list();
            for (const auto &t: tables) {
                auto kid = t.second.first;
                emit(database_collection_size, defAttributes({{"collection",t.first}}),db->get_index_size(docdb::RawKey(kid), docdb::RawKey(kid+1)));
            }
            ldb.GetProperty("leveldb.approximate-memory-usage", &out);
            auto sz = std::strtoull(out.c_str(),nullptr, 10);
            emit(database_memory_size, sz);
        };
    };

    col.shared_sensors+=[=](StorageSensor &s) {
        return [=](auto emit) {
            emit(database_events,s.storage->get_rev());
        };
    };

    col.shared_sensors+=[=](SharedStats &s) {
        return [&](auto emit){
            emit(database_duplicated, s.duplicated_post);
        };
    };

    col.unique_sensors+=[=](ClientSensor &s) {
        return[&,
           attrmap = std::vector<KindAttrMapItem>(),
           basicattr = defAttributes({
            {"connID", s._connectionID}}),
            extattr = defAttributes({
            {"connID", s._connectionID},
            {"userAgent", s._user_agent},
            {"ident", s._ident}})
        ](auto emit) mutable {
            emit(client_info, extattr, nullptr);
            emit(client_command_counts, basicattr, s.command_counter);
            emit(client_query_counts, basicattr, s.query_counter);
            emit(client_subscripions, basicattr, s.subscriptions);
            emit(client_error_counts, basicattr, s.error_counter);
            emit(client_max_subscriptions, basicattr, s.max_subscriptions);
            for (const auto &k: s.event_created_kinds) {
                auto iter = std::lower_bound(attrmap.begin(), attrmap.end(), KindAttrMapItem{k.first,{}},
                        [](const auto &a, const auto &b){return a.first < b.first;});
                if (iter == attrmap.end() || iter->first > k.first) {
                    auto newa = *basicattr;
                    newa.push_back({"kind", k.first});
                    iter = attrmap.insert(iter, {k.first, defAttributes(std::move(newa))});
                }
                emit(client_event_kind_counts, iter->second, k.second);
            }
        };
    };





}



}
