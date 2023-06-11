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
        ,_index_by_id(_storage,"id_hash")
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
void App::IndexByIdHashFn::operator ()(Emit emit, const Event &ev) const {
    std::hash<std::string_view> hasher;
    std::size_t h = hasher(ev["id"].as<std::string_view>());
    emit(h);
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
        //tags are ordered
        //in each group, one item must be found

        const auto &doc_tags = doc["tags"].array();
        if (doc_tags.empty()) return false;
        char ptag = 0;//prev tag
        bool found = true; //true,  if found
        for (const auto &tdef: tags) {
            char t = tdef.first; //get tag
            if (t != ptag) {        //if differ then prev tag
                if (!found) return false;   //nothing found in prev group? failure
                found = false;      //in new group, nothing found yet
                ptag = t;           //set new tag
            }
            if (found) continue;        //already found - continue
            //find tag and value in document tags
            auto iter = std::find_if(doc_tags.begin(), doc_tags.end(), [&](const docdb::Structured &x){
                //tag must be valid
                if (x[0].contains<std::string_view>() && x[1].contains<std::string_view>()) {
                    //get tag name
                    std::string_view tname = x[0].as<std::string_view>();
                    //it must have size 1 character
                    if (tname.size() == 1) {
                        //check tag and value, return true if match
                        return tname[0] == t &&  x[1].as<std::string_view>() == tdef.second;
                    }
                }
                return false;
            });
            //found item
            if (iter != doc_tags.end()) {
                //store found status
                found = true;
            }
        }
        //not found in last group, failure
        if (!found) return false;
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


void merge_ids(std::vector<docdb::DocID> &out,
                      std::vector<docdb::DocID> &a,
                      std::vector<docdb::DocID> &b) {
    std::swap(out, b);
    std::sort(a.begin(), a.end());
    out.clear();
    std::set_union(a.begin(),a.end(),b.begin(), b.end(), std::back_inserter(out));
    a.clear();
}

static void intersection_ids(std::vector<docdb::DocID> &out,
                      std::vector<docdb::DocID> &a,
                      std::vector<docdb::DocID> &b) {
    std::swap(out, b);
    out.clear();
    std::set_intersection(a.begin(),a.end(),b.begin(), b.end(), std::back_inserter(out));
    a.clear();
}


std::vector<docdb::DocID> App::find_in_index(const Filter &filter) const {
    std::vector<docdb::DocID> out;
    std::vector<docdb::DocID> a;
    std::vector<docdb::DocID> b;
    std::vector<docdb::DocID> c;
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
            std::size_t h = hasher(id);
            for (const auto &x: _index_by_id.select(h)) {
                a.push_back(x.id);
            }
            merge_ids(out, a, b);
        }
        return out;
    }
    else if (!filter.authors.empty()) {
        for (const auto &author: filter.authors) {
            std::size_t h = hasher(author);
            docdb::Key from(h);
            docdb::Key to(h);
            append_time(from, to);
            for (const auto &x: _index_pubkey_time.select_between(from,to,docdb::LastRecord::included)) {
                a.push_back(x.id);
            }
            merge_ids(out, a, b);
        }
        return out;
    } else {

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

        char ptag = 0;
        for (const auto &tg: filter.tags) {
            if (ptag != tg.first) {
                if (!ptag) {
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
        if (!ptag) {
            if (merge_result()) return {};
        }

        for (const auto &k: filter.kinds) {
            docdb::Key from(k);
            docdb::Key to(k);
            append_time(from, to);
            all = false;
            for (const auto &x: _index_kind_time.select_between(from,to,docdb::LastRecord::included)) {
                c.push_back(x.id);
            }
        }
        if (merge_result()) return {};
        if (all) {
            for (const auto &x: _index_by_id.select_all()) {
                out.push_back(x.id);
            }
        }
        return out;
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
                char n = t[1];
                const auto &a = v.array();
                for (const auto &c: a)  {
                    out.tags.emplace_back(n,c.as<std::string>());
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
    std::sort(out.tags.begin(),out.tags.end());
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

} /* namespace nostr_server */
