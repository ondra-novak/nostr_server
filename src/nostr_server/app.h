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


    App(const Config &cfg);

    void init_handlers(coroserver::http::Server &server);


    virtual EventPublisher &get_publisher() override {return event_publish;}
    virtual Storage &get_storage() override {return _storage;}
    virtual docdb::DocID doc_to_replace(const Event &event) const override;
    virtual std::vector<docdb::DocID> find_in_index(const Filter &filter) const;
protected:
    coroserver::http::StaticPage static_page;

    struct IndexByIdHashFn {
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

    struct IndexByAuthorKindFn;

    using IndexById = docdb::Indexer<Storage,IndexByIdHashFn,docdb::IndexType::multi>;
    using IndexByAuthorKind = docdb::Indexer<Storage,IndexByAuthorKindFn,docdb::IndexType::unique, TimestampRowDef>;
    using IndexByPubkeyTime = docdb::Indexer<Storage,IndexByPubkeyHashTimeFn,docdb::IndexType::multi>;
    using IndexTagValueHashTime = docdb::Indexer<Storage,IndexTagValueHashTimeFn,docdb::IndexType::multi>;
    using IndexKindTime = docdb::Indexer<Storage,IndexKindTimeFn,docdb::IndexType::multi>;




    EventPublisher event_publish;
    docdb::PDatabase _db;


    Storage _storage;
    IndexById _index_by_id;
    IndexByPubkeyTime _index_pubkey_time;
    IndexByAuthorKind _index_replaceable;
    IndexTagValueHashTime _index_tag_value_time;
    IndexKindTime _index_kind_time;



};



} /* namespace nostr_server */

#endif /* SRC_NOSTR_SERVER_APP_H_ */
