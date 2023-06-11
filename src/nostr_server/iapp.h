#pragma once
#ifndef SRC_NOSTR_SERVER_IAPP_H_
#define SRC_NOSTR_SERVER_IAPP_H_
#include "publisher.h"

#include <docdb/database.h>
#include <docdb/storage.h>
#include <docdb/indexer.h>

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



    struct Filter {
        std::vector<std::string> ids;
        std::vector<std::string> authors;
        std::vector<unsigned int> kinds;
        std::vector<std::pair<char, std::string> > tags; ///<ordered
        std::optional<std::time_t> since;
        std::optional<std::time_t> until;
        std::optional<unsigned int> limit;

        bool test(const docdb::Structured &doc) const;
        static Filter create(const docdb::Structured &f);
    };


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
    virtual std::vector<docdb::DocID> find_in_index(const Filter &filter) const = 0;
    virtual docdb::DocID doc_to_replace(const Event &event) const = 0;
};

using PApp = std::shared_ptr<IApp>;

void merge_ids(std::vector<docdb::DocID> &out, std::vector<docdb::DocID> &a, std::vector<docdb::DocID> &b);

}



#endif /* SRC_NOSTR_SERVER_IAPP_H_ */
