#pragma once
#ifndef SRC_NOSTR_SERVER_IAPP_H_
#define SRC_NOSTR_SERVER_IAPP_H_
#include "publisher.h"
#include "filter.h"

#include <docdb/database.h>
#include <docdb/storage.h>
#include <docdb/indexer.h>
#include <docdb/binops.h>

#include <optional>

namespace nostr_server {

class IApp {
public:



    using DocumentType =docdb::StructuredDocument<docdb::Structured::use_string_view> ;

    using AuthorKindTagKey = std::tuple<std::string_view, unsigned int, std::string_view>;
    using TimestampRowDef = docdb::FixedRowDocument<std::time_t>;
    using IdHashKey = std::size_t;

    using Storage = docdb::Storage<DocumentType>;
    using IndexViewByAuthorKindTag = docdb::IndexView<Storage,TimestampRowDef, docdb::IndexType::unique>;




    using OrderingItem = std::pair<unsigned int, unsigned int>;
    using RecordsetCalculator = docdb::RecordsetStackT<docdb::DocID, OrderingItem>;

    using DocIDList = std::vector<docdb::DocID>;

    virtual ~IApp() = default;
    virtual EventPublisher &get_publisher() = 0;
    virtual Storage &get_storage() = 0;
    ///Returns candidates for given filter
    /**
     * @note doesn't apply filter!, it just chooses index to enumerate documents,
     * you need to load each document and test it for filter
     * @param filter filter
     * @return candidates
     */
    virtual void find_in_index(RecordsetCalculator &calc, const std::vector<Filter> &filters) const = 0;
    virtual docdb::DocID doc_to_replace(const Event &event) const = 0;
    virtual docdb::DocID find_replacable(std::string_view pubkey, unsigned int kind, std::string_view category) const = 0;
    virtual docdb::PDatabase get_database() const = 0;
    virtual Event get_server_capabilities() const = 0;
    virtual bool is_home_user(std::string_view pubkey) const = 0;
    virtual void client_counter(int increment) = 0;
    virtual void publish(Event &&ev, std::string source) = 0;
    virtual docdb::DocID find_by_id(std::string_view id) const = 0;

};

using PApp = std::shared_ptr<IApp>;



}



#endif /* SRC_NOSTR_SERVER_IAPP_H_ */
