#pragma once
#ifndef SRC_NOSTR_SERVER_IAPP_H_
#define SRC_NOSTR_SERVER_IAPP_H_
#include "publisher.h"

#include <docdb/database.h>
#include <docdb/storage.h>
#include <docdb/indexer.h>


namespace nostr_server {

class IApp {
public:



    using DocumentType =docdb::StructuredDocument<docdb::Structured::use_string_view> ;

    using AuthorKindTagKey = std::tuple<std::string_view, unsigned int, std::string_view>;
    using TimestampRowDef = docdb::FixedRowDocument<std::time_t>;
    using IdHashKey = std::size_t;

    using Storage = docdb::Storage<DocumentType>;
    using IndexViewByAuthorKindTag = docdb::IndexView<Storage,TimestampRowDef, docdb::IndexType::unique>;



    struct Filter {
        std::vector<std::string> ids;
        std::vector<std::string> authors;
        std::vector<int> kinds;
        std::vector<std::pair<char, std::string> > tags;
        std::time_t since;
        std::time_t until;

        bool test(const docdb::Structured &doc) const;
    };


    virtual ~IApp() = default;
    virtual EventPublisher &get_publisher() = 0;
    virtual Storage &get_storage() = 0;
    virtual std::vector<docdb::DocID> find_in_index(const Filter &filter) const = 0;
    virtual docdb::DocID doc_to_replace(const Event &event) const = 0;
};

using PApp = std::shared_ptr<IApp>;


}



#endif /* SRC_NOSTR_SERVER_IAPP_H_ */
