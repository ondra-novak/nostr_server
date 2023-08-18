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
#include "kinds.h"

#include "whitelist_impl.h"
#include "routing_imp.h"

#include <coroserver/http_ws_server.h>
#include <coroserver/strutils.h>
#include <docdb/json.h>
#include <docdb/aggregator.h>
#include <sstream>
#include <shared/logOutput.h>

using docdb::AggregateBy;


namespace nostr_server {

const docdb::Structured App::supported_nips = {1,5,9,11,12,16,20,24,33,42, 45,50,97};
//to be implemented: 40
const std::string App::software_url = "git+https://github.com/ondra-novak/nostr_server.git";
const std::string App::software_version = PROJECT_NOSTR_SERVER_VERSION;





App::App(const Config &cfg)
        :static_page(cfg.web_document_root, "index.html")
        ,_db(docdb::Database::create(cfg.database_path, cfg.leveldb_options))
        ,_server_desc(cfg.description)
        ,_server_options(cfg.options)
        ,_followerConfig(cfg.followercfg)
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
        ,_index_whitelist(_storage, "karma")
        ,_index_attachments(_storage,"attachments")
        ,_index_routing(_storage, "routing")
        ,_index_nip05(_storage, "nip05")
        ,_gc_is_clear(std::make_shared<std::atomic_flag>())
{
    _storage.register_transaction_observer(autocompact());
    if (cfg.metric.enable) {
        register_scavengers(*_omcoll);
        _omcoll->make_active();
        _dbsensor.enable(_db);
        _storage_sensor.enable(StorageSensor{&_storage});
    }
    _empty_database = _index_whitelist.select_all().empty();
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
    server.set_handler("/file/", coroserver::http::Method::GET, [me = shared_from_this()](coroserver::http::ServerRequest &req, std::string_view vpath)->cocls::future<bool>{
        if (req[coroserver::http::strtable::hdr_etag].has_value()) {
            req.set_status(304);
            co_await req.send("");
            co_return true;
        }
        Event::ID id;
        coroserver::base64::decode(vpath, [&, pos = 0U](unsigned char c) mutable {
            if (pos < id.size()) id[pos++] = c;
        }, coroserver::base64::Table::get_base64url_table());
        docdb::DocID docid = me->find_event_by_id(id);
        if (docid) {
            auto evatt = me->_storage.find(docid);
            if (evatt && std::holds_alternative<Event>(evatt->document)) {
                const Event &evv = std::get<Event>(evatt->document);
                std::string mime = evv.get_tag_content("m");
                Attachment::ID hash = get_attachment_id(evv);
                auto attid = me->find_attachment(hash);
                if (attid) {
                    auto evatt2 = me->_storage.find(attid);
                    if (evatt2 && std::holds_alternative<Attachment>(evatt2->document)) {
                        const Attachment &att = std::get<Attachment>(evatt2->document);
                        req.add_header(coroserver::http::strtable::hdr_content_type,mime);
                        req.add_header(coroserver::http::strtable::hdr_etag, vpath);
                        req.caching(24*60*60*365);
                        co_await req.send(att.data);
                        co_return true;
                    }
                }

            }
        }
        co_return false;
    });
    server.set_handler("/routing", coroserver::http::Method::GET, [me=shared_from_this()](coroserver::http::ServerRequest &req, std::string_view vpath) -> cocls::future<bool>{
        std::ostringstream out;
        if (vpath.empty()) {
            auto relays = me->get_known_relays();
            for (const auto &[r,d] : relays) {
                out << static_cast<int>(d) << "\t" << r << std::endl;
            }
        } else if (vpath[0] == '/'){
            vpath = vpath.substr(1);
            std::string relay;
            coroserver::url::decode(vpath, [&](char c){relay.push_back(c);});
            auto users = me->get_users_on_relay(relay);
            for (const auto &[r,d] : users) {
                out << static_cast<int>(d) << "\t" << r.to_hex() << std::endl;
            }
        }
        req.content_type(coroserver::http::ContentType::text_plain_utf8);
        return req.send(out);
    });
    server.set_handler("/.well-known/nostr.json", coroserver::http::Method::GET, [me=shared_from_this()](coroserver::http::ServerRequest &req, std::string_view vpath) -> cocls::future<bool>{
        return me->process_nip05_request(req, vpath);
    });
}


template<typename Emit>
void App::IndexByIdFn::operator ()(Emit emit, const EventOrAttachment &evatt) const {
    if (!std::holds_alternative<Event>(evatt)) return;
    const Event &ev = std::get<Event>(evatt);

    emit(ev.id,ev.created_at);
//    emit(ev["id"].as<std::string_view>(), ev["created_at"].as<std::time_t>());
}

template<typename Emit>
void App::IndexByAuthorKindFn::operator()(Emit emit, const EventOrAttachment &evatt) const {
    if (!std::holds_alternative<Event>(evatt)) return;
    const Event &ev = std::get<Event>(evatt);
    std::string tag;
    bool replacable_1 = (ev.kind == kind::Metadata) | (ev.kind == kind::Contacts)
                       | ((ev.kind >= kind::Replaceable_Begin) & (ev.kind < kind::Replaceable_End));
    bool replacable_2 = (ev.kind >= kind::Parameterized_Replaceable_Begin) & (ev.kind < kind::Parameterized_Replaceable_End);
    bool replacable = replacable_1 | replacable_2;
    if  (replacable_2) {
        tag = ev.get_tag_content("d");
    }
    if (replacable) {
        emit(AuthorKindTagKey(ev.author, ev.kind, tag), ev.created_at);
    }
}

template<typename Emit>
void App::IndexByPubkeyHashTimeFn::operator ()(Emit emit, const EventOrAttachment &evatt) const {
    if (!std::holds_alternative<Event>(evatt)) return;
    const Event &ev = std::get<Event>(evatt);
    emit({ev.author, ev.created_at});
}

template<typename Emit>
void App::IndexTagValueHashTimeFn::operator ()(Emit emit, const EventOrAttachment &evatt) const {
    if (!std::holds_alternative<Event>(evatt)) return;
    const Event &ev = std::get<Event>(evatt);
    std::hash<std::string_view> hasher;
    for (const auto &t: ev.tags) {
        if (t.tag.size() == 1) {
            std::size_t h = hasher(t.content);
            emit({t.tag[0],h, ev.created_at});
        }
    }
}

template<typename Emit>
void App::IndexKindTimeFn::operator ()(Emit emit, const EventOrAttachment &evatt) const {
    if (!std::holds_alternative<Event>(evatt)) return;
    const Event &ev = std::get<Event>(evatt);
    emit({ev.kind,ev.created_at});
}

template<typename Emit>
void App::IndexTimeFn::operator ()(Emit emit, const EventOrAttachment &evatt) const {
    if (!std::holds_alternative<Event>(evatt)) return;
    const Event &ev = std::get<Event>(evatt);
    emit(ev.created_at);
}


template<typename Emit>
void App::IndexAttachmentFn::operator()(Emit emit, const EventOrAttachment &evatt) const {
    if (std::holds_alternative<Event>(evatt)) {
        const Event &ev = std::get<Event>(evatt);
        if (ev.kind == kind::File_Header && ev.find_indexed_tag('f', "file")) {
            Attachment::ID attid = Attachment::ID::from_hex(ev.get_tag_content("x"));
            emit({attid, emit.id()},true);
        }
    } else if (std::holds_alternative<Attachment>(evatt)) {
        const Attachment &att = std::get<Attachment>(evatt);
        emit(att.id, false);
    }
}

template<typename Emit>
void App::IndexNip05Fn::operator()(Emit emit, const EventOrAttachment &evatt) const {
    if (!std::holds_alternative<Event>(evatt)) return;
    const Event &ev = std::get<Event>(evatt);
    if (ev.kind == kind::Metadata) {
        try {
            auto ctx = docdb::Structured::from_json(ev.content);
            std::string_view name = ctx["nip05"].as<std::string_view>();
            if (name.find('@') != name.npos) {
                emit(name);
            }
        } catch (...) {

        }
    }
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

bool App::check_whitelist(const Event::Pubkey &k) const
{
    if (_empty_database) {
        _empty_database = _index_whitelist.select_all().empty();
        if (_empty_database) return true;
    }
    auto r = _index_whitelist.find(k);
    if (!r) return false;
    return r->get_score() > 0;
}

int App::get_karma(const Event::Pubkey &k) const
{
    auto r = _index_whitelist.find(k);
    if (!r) return 0;
    return r->get_score();
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


void App::client_counter(int increment, std::string_view url)  {
    std::lock_guard _(_app_share);
    _clients+=increment;
    auto iter = _this_relay_url.find(url);
    if (iter == _this_relay_url.end())  {
        _this_relay_url.emplace(std::string(url));
    }
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

template<typename Index, typename Def>
static auto searchShortenID(const Index &idx, const Def &def) {
    auto from = def.first;
    auto to = def.first;
    for (unsigned int i = def.second; i < def.first.size(); ++i) {
        from[i] = 0;
        to[i] = 0xFF;
    }
    return idx.select_between(from,to);
}

void App::find_in_index(RecordSetCalculator &calc, const std::vector<Filter> &filters) const {
    std::hash<std::string_view> hasher;
    calc.clear();

    docdb::PSnapshot snap = _storage.get_db()->make_snapshot();

    calc.push(calc.empty_set());
    for (const auto &f: filters) {
        bool need_time = true;
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
                   if (a.second != a.first.size()) {
                        calc.push(searchShortenID(_index_by_id.get_snapshot(snap), a),
                            unique_index_ordering);
                        calc.OR(merge_relevance);
                   } else {
                        auto s = calc.empty_set();
                        auto row = _index_by_id.find(a.first);
                        if (row) {
                            auto [time] = row->value.get<std::time_t>();
                            s.push_back({row->id,unique_key_value_ordering(time)});
                        }
                        calc.push(std::move(s));
                        calc.OR(merge_relevance);
                   }
               }
               calc.AND(merge_relevance);
               if (calc.is_top_empty()) break;
            }
            if (!f.authors.empty()) {
                calc.push(calc.empty_set());
                for (const auto &a: f.authors) {
                    if (a.second != a.first.size()) {
                        calc.push(searchShortenID(_index_pubkey_time.get_snapshot(snap), a),
                            multi_index_ordering<Event::Pubkey>());
                        calc.OR(merge_relevance);
                    } else {
                        docdb::Key from(a.first);
                        docdb::Key to(a.first);
                        append_time(f, from, to);
                        need_time = false;
                        calc.push(_index_pubkey_time.get_snapshot(snap).select_between(from, to),
                                multi_index_ordering<std::size_t>());
                        calc.OR(merge_relevance);
                    }
                }
                calc.AND(merge_relevance);
                if (calc.is_top_empty()) break;
            }
            for(const auto &[t, contents]:f.tags) {
                calc.push(calc.empty_set());
                for (const auto &x: contents) {
                    std::size_t h = hasher(x);
                    docdb::Key from(t,h);
                    docdb::Key to(t,h);
                    append_time(f,from, to);
                    need_time = false;
                    calc.push(_index_tag_value_time.get_snapshot(snap).select_between(from, to),
                            multi_index_ordering<unsigned char, std::size_t>());
                    calc.OR(merge_relevance);
                }
                calc.AND(merge_relevance);
            }
            if (!f.kinds.empty()) {
                calc.push(calc.empty_set());
                for (const auto &a: f.kinds) {
                    docdb::Key from(a);
                    docdb::Key to(a);
                    append_time(f, from, to);
                    need_time = false;
                    calc.push(_index_kind_time.get_snapshot(snap).select_between(from, to),
                            multi_index_ordering<unsigned int>());
                    calc.OR(merge_relevance);
                }
                calc.AND(merge_relevance);
            }
            if (need_time && (f.since.has_value() || f.until.has_value())) {
                docdb::Key from;
                docdb::Key to;
                append_time(f, from, to);
                calc.push(_index_time.get_snapshot(snap).select_between(from, to),
                        multi_index_ordering<>());
                calc.AND(merge_relevance);
            }
        } while (false);
        calc.OR(merge_relevance);
    }
}



cocls::future<bool> App::send_infodoc(coroserver::http::ServerRequest &req) {
    JSON doc = App::get_server_capabilities();
    req.add_header(coroserver::http::strtable::hdr_content_type, "application/nostr+json");
    std::string json = doc.to_json();
    return req.send(std::move(json));
}



template<typename Emit>
inline void App::IndexForFulltextFn::operator ()(Emit emit, const EventOrAttachment &evatt) const {

    if (!std::holds_alternative<Event>(evatt)) return;
    const Event &ev = std::get<Event>(evatt);

    if (ev.kind == kind::Metadata || ev.kind == kind::Short_Text_Note
          || ev.kind == kind::Recommend_Relay || ev.kind == kind::Long_form_Content) {
        std::vector<WordToken> tokens;
        tokenize_text(ev.content, tokens);
        for (const auto &x: tokens) {
            if (x.first.size()>1) {
                emit(x.first, x.second);
            }
        }
    }
    for (const Event::Tag &t: ev.tags) {
        if (t.tag == "description" || t.tag == "name" || t.tag == "subject" || t.tag == "summary" || t.tag == "title") {
            std::vector<WordToken> tokens;
            tokenize_text(t.content, tokens);
            for (const auto &x: tokens) {
                if (x.first.size()>1) {
                    emit(x.first, x.second);
                }
            }
        }
    }
}

JSON App::get_server_capabilities() const {
    JSON limitation = JSON::KeyPairs();
    if (_server_options.pow) {
        limitation.set("min_pow_difficulty", _server_options.pow);
    }
    limitation.set("min_prefix",32);
    limitation.set("max_message_length", static_cast<std::intmax_t>(_server_options.max_message_size));
    limitation.set("max_file_size",static_cast<std::intmax_t>(_server_options.attachment_max_size));
    JSON doc = {
        {"name", _server_desc.name},
        {"description", std::string_view(_server_desc.desc)},
        {"contact", std::string_view(_server_desc.contact)},
        {"pubkey", std::string_view(_server_desc.pubkey)},
        {"supported_nips", &supported_nips},
        {"software", std::string_view(software_url)},
        {"version", std::string_view(software_version)},
        {"limitation", limitation}
    };
    return doc;
}

cocls::future<bool> App::send_simple_stats(coroserver::http::ServerRequest &req) {
    std::lock_guard lk(_app_share);
    std::string out;
    _db->get_level_db().GetProperty("leveldb.approximate-memory-usage", &out);
    JSON ev = {
            {"active_connections",_clients},
            {"stored_events", static_cast<std::intmax_t>(_storage.get_rev())},
            {"database_size", static_cast<std::intmax_t>(_db->get_index_size(docdb::RawKey(0), docdb::RawKey(0xFF)))},
            {"memory_usage", static_cast<std::intmax_t>(std::strtoul(out.c_str(), nullptr, 10))}};
    req.add_header(coroserver::http::strtable::hdr_content_type, "application/json");
    std::string json = ev.to_json();
    return req.send(std::move(json));

}

bool App::is_home_user(const Event::Pubkey &pubkey) const {
    auto r = _index_whitelist.find(pubkey);
    if (!r) return false;
    return r->local;
}


void App::publish(Event &&event, const void *publisher)  {

    //if replacable event, find event to replace
    auto to_replace = doc_to_replace(event);
    bool gc = event.kind == kind::Event_Deletion;

    //to_replace==-1 means that this event is old, cannot be replaced
    if (to_replace != docdb::DocID(-1)) {
        //replace event
        _storage.put(event, to_replace);
        //publish event
        event_publish.publish(EventSource{std::move(event),publisher});

        if (gc) {
            start_gc_thread();
        }

    }

}

void App::publish(Event &&ev, const Attachment &attach, const void *publisher) {
    //if replacable event, find event to replace
    auto to_replace = doc_to_replace(ev);
    auto att_to_replace = find_attachment(attach.id);

     //to_replace==-1 means that this event is old, cannot be replaced
    if (to_replace != docdb::DocID(-1)) {
        //replace event
        docdb::Batch b;
        _storage.put(b, ev, to_replace);
        _storage.put(b, std::move(attach), att_to_replace);
        _db->commit_batch(b);
        //publish event
        event_publish.publish(EventSource{std::move(ev),publisher});
    }



}


docdb::DocID App::find_attachment(const Attachment::ID &id) const {
    auto fnd = _index_attachments.find(id);
    if (fnd) {
        return fnd->id;
    } else {
        return 0;
    }
}

std::string App::get_attachment_link(const Event::ID &id, std::string_view mime) const {
    std::string s = "file/";
    coroserver::base64::encode(
            {reinterpret_cast<const char *>(id.data()), id.size()},[&](char c){
        s.push_back(c);
    }, coroserver::base64::Table::get_base64url_table());
    auto ctx = coroserver::http::strContentType[mime];
    if (ctx != coroserver::http::ContentType::binary) {
        std::string_view ext = coroserver::http::ext2ctx[ctx];
        if (!ext.empty()) s.append(".").append(ext);
    }

    return s;
}

App::Storage::TransactionObserver App::autocompact() {
    docdb::KeyspaceID kid = _storage.get_kid();
    return [kid](docdb::Batch &b, const Storage::Update &up) {
        if (up.old_doc) {
            b.Delete(docdb::RawKey(kid,up.old_doc_id));
        }
    };
}

std::size_t App::run_attachment_gc(ondra_shared::LogObject &lg, std::stop_token stp) {
    std::vector<Attachment::ID> killthem;
    for (const auto &row: _index_attachments.select_all()) {
        if (stp.stop_requested()) break;
        auto [id] = row.key.get<Attachment::ID>();
        auto [state] = row.value.get<bool>();
        if (!state) {
            killthem.push_back(id);
        } else if (!killthem.empty() && killthem.back() == id) {
            killthem.pop_back();
        }
    }
    for (const auto &k: killthem) {
        if (stp.stop_requested()) break;
        lg.debug("Deleting attachment: $1", k.to_hex());
        auto fnd = _index_attachments.find(k);
        if (fnd) {
            _storage.erase(fnd->id);
        }
    }
    return killthem.size();
}

docdb::DocID App::find_event_by_id(const Event::ID &id) const {
    auto r = _index_by_id.find(id);
    if (r) return r->id;
    else return 0;
}

void App::start_gc_thread() {
    if (_gc_running.exchange(true,std::memory_order_relaxed) == false) {
        if (_gc_thread.joinable()) _gc_thread.join();
        _gc_thread = std::jthread([&](std::stop_token stp){
            ondra_shared::LogObject lg("GC");
            try {
                auto s = run_attachment_gc(lg, stp);
                lg.progress("Done $1 attachment(s) collected", s);
            } catch(std::exception &e) {
                lg.error("$1", e.what());
            } catch(...) {
                lg.error("unknown exception");
            }
            _gc_running.store(false, std::memory_order_relaxed);
        });
    }
}

bool App::is_this_me(std::string_view relay) const {
    if (relay.empty()) return false;
    if (relay.back() == '/') relay = relay.substr(0, relay.size()-1);
    std::lock_guard _(_app_share);
    auto iter = _this_relay_url.find(relay);
    return iter != _this_relay_url.end();
}

static auto aggrDepth() {
    return [dist = 255U](const docdb::Row &row) mutable-> Event::Depth{
        auto [d] = row.get<unsigned char>();
        if (d < dist) dist = d;
        return Event::Depth(dist);
    };
}

std::vector<std::pair<std::string, Event::Depth> > App::get_known_relays() const {
    std::vector<std::pair<std::string, Event::Depth> > out;
    for (const auto &row: docdb::AggregateBy<std::tuple<std::string> >::Recordset(
            _index_routing.select_all(), aggrDepth())) {
        out.push_back({std::get<0>(row.key), row.value});
    }
    return out;

}

std::vector<std::pair<Event::Pubkey, Event::Depth> > App::get_users_on_relay(std::string_view relay) const {
    std::vector<std::pair<Event::Pubkey, unsigned char> > out;
    for (const auto &row: docdb::AggregateBy<std::tuple<std::string, Event::Pubkey> >::Recordset(
            _index_routing.select(relay), aggrDepth())) {
        out.push_back({std::get<1>(row.key), row.value});
    }
    return out;
}

struct Nip05req_query {
    std::string name;
    static constexpr auto fields = coroserver::http::makeQueryFieldMap<Nip05req_query>({
        {"name", &Nip05req_query::name}
    });
};



cocls::future<bool> App::process_nip05_request(coroserver::http::ServerRequest &req, std::string_view vpath) {
    req.content_type(coroserver::http::ContentType::json);
    docdb::Structured names = docdb::Structured::KeyPairs();
    docdb::Structured relays = docdb::Structured::KeyPairs();
    do {
        Nip05req_query q;
        coroserver::http::parse_query(vpath, Nip05req_query::fields, q);
        if (q.name.empty() || q.name == "_")
            return cocls::future<bool>::set_value(false);       //return here - allow to override by static pages
        auto host = req.get_host();
        std::string ident;
        ident.append(q.name).append("@").append(host);
        auto fres = _index_nip05.find(ident);
        if (!fres) break;
        auto doc = _storage.find(fres->id);
        if (!doc || !std::holds_alternative<Event>(doc->document)) break;
        const Event &ev = std::get<Event>(doc->document);
        auto base_url = req.get_url();
        std::string my_relay("ws");my_relay.append(base_url.substr(4,base_url.find("/.well-known")-4));
        names.set (q.name,ev.author.to_hex());
        relays.set(ev.author.to_hex(), docdb::Structured::Array({my_relay}));
    }while(false);
    docdb::Structured result = {
            {"names",names},
            {"relays",relays}
    };
    req.content_type(coroserver::http::ContentType::json);
    req.add_header(coroserver::http::strtable::hdr_access_control_allow_origin, "*");
    return req.send(result.to_json());

}

} /* namespace nostr_server */

