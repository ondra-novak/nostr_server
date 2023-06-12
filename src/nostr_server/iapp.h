#pragma once
#ifndef SRC_NOSTR_SERVER_IAPP_H_
#define SRC_NOSTR_SERVER_IAPP_H_
#include "publisher.h"

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


    using StringOptions = std::vector<std::string>;
    using NumberOptions = std::vector<unsigned int>;

    struct Filter {
        StringOptions ids;
        StringOptions authors;
        NumberOptions kinds;
        std::map<char,StringOptions> tags;
        std::optional<std::time_t> since;
        std::optional<std::time_t> until;
        std::optional<unsigned int> limit;
        std::uint64_t tag_mask = 0;

        bool test(const docdb::Structured &doc) const;
        static Filter create(const docdb::Structured &f);
    };


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
    virtual bool find_in_index(docdb::RecordSetCalculator &calc, const std::vector<Filter> &filters) const = 0;
    virtual docdb::DocID doc_to_replace(const Event &event) const = 0;

    static void merge_ids(DocIDList &out, DocIDList &a, DocIDList  &tmp);
    static void intersection_ids(DocIDList &out, DocIDList &a, DocIDList &tmp);
};

using PApp = std::shared_ptr<IApp>;



}



#endif /* SRC_NOSTR_SERVER_IAPP_H_ */
