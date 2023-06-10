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
    virtual const IndexByAuthorKind &get_index_replaceable() const override {return _index_replaceable;}
    virtual std::vector<docdb::DocID> find_in_index(const Filter &filter) const;
protected:
    coroserver::http::StaticPage static_page;

    EventPublisher event_publish;
    docdb::PDatabase _db;


    Storage _storage;
    IndexById _index_by_id;
    IndexByAuthorKind _index_replaceable;



};

} /* namespace nostr_server */

#endif /* SRC_NOSTR_SERVER_APP_H_ */
