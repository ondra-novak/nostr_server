/*
 * server.cpp
 *
 *  Created on: 9. 6. 2023
 *      Author: ondra
 */

#include "app.h"
#include "peer.h"
#include "nostr_server_version.h"
#include "fulltext.h"
#include <coroserver/http_ws_server.h>
#include <docdb/json.h>
#include <sstream>

namespace nostr_server {

const Event App::supported_nips = {1,9,11,12,16,20,33,42, 45,50};
//to be implemented: 40
const std::string App::software_url = "git+https://github.com/ondra-novak/nostr_server.git";
const std::string App::software_version = PROJECT_NOSTR_SERVER_VERSION;




App::App(const Config &cfg)
        :static_page(cfg.web_document_root, "index.html")
        ,_db(docdb::Database::create(cfg.database_path, cfg.leveldb_options))
        ,_server_desc(cfg.description)
        ,_server_options(cfg.options)
        ,_open_metrics_conf(cfg.metric)
        ,_omcoll(std::make_shared<telemetry::open_metrics::Collector>())
        ,_storage(_db,"events")
        ,_index_by_id(_storage,"ids")
        ,_index_pubkey_time(_storage,"pubkey_hash_time")
        ,_index_replaceable(_storage, "replaceable")
        ,_index_tag_value_time(_storage, "tag_value_time")
        ,_index_kind_time(_storage, "kind_time")
        ,_index_time(_storage, "time")
        ,_index_fulltext(_storage, "fulltext")
{
    _storage.restore_backup(Storage(_db, "events_restore_backup"));
    if (cfg.metric.enable) {
        register_scavengers(*_omcoll);
        _omcoll->make_active();
        _dbsensor.enable(_db);
        _storage_sensor.enable(StorageSensor{&_storage});
    }

}



void App::init_handlers(coroserver::http::Server &server) {
    server.set_handler("/", coroserver::http::Method::GET, [me = shared_from_this()](coroserver::http::ServerRequest &req, std::string_view vpath) -> cocls::future<bool> {
        req.add_header(coroserver::http::strtable::hdr_access_control_allow_origin, "*");
        if (vpath.empty() && req[coroserver::http::strtable::hdr_upgrade] == coroserver::http::strtable::val_websocket) {
            return Peer::client_main(req, me, me->_server_options);
        } else {
         auto ctx = req[coroserver::http::strtable::hdr_accept];
         if (ctx == "application/nostr+json") {
                return me->send_infodoc(req);
            }
            return me->static_page(req, vpath);
        }
    });
    if (_open_metrics_conf.enable) {
        server.set_handler("/metrics", coroserver::http::Method::GET, [&](coroserver::http::ServerRequest &req, std::string_view ) ->cocls::future<bool> {
            if (!_open_metrics_conf.auth.empty()) {
                std::string_view hdr = req[coroserver::http::strtable::hdr_authorization];
                if (hdr.find(_open_metrics_conf.auth) == hdr.npos) {
                    req.set_status(401);
                    return cocls::future<bool>::set_value(true);
                }
            }
            std::ostringstream s(std::ios::binary);
            _omcoll->collect(s);
            s << "# EOF" << "\n";
            req.add_header(coroserver::http::strtable::hdr_content_type, "application/openmetrics-text; version=1.0.0; charset=utf-8");
            return req.send(s);
        });
    }

    server.set_handler("/info", coroserver::http::Method::GET, [me = shared_from_this()](coroserver::http::ServerRequest &req){
        return me->send_infodoc(req);
    });
    server.set_handler("/stats", coroserver::http::Method::GET, [me = shared_from_this()](coroserver::http::ServerRequest &req){
        return me->send_simple_stats(req);
    });
}


template<typename Emit>
void App::IndexByIdFn::operator ()(Emit emit, const Event &ev) const {
    emit(ev["id"].as<std::string_view>(), ev["created_at"].as<std::time_t>());
}

template<typename Emit>
void App::IndexByAuthorKindFn::operator()(Emit emit, const Event &ev) const {
    auto kind = ev["kind"].as<unsigned int>();
    std::string tag;
    bool replacable_1 = (kind == 0) | (kind == 3) | ((kind >= 10000) & (kind < 20000));
    bool replacable_2 = (kind >= 30000) & (kind < 40000);
    bool replacable = replacable_1 | replacable_2;
    if  (replacable_2) {
        auto tags = ev["tags"];
        auto a = tags.array();
        auto iter = std::find_if(a.begin(), a.end(), [](const docdb::Structured &x){
            return x[0].contains<std::string_view>() && x[0].as<std::string_view>() == "d";
        });
        if (iter != a.end()) {
            const auto &d = *iter;
            tag = d[1].to_string();
        }
    }
    if (replacable) {
        auto pubkey = ev["pubkey"].as<std::string_view>();
        auto created_at = ev["created_at"].as<std::time_t>();
        emit(AuthorKindTagKey(pubkey, kind, tag), TimestampRowDef::Type(created_at));
    }
}

template<typename Emit>
void App::IndexByPubkeyHashTimeFn::operator ()(Emit emit, const Event &ev) const {
    std::hash<std::string_view> hasher;
    std::size_t h = hasher(ev["pubkey"].as<std::string_view>());
    std::time_t t = ev["created_at"].as<std::time_t>();
    emit({h,t});
}

template<typename Emit>
void App::IndexTagValueHashTimeFn::operator ()(Emit emit, const Event &ev) const {
    std::hash<std::string_view> hasher;
    std::time_t ct = ev["created_at"].as<std::time_t>();
    const auto &tags = ev["tags"].array();
    for (const auto &tag: tags) {
        std::string_view t = tag[0].as<std::string_view>();
        std::string_view v = tag[1].as<std::string_view>();
        if (t.size() == 1) {
            std::size_t h = hasher(v);
            emit({static_cast<unsigned char>(t[0]), h, ct});
        }
    }
}

template<typename Emit>
void App::IndexKindTimeFn::operator ()(Emit emit, const Event &ev) const {
    std::time_t ct = ev["created_at"].as<std::time_t>();
    unsigned int kind = ev["kind"].as<unsigned int>();
    emit({kind,ct});
}

template<typename Emit>
void App::IndexTimeFn::operator ()(Emit emit, const Event &ev) const {
    std::time_t ct = ev["created_at"].as<std::time_t>();
    emit({ct});
}




docdb::DocID App::doc_to_replace(const Event &event) const {
    IndexByAuthorKindFn idx;
    docdb::DocID to_replace =0;
    idx([&](docdb::Key &&k, TimestampRowDef::Type &&tmrow){
        auto r = _index_replaceable.find(k);
        if (r) {
            auto [tm] = r->value.get();
            auto [new_tm] = tmrow.get();
            to_replace = r->id;
            if (tm > new_tm) to_replace = -1;
        }
    }, event);
    return to_replace;
}

docdb::DocID App::find_replacable(std::string_view pubkey, unsigned int kind, std::string_view category) const {
    auto r = _index_replaceable.find({pubkey,kind,category});
    if (r) {
        return r->id;
    } else {
        return 0;
    }

}

static void append_time(const Filter &f, docdb::Key &from, docdb::Key &to) {
    if (f.since.has_value()) {
        from.append(*f.since);
    } else {
        from.append<std::time_t>(0);
    }

    if (f.until.has_value()) {
        to.append(*f.until);
    } else {
        to.append<std::time_t>(std::numeric_limits<std::time_t>::max());
    }
};


void App::client_counter(int increment)  {
    _clients.fetch_add(increment, std::memory_order_relaxed);
}


static constexpr IApp::OrderingItem unique_key_value_ordering(std::time_t time) {
    return {0,static_cast<unsigned int>(time-946681200)};
}


constexpr auto unique_index_ordering = [](const auto &row) -> IApp::OrderingItem {
    auto [time] = row.value.template get<std::time_t>();
    return unique_key_value_ordering(time);
};

template<typename ... Args>
static constexpr auto multi_index_ordering() {
    return [](const auto &row) -> IApp::OrderingItem {
        auto tup = row.key.template get<Args..., std::time_t>();
        static constexpr auto pos = std::tuple_size_v<decltype(tup)> - 1;
        return unique_key_value_ordering(std::get<pos>(tup));
    };
};


auto fulltext_relevance_ordering(std::string_view word) {
    return [=](const auto &row) -> IApp::OrderingItem {
        auto [rel] = row.value.template get<unsigned char>();
        auto [k] = row.key.template get<std::string_view>();
        rel *= (k.length() - word.length())+1;
        return {256/rel,0};
    };
}

IApp::OrderingItem merge_relevance(const IApp::OrderingItem& a, const IApp::OrderingItem &b) {
    return {std::max(a.first, b.first),std::max(a.second, b.second)};
}


void App::find_in_index(RecordsetCalculator &calc, const std::vector<Filter> &filters) const {
    std::hash<std::string_view> hasher;
    calc.clear();

    docdb::PSnapshot snap = _storage.get_db()->make_snapshot();

    calc.push(calc.empty_set());
    for (const auto &f: filters) {
        calc.push(calc.all_items_set());
        do {
            if (!f.ft_search.empty()) {
                std::vector<WordToken> wt;
                tokenize_text(f.ft_search, wt);
                calc.push(calc.empty_set());
                for (const WordToken &tk: wt) {
                   calc.push(_index_fulltext.select(docdb::prefix(tk.first)),
                           fulltext_relevance_ordering(tk.first));
                   calc.OR(merge_relevance);
                }
                calc.AND(merge_relevance);
            }
            if (calc.is_top_empty()) break;;
            if (!f.ids.empty()) {
               calc.push(calc.empty_set());
               for (const auto &a: f.ids) {
                   auto s = calc.empty_set();
                   if (a.size() == 64) {
                       auto row = _index_by_id.find(a);
                       if (row) {
                           auto [time] = row->value.get<std::time_t>();
                           s.push_back({row->id,unique_key_value_ordering(time)});
                       }
                       calc.push(std::move(s));
                   } else {
                       calc.push(_index_by_id.get_snapshot(snap).select(docdb::prefix(a)),unique_index_ordering);
                   }
                   calc.OR(merge_relevance);
               }
               calc.AND(merge_relevance);
               if (calc.is_top_empty()) break;
            }
            if (!f.authors.empty()) {
                calc.push(calc.empty_set());
                for (const auto &a: f.authors) {
                    std::size_t h = hasher(a);
                    docdb::Key from(h);
                    docdb::Key to(h);
                    append_time(f, from, to);
                    calc.push(_index_pubkey_time.get_snapshot(snap).select_between(from, to),
                            multi_index_ordering<std::size_t>());
                    calc.OR(merge_relevance);
                }
                calc.AND(merge_relevance);
                if (calc.is_top_empty()) break;
            }
            if (!f.tags.empty()) {
                auto iter = f.tags_begin();
                auto end = f.tags_end();
                while (iter != end) {
                    calc.push(calc.empty_set());
                    auto tend = f.tags_next(iter);
                    while (iter != tend) {
                        char t = iter->first;
                        std::string_view val = iter->second;
                        std::size_t h = hasher(val);
                        docdb::Key from(t,h);
                        docdb::Key to(t,h);
                        append_time(f,from, to);
                        calc.push(_index_tag_value_time.get_snapshot(snap).select_between(from, to),
                                multi_index_ordering<unsigned char, std::size_t>());
                        calc.OR(merge_relevance);
                        ++iter;
                    }
                    calc.AND(merge_relevance);
                    if (calc.is_top_empty()) break;
                }
                if (calc.is_top_empty()) break;
            }
            if (!f.kinds.empty()) {
                calc.push(calc.empty_set());
                for (const auto &a: f.kinds) {
                    docdb::Key from(a);
                    docdb::Key to(a);
                    append_time(f, from, to);
                    calc.push(_index_kind_time.get_snapshot(snap).select_between(from, to),
                            multi_index_ordering<unsigned int>());
                    calc.OR(merge_relevance);
                }
                calc.AND(merge_relevance);
            }
            if (calc.top().is_inverted() && (f.since.has_value() || f.until.has_value())) {
                docdb::Key from;
                docdb::Key to;
                append_time(f, from, to);
                calc.push(_index_time.get_snapshot(snap).select_between(from, to),
                        multi_index_ordering<>());
                calc.OR(merge_relevance);
            }
        } while (false);
        calc.OR(merge_relevance);
    }
}



cocls::future<bool> App::send_infodoc(coroserver::http::ServerRequest &req) {
    Event doc = App::get_server_capabilities();
    req.add_header(coroserver::http::strtable::hdr_content_type, "application/nostr+json");
    std::string json = doc.to_json();
    return req.send(std::move(json));
}



template<typename Emit>
inline void App::IndexForFulltextFn::operator ()(Emit emit, const Event &ev) const {
    unsigned int kind = ev["kind"].as<unsigned int>();
    if (kind == 0 || kind == 1 || kind == 2 || kind == 30023) {
        std::vector<WordToken> tokens;
        tokenize_text(ev["content"].as<std::string_view>(), tokens);
        for (const auto &x: tokens) {
            if (x.first.size()>1) {
                emit(x.first, x.second);
            }
        }
    }
}

Event App::get_server_capabilities() const {
    Event limitation = Event::KeyPairs();
    if (_server_options.pow) {
        limitation.set("min_pow_difficulty", _server_options.pow);
    }
    if (_server_options.auth) {
        limitation.set("auth_required", true);
    }
    Event doc {
        {"name", _server_desc.name},
        {"description", _server_desc.desc},
        {"contact", _server_desc.contact},
        {"pubkey", _server_desc.pubkey},
        {"supported_nips", &supported_nips},
        {"software", software_url},
        {"version", software_version},
        {"limitation", limitation}
    };
    return doc;
}

cocls::future<bool> App::send_simple_stats(coroserver::http::ServerRequest &req) {
    std::string out;
    _db->get_level_db().GetProperty("leveldb.approximate-memory-usage", &out);
    Event ev = {
            {"active_connections",_clients.load(std::memory_order_relaxed)},
            {"stored_events", static_cast<std::intmax_t>(_storage.get_rev())},
            {"database_size", static_cast<std::intmax_t>(_db->get_index_size(docdb::RawKey(0), docdb::RawKey(0xFF)))},
            {"memory_usage", static_cast<std::intmax_t>(std::strtoul(out.c_str(), nullptr, 10))}};
    req.add_header(coroserver::http::strtable::hdr_content_type, "application/json");
    std::string json = ev.to_json();
    return req.send(std::move(json));

}

bool App::is_home_user(std::string_view pubkey) const {
    auto fnd = _index_replaceable.find({pubkey, static_cast<unsigned int>(0),std::string_view()});
    return fnd;
}

void App::publish(Event &&event, std::string source)  {
    auto to_replace = doc_to_replace(event);
    if (to_replace != docdb::DocID(-1)) {
        _storage.put(event, to_replace);
    }
    event_publish.publish(EventSource{std::move(event),std::move(source)});
}

docdb::DocID App::find_by_id(std::string_view id) const {
    auto result = _index_by_id.find(id);
    if (result) return result->id;
    else return 0;
}

} /* namespace nostr_server */
