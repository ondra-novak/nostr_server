#pragma once
#ifndef SRC_NOSTR_SERVER_APP_H_
#define SRC_NOSTR_SERVER_APP_H_

#include "publisher.h"
#include "config.h"
#include "iapp.h"



#include <docdb/json.h>
#include <cocls/publisher.h>
#include <coroserver/http_server.h>
#include <coroserver/websocket_stream.h>
#include <coroserver/http_static_page.h>
#include <memory>

namespace nostr_server {


class App: public std::enable_shared_from_this<App>, public IApp {
public:

    static const Event supported_nips;
    static const std::string software_url;
    static const std::string software_version;


    App(const Config &cfg);

    void init_handlers(coroserver::http::Server &server);


    virtual EventPublisher &get_publisher() override {return event_publish;}
    virtual Storage &get_storage() override {return _storage;}
    virtual docdb::DocID doc_to_replace(const Event &event) const override;
    virtual bool find_in_index(docdb::RecordSetCalculator &calc, const std::vector<Filter> &filters, std::vector<FulltextRelevance> &&relevance) const ;
    virtual docdb::PDatabase get_database() const {return _db;}
protected:
    coroserver::http::StaticPage static_page;

    struct IndexByIdFn {
        static constexpr int revision = 2;
        template<typename Emit> void operator ()(Emit emit, const Event &ev) const;
    };
    struct IndexByPubkeyHashTimeFn {
        static constexpr int revision = 1;
        template<typename Emit> void operator ()(Emit emit, const Event &ev) const;
    };
    struct IndexTagValueHashTimeFn {
        static constexpr int revision = 1;
        template<typename Emit> void operator ()(Emit emit, const Event &ev) const;
    };
    struct IndexKindTimeFn {
        static constexpr int revision = 1;
        template<typename Emit> void operator ()(Emit emit, const Event &ev) const;
    };
    struct IndexByAuthorKindFn {
        static constexpr int revision = 2;
        template<typename Emit> void operator()(Emit emit, const Event &ev) const;
    };

    struct IndexForFulltextFn{
        static constexpr int revision = 3;
        template<typename Emit> void operator()(Emit emit, const Event &ev) const;
    };

    struct IndexByAuthorKindFn;

    using IndexById = docdb::Indexer<Storage,IndexByIdFn,docdb::IndexType::unique>;
    using IndexByAuthorKind = docdb::Indexer<Storage,IndexByAuthorKindFn,docdb::IndexType::unique, TimestampRowDef>;
    using IndexByPubkeyTime = docdb::Indexer<Storage,IndexByPubkeyHashTimeFn,docdb::IndexType::multi>;
    using IndexTagValueHashTime = docdb::Indexer<Storage,IndexTagValueHashTimeFn,docdb::IndexType::multi>;
    using IndexKindTime = docdb::Indexer<Storage,IndexKindTimeFn,docdb::IndexType::multi>;
    using IndexForFulltext = docdb::Indexer<Storage,IndexForFulltextFn,docdb::IndexType::multi>;




    EventPublisher event_publish;
    docdb::PDatabase _db;
    ServerDescription _server_desc;


    Storage _storage;
    IndexById _index_by_id;
    IndexByPubkeyTime _index_pubkey_time;
    IndexByAuthorKind _index_replaceable;
    IndexTagValueHashTime _index_tag_value_time;
    IndexKindTime _index_kind_time;
    IndexForFulltext _index_fulltext;

    cocls::future<bool> send_infodoc(coroserver::http::ServerRequest &req);

    static void merge_ft(FTRList &out, FTRList &a, FTRList &tmp);
    static void dedup_ft(FTRList &x);


};



} /* namespace nostr_server */

#endif /* SRC_NOSTR_SERVER_APP_H_ */
