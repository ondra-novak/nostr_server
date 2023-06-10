#pragma once
#ifndef SRC_NOSTR_SERVER_APP_H_
#define SRC_NOSTR_SERVER_APP_H_

#include "publisher.h"
#include "config.h"
#include "iapp.h"



#include <docdb/json.h>
#include <docdb/database.h>
#include <docdb/storage.h>
#include <docdb/indexer.h>
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
protected:
    coroserver::http::StaticPage static_page;

    EventPublisher event_publish;
    docdb::PDatabase _db;
    using DocumentType =docdb::StructuredDocument<docdb::Structured::use_string_view> ;

    using Storage = docdb::Storage<DocumentType>;
    using IndexById = docdb::Indexer<Storage,[](auto emit, const Event &ev){
        emit(ev["id"].as<std::string_view>());
    },1,docdb::IndexType::unique_hide_dup>;

    Storage _storage;




};

} /* namespace nostr_server */

#endif /* SRC_NOSTR_SERVER_APP_H_ */
