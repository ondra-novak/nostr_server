/*
 * server.cpp
 *
 *  Created on: 9. 6. 2023
 *      Author: ondra
 */

#include "app.h"
#include "peer.h"

#include <coroserver/http_ws_server.h>

namespace nostr_server {

App::App(const Config &cfg)
        :static_page(cfg.web_document_root, "index.html")
        ,_db(docdb::Database::create(cfg.database_path, cfg.leveldb_options))
        ,_storage(_db,"events")
        ,_index_by_id(_storage,"by_id")
        ,_index_replaceable(_storage, "replaceable")
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



bool IApp::Filter::test(const docdb::Structured &doc) const {
}


std::vector<docdb::DocID> App::find_in_index(const Filter &filter) const {

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
