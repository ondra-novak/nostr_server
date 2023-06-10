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

    struct IndexByIdFn {
        static constexpr int revision = 1;
        template<typename Emit>
        void operator ()(Emit emit, const Event &ev) const {
             emit(ev["id"].as<std::string_view>());
        }
    };

    struct IndexByAuthorKindFn {
        static constexpr int revision = 1;
        template<typename Emit>
        void operator()(Emit emit, const Event &ev) const {
            auto pubkey = ev["pubkey"].as<std::string_view>();
            auto kind = ev["kind"].as<unsigned int>();
            auto tags = ev["tags"];
            docdb::Key k;
            if ((kind == 0) | (kind == 3) | ((kind >= 10000) & (kind < 20000)) | ((kind >= 30000) & (kind < 40000)) ) {
                k.append(pubkey, kind);
                if ((kind >= 30000) & (kind < 40000)) {
                    auto a = tags.array();
                    auto iter = std::find_if(a.begin(), a.end(), [](const docdb::Structured &x){
                        return x[0].contains<std::string_view>() && x[0].as<std::string_view>() == "d";
                    });
                    if (iter != a.end()) {
                        const auto &d = *iter;
                        k.append(d[1].to_string());
                        emit(k);
                    }
                } else {
                    emit(k);
                }
            }
        }
    };

    using DocumentType =docdb::StructuredDocument<docdb::Structured::use_string_view> ;

    using Storage = docdb::Storage<DocumentType>;
    using IndexById = docdb::Indexer<Storage,IndexByIdFn,docdb::IndexType::unique>;
    using IndexByAuthorKind = docdb::Indexer<Storage,IndexByAuthorKindFn,docdb::IndexType::unique>;


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
    virtual const IndexByAuthorKind &get_index_replaceable() const = 0;
    virtual std::vector<docdb::DocID> find_in_index(const Filter &filter) const = 0;
};

using PApp = std::shared_ptr<IApp>;


}



#endif /* SRC_NOSTR_SERVER_IAPP_H_ */
