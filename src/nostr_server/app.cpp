/*
 * server.cpp
 *
 *  Created on: 9. 6. 2023
 *      Author: ondra
 */

#include "app.h"
#include "peer.h"

#include <coroserver/http_ws_server.h>


#include <bitset>
namespace nostr_server {

App::App(const Config &cfg)
        :static_page(cfg.web_document_root, "index.html")
        ,_db(docdb::Database::create(cfg.database_path, cfg.leveldb_options))
        ,_storage(_db,"events")
        ,_index_by_id(_storage,"ids")
        ,_index_pubkey_time(_storage,"pubkey_hash_time")
        ,_index_replaceable(_storage, "replaceable")
        ,_index_tag_value_time(_storage, "tag_value_time")
        ,_index_kind_time(_storage, "kind_time")
{

}



void App::init_handlers(coroserver::http::Server &server) {
    server.set_handler("/", coroserver::http::Method::GET, [me = shared_from_this()](coroserver::http::ServerRequest &req, std::string_view vpath) -> cocls::future<bool> {
        if (vpath.empty() && req[coroserver::http::strtable::hdr_upgrade] == coroserver::http::strtable::val_websocket) {
            return Peer::client_main(req, me);
        } else {
            return me->static_page(req, vpath);
        }
    });
}


template<typename Emit>
void App::IndexByIdFn::operator ()(Emit emit, const Event &ev) const {
    emit(ev["id"].as<std::string_view>());
}

template<typename Emit>
void App::IndexByAuthorKindFn::operator()(Emit emit, const Event &ev) const {
    auto kind = ev["kind"].as<unsigned int>();
    std::string_view pubkey;
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


static int tag2bit(char tag) {
    return (tag >= '0' && tag <= '9')*(tag -'0'+1)
         + (tag >= 'A' && tag <= 'Z')*(tag -'A'+11)
         + (tag >= 'a' && tag <= 'z')*(tag -'a'+37) - 1;

}

bool IApp::Filter::test(const docdb::Structured &doc) const {
try {
    if (!authors.empty()) {
        if (std::find(authors.begin(), authors.end(), doc["pubkey"].as<std::string_view>()) == authors.end()) {
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


void IApp::merge_ids(DocIDList &out, DocIDList &a, DocIDList &b) {
    std::swap(out, b);
    std::sort(a.begin(), a.end());
    out.clear();
    std::set_union(a.begin(),a.end(),b.begin(), b.end(), std::back_inserter(out));
    a.clear();
}

void IApp::intersection_ids(DocIDList &out, DocIDList &a, DocIDList &b) {
    std::swap(out, b);
    out.clear();
    std::set_intersection(a.begin(),a.end(),b.begin(), b.end(), std::back_inserter(out));
    a.clear();
}


#if 0
bool App::find_in_index(const Filter &filter, DocIDList &result) const {
    DocIDList out;
    DocIDList a;
    DocIDList b;
    DocIDList c;
    std::hash<std::string_view> hasher;
    auto append_time = [&](docdb::Key &from, docdb::Key &to) {
        if (filter.since.has_value()) {
            from.append(*filter.since);
        } else {
            from.append<std::time_t>(0);
        }

        if (filter.until.has_value()) {
            to.append(*filter.until);
        } else {
            to.append<std::time_t>(std::numeric_limits<std::time_t>::max());
        }
    };
    if (!filter.ids.empty()) {
        for (const auto &id: filter.ids) {
            for (const auto &x: _index_by_id.select(id)) {
                a.push_back(x.id);
            }
            merge_ids(out, a, b);
        }
        result = std::move(out);
        return false;
    }
    else {

        bool all = true;
        auto merge_result = [&] {
            if (all) {
                std::swap(out,c);
                all = false;
            } else {
                intersection_ids(out, c, b);
            }
            return (out.empty());
        };

        if (!filter.tags.empty()) {
            char ptag = 0;
            for (const auto &tg: filter.tags) {
                if (ptag != tg.first) {
                    if (ptag) {
                        if (merge_result()) return {};
                    }
                    ptag = tg.first;
                }
                auto h = hasher(tg.second);
                docdb::Key from(static_cast<unsigned char>(tg.first), h);
                docdb::Key to(static_cast<unsigned char>(tg.first), h);
                append_time(from, to);
                for (const auto &x: _index_tag_value_time.select_between(from,to,docdb::LastRecord::included)) {
                    a.push_back(x.id);
                }
                merge_ids(c, a, b);
            }
            if (merge_result()) return {};
        }

        if (!filter.authors.empty()) {
            for (const auto &author: filter.authors) {
                std::size_t h = hasher(author);
                docdb::Key from(h);
                docdb::Key to(h);
                append_time(from, to);
                for (const auto &x: _index_pubkey_time.select_between(from,to,docdb::LastRecord::included)) {
                    a.push_back(x.id);
                }
                merge_ids(c, a, b);
            }
            if (merge_result()) return {};
        }

        if (!filter.kinds.empty()) {
            for (const auto &k: filter.kinds) {
                docdb::Key from(k);
                docdb::Key to(k);
                append_time(from, to);
                for (const auto &x: _index_kind_time.select_between(from,to,docdb::LastRecord::included)) {
                    a.push_back(x.id);
                }
                merge_ids(c, a, b);
            }
            if (merge_result()) return {};
        }
        if (all) return true;
        result = std::move(out);
        return false;
    }

}
#endif
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


bool App::find_in_index(docdb::RecordSetCalculator &calc, const std::vector<Filter> &filters) const {
    calc.clear();
    std::hash<std::string_view> hasher;

    docdb::PSnapshot snap = _storage.get_db()->make_snapshot();

    bool b = false;
    for (const auto &f: filters) {
        bool r = false;
        if (!f.ids.empty()) {
           auto s = calc.get_empty_set();
           for (const auto &a: f.ids) {
               auto row = _index_by_id.find(a);
               if (row) s.push_back(row->id);
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
                    std::size_t h = hasher(v);
                    docdb::Key from(h);
                    docdb::Key to(h);
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
        calc.OR(b);
    }
    return b;

}

} /* namespace nostr_server */
