#pragma once
#ifndef SRC_NOSTR_SERVER_APP_H_
#define SRC_NOSTR_SERVER_APP_H_

#include "publisher.h"
#include "config.h"
#include "iapp.h"
#include "telemetry_def.h"
#include "../telemetry/open_metrics/Collector.h"
#include "whitelist.h"


#include <docdb/json.h>
#include <cocls/publisher.h>
#include <coroserver/http_server.h>
#include <coroserver/websocket_stream.h>
#include <coroserver/http_static_page.h>
#include <shared/logOutput.h>
#include <stop_token>
#include <memory>
#include <thread>

namespace nostr_server {


class App: public std::enable_shared_from_this<App>, public IApp {
public:

    static const docdb::Structured supported_nips;
    static const std::string software_url;
    static const std::string software_version;


    App(const Config &cfg);

    void init_handlers(coroserver::http::Server &server);


    virtual EventPublisher &get_publisher() override {return event_publish;}
    virtual Storage &get_storage() override {return _storage;}
    virtual docdb::DocID doc_to_replace(const Event &event) const override;
    virtual docdb::DocID find_event_by_id(const Event::ID &id) const override;
    virtual void find_in_index(RecordSetCalculator &calc, const std::vector<Filter> &filters) const override ;
    virtual docdb::PDatabase get_database() const override {return _db;}
    virtual JSON get_server_capabilities() const override;
    virtual bool is_home_user(const Event::Pubkey &pubkey) const override;
    virtual void client_counter(int increment) override;
    virtual void publish(Event &&ev, const void *publisher) override;
    virtual docdb::DocID find_replacable(std::string_view pubkey, unsigned int kind, std::string_view category) const override;
    virtual bool check_whitelist(const Event::Pubkey &k) const override;
    virtual AttachmentLock publish_attachment(Attachment &&event) override;
    virtual docdb::DocID find_attachment(const Attachment::ID &id) const override;
    virtual std::string get_attachment_link(const Attachment::ID &id) const override;
    virtual AttachmentLock lock_attachment(const Attachment::ID &id)  override;
protected:
    coroserver::http::StaticPage static_page;

    struct IndexByIdFn {
        static constexpr int revision = 3;
        template<typename Emit> void operator ()(Emit emit, const EventOrAttachment &ev) const;
    };
    struct IndexByPubkeyHashTimeFn {
        static constexpr int revision = 1;
        template<typename Emit> void operator ()(Emit emit, const EventOrAttachment &ev) const;
    };
    struct IndexTagValueHashTimeFn {
        static constexpr int revision = 2;
        template<typename Emit> void operator ()(Emit emit, const EventOrAttachment &ev) const;
    };
    struct IndexKindTimeFn {
        static constexpr int revision = 1;
        template<typename Emit> void operator ()(Emit emit, const EventOrAttachment &ev) const;
    };
    struct IndexTimeFn {
        static constexpr int revision = 1;
        template<typename Emit> void operator ()(Emit emit, const EventOrAttachment &ev) const;
    };
    struct IndexByAuthorKindFn {
        static constexpr int revision = 2;
        template<typename Emit> void operator()(Emit emit, const EventOrAttachment &ev) const;
    };

    struct IndexForFulltextFn{
        static constexpr int revision = 4;
        template<typename Emit> void operator()(Emit emit, const EventOrAttachment &ev) const;
    };
    struct IndexAttachmentFn {
        static constexpr int revision = 3;
        template<typename Emit> void operator()(Emit emit, const EventOrAttachment &ev) const;
    };


    using IndexById = docdb::Indexer<Storage,IndexByIdFn,docdb::IndexType::unique>;
    using IndexByAuthorKind = docdb::Indexer<Storage,IndexByAuthorKindFn,docdb::IndexType::unique, TimestampRowDef>;
    using IndexByPubkeyTime = docdb::Indexer<Storage,IndexByPubkeyHashTimeFn,docdb::IndexType::multi>;
    using IndexTagValueHashTime = docdb::Indexer<Storage,IndexTagValueHashTimeFn,docdb::IndexType::multi>;
    using IndexKindTime = docdb::Indexer<Storage,IndexKindTimeFn,docdb::IndexType::multi>;
    using IndexTime = docdb::Indexer<Storage,IndexTimeFn,docdb::IndexType::multi>;
    using IndexForFulltext = docdb::Indexer<Storage,IndexForFulltextFn,docdb::IndexType::multi>;
    using IndexAttachments = docdb::Indexer<Storage,IndexAttachmentFn,docdb::IndexType::unique>;

    EventPublisher event_publish;
    docdb::PDatabase _db;
    ServerDescription _server_desc;
    ServerOptions _server_options;
    OpenMetricConf _open_metrics_conf;
    std::shared_ptr<telemetry::open_metrics::Collector> _omcoll;
    telemetry::SharedSensor<docdb::PDatabase> _dbsensor;
    telemetry::SharedSensor<StorageSensor> _storage_sensor;
    mutable bool _empty_database = true;

    std::atomic<int> _clients = {};

    Storage _storage;
    IndexById _index_by_id;
    IndexByPubkeyTime _index_pubkey_time;
    IndexByAuthorKind _index_replaceable;
    IndexTagValueHashTime _index_tag_value_time;
    IndexKindTime _index_kind_time;
    IndexTime _index_time;
    IndexForFulltext _index_fulltext;
    WhiteListIndex _index_whitelist;
    IndexAttachments _index_attachments;


    Storage::TransactionObserver autocompact();


    cocls::future<bool> send_infodoc(coroserver::http::ServerRequest &req);
    cocls::future<bool> send_simple_stats(coroserver::http::ServerRequest &req);
    std::size_t run_attachment_gc(ondra_shared::LogObject &lg, std::stop_token stp);
    void start_gc_thread();

    std::jthread _gc_thread;
    std::atomic<bool> _gc_running = {false};

    struct AttLock {
        std::mutex _mx;
        std::vector<AttachmentWeakLock> _lock_map;
        AttachmentLock lock(const Attachment::ID &id);
        bool is_locked(const Attachment::ID &id);
    };

    AttLock _att_lock;



};



} /* namespace nostr_server */

#endif /* SRC_NOSTR_SERVER_APP_H_ */
