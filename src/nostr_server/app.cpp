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
        ,_index_fulltext(_storage, "fulltext")
{
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
    emit(ev["id"].as<std::string_view>());
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


int IApp::Filter::tag2bit(char tag) {
    return (tag >= '0' && tag <= '9')*(tag -'0'+1)
         + (tag >= 'A' && tag <= 'Z')*(tag -'A'+11)
         + (tag >= 'a' && tag <= 'z')*(tag -'a'+37) - 1;

}

bool IApp::Filter::test(const docdb::Structured &doc) const {
try {
    if (!authors.empty()) {
        auto t = doc["pubkey"].as<std::string_view>();
        if (std::find_if(authors.begin(), authors.end(), [&](const std::string_view &a){
            return t.compare(0, a.size(), a) == 0;
        }) == authors.end()) {
            return false;
        }
    }
    if (!ids.empty()) {
        if (std::find(ids.begin(), ids.end(), doc["id"].as<std::string_view>()) == ids.end()) {
            return false;
        }
    }
    if (!kinds.empty()) {
        if (std::find(kinds.begin(), kinds.end(), doc["kind"].as<unsigned int>()) == kinds.end()) {
            return false;
        }
    }
    if (!tags.empty()) {
        std::uint64_t checks = 0;
        const auto &doc_tags = doc["tags"].array();
        for (const auto &tag : doc_tags) {
            if (tag[0].contains<std::string_view>() && tag[1].contains<std::string_view>()) {
                std::string_view tagstr = tag[0].as<std::string_view>();
                std::string_view value = tag[1].as<std::string_view>();
                if (tagstr.size() == 1) {
                    char t = tagstr[0];
                    int bit = tag2bit(t);
                    auto mask = std::uint64_t(1) << bit;
                    if ((bit>=0) & ((checks & mask) == 0)) {
                        auto cond = tags.find(t);
                        if (cond == tags.end()) {
                            checks|=mask;
                        } else {
                            auto iter = std::find(cond->second.begin(), cond->second.end(), value);
                            if (iter != cond ->second.end()) checks |= mask;
                        }
                    }
                }
            }
        }
        if ((checks & tag_mask) != tag_mask) return false;
    }
    if (since.has_value()) {
        if (doc["created_at"].as<std::time_t>() < *since) return false;
    }
    if (until.has_value()) {
        if (doc["created_at"].as<std::time_t>() > *until) return false;
    }
    return true;
} catch (...) {
    return false;
}

}


IApp::Filter IApp::Filter::create(const docdb::Structured &f) {
    Filter out;
    const auto &kv = f.keypairs();
    for (const auto &[k, v]: kv) {
        if (k == "authors") {
            const auto &a = v.array();
            for (const auto &c: a)  {
                out.authors.push_back(c.as<std::string>());
            }
        } else if (k == "ids") {
            const auto &a = v.array();
            for (const auto &c: a)  {
                out.ids.push_back(c.as<std::string>());
            }
        } else if (k == "kinds") {
            const auto &a = v.array();
            for (const auto &c: a)  {
                out.kinds.push_back(c.as<unsigned int>());
            }
        } else if (k.substr(0,1) == "#") {
            auto t = k.substr(1);
            if (t.size() == 1) {
                char n = t[0];
                int bit = tag2bit(n);
                if (bit >= 0) {
                    const auto &a = v.array();
                    StringOptions opts;
                    for (const auto &c: a)  {
                        opts.push_back(c.as<std::string>());
                    }
                    out.tags.emplace(n, std::move(opts));
                    out.tag_mask |= std::uint64_t(1) << bit;
                }
            }
        } else if (k == "since") {
            out.since = v.as<std::time_t>();
        } else if (k == "until") {
            out.until = v.as<std::time_t>();
        } else if (k == "limit") {
            out.limit = v.as<unsigned int>();
        } else if (k == "search") {
            out.ft_search = v.as<std::string_view>();
        }
    }
    return out;
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

static void append_time(const IApp::Filter &f, docdb::Key &from, docdb::Key &to) {
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


void App::merge_ft(FTRList &out, FTRList &a, FTRList &tmp) {
    std::swap(out, tmp);
    out.clear();
    auto iter_a = a.begin();
    auto iter_t = tmp.begin();
    auto end_a = a.end();
    auto end_t = tmp.end();
    while (iter_a != end_a && iter_t != end_t) {
        const auto &itm_a = *iter_a;
        const auto &itm_t = *iter_t;
        if (itm_a.first < itm_t.first) {
            out.emplace_back(itm_a.first, itm_a.second+100);
            ++iter_a;
        } else if (itm_a.first > itm_t.first) {
            out.emplace_back(itm_t.first, itm_t.second+100);
            ++iter_t;
        } else {
            out.emplace_back(itm_t.first, itm_t.second+itm_a.second);
            ++iter_a;
            ++iter_t;
        }
    }
    while (iter_a != end_a) {
        const auto &itm_a = *iter_a;
        out.emplace_back(itm_a.first, itm_a.second+100);
        ++iter_a;
    }
    while (iter_t != end_t) {
        const auto &itm_t = *iter_t;
        out.emplace_back(itm_t.first, itm_t.second+100);
        ++iter_t;
    }

}

void App::client_counter(int increment)  {
    _clients.fetch_add(increment, std::memory_order_relaxed);
}

void App::dedup_ft(FTRList &x) {
    if (x.empty()) return;
    auto i = x.begin();
    auto j = i;
    ++i;
    auto e = std::remove_if(i,x.end(),[&](const FulltextRelevance &rl) {
        if (rl.first == j->first) {
            j->second = std::min(j->second, rl.second);
            return true;
        } else {
            return false;
        }
    });
    x.erase(e, x.end());
}

bool App::find_in_index(docdb::RecordSetCalculator &calc, const std::vector<Filter> &filters, FTRList &&relevance) const {
    calc.clear();
    relevance.clear();
    std::hash<std::string_view> hasher;
    std::vector<FulltextRelevance> ftr2,ftr3;

    docdb::PSnapshot snap = _storage.get_db()->make_snapshot();

    bool b = false;
    for (const auto &f: filters) {
        bool r = false;
        if (!f.ft_search.empty()) {
            std::vector<WordToken> wt;
            tokenize_text(f.ft_search, wt);
            if (!wt.empty()) {
               for (const WordToken &tk: wt) {
                   for (const auto &row : _index_fulltext.select(docdb::prefix(tk.first))) {
                       auto [rel] = row.value.get<unsigned char>();
                       auto [k] = row.key.get<std::string_view>();
                       rel *= (k.length() - tk.first.length())+1;
                       ftr2.push_back({row.id, rel});
                   }
                   std::sort(ftr2.begin(), ftr2.end());
                   dedup_ft(ftr2);
                   merge_ft(relevance, ftr2, ftr3);
               }
               auto s = calc.get_empty_set();
               std::transform(relevance.begin(), relevance.end(), std::back_inserter(s),[&](const auto &x) {
                   return x.first;
               });
               calc.push(std::move(s));
               calc.AND(r);
            }
        }
        if (!f.ids.empty()) {
           auto s = calc.get_empty_set();
           for (const auto &a: f.ids) {
               if (a.size() == 64) {
                   auto row = _index_by_id.find(a);
                   if (row) s.push_back(row->id);
               } else {
                   calc.push(_index_by_id.select(docdb::prefix(a)));
               }
           }
           calc.push(std::move(s));
           calc.AND(r);
           if (calc.top_is_empty()) continue;
        }
        if (!f.authors.empty()) {
            bool rr = false;
            for (const auto &a: f.authors) {
                std::size_t h = hasher(a);
                docdb::Key from(h);
                docdb::Key to(h);
                append_time(f, from, to);
                calc.push(_index_pubkey_time.get_snapshot(snap).select_between(from, to));
                calc.OR(rr);
            }
            calc.AND(r);
            if (calc.top_is_empty()) continue;
        }
        if (!f.tags.empty()) {
            for (const auto &[tag, values]: f.tags) {
                bool rr = false;
                for (const auto &v: values) {
                    unsigned char t = tag;
                    std::size_t h = hasher(v);
                    docdb::Key from(t,h);
                    docdb::Key to(t,h);
                    append_time(f,from, to);
                    calc.push(_index_tag_value_time.select_between(from, to));
                    calc.OR(rr);
                }
                calc.AND(r);
                if (calc.top_is_empty()) break;
            }
            if (calc.top_is_empty()) continue;
        }
        if (!f.kinds.empty()) {
            bool rr = false;
            for (const auto &a: f.kinds) {
                docdb::Key from(a);
                docdb::Key to(a);
                append_time(f, from, to);
                calc.push(_index_kind_time.select_between(from, to));
                calc.OR(rr);
            }
            calc.AND(r);
        }
        if (r) {
            calc.OR(b);
        }
    }
    return b;

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

} /* namespace nostr_server */
