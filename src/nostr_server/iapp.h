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

enum class PeerServerity: int {
    debug,
    progress,
    warn,
    error
};




class IApp {
public:

    using AuthorKindTagKey = std::tuple<Event::Pubkey, unsigned int, std::string_view>;
    using TimestampRowDef = docdb::FixedRowDocument<std::time_t>;
    using IdHashKey = std::size_t;

    using Storage = docdb::Storage<EventDocument>;
    using IndexViewByAuthorKindTag = docdb::IndexView<Storage,TimestampRowDef, docdb::IndexType::unique>;

    using OrderingItem = std::pair<unsigned int, unsigned int>;
    using RecordSetCalculator = docdb::RecordsetStackT<docdb::DocID, OrderingItem>;

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
    virtual void find_in_index(RecordSetCalculator &calc, const std::vector<Filter> &filters) const = 0;
    virtual docdb::DocID find_event_by_id(const Event::ID &id) const = 0;
    virtual docdb::DocID doc_to_replace(const Event &event) const = 0;
    virtual docdb::DocID find_replacable(std::string_view pubkey, unsigned int kind, std::string_view category) const = 0;
    virtual docdb::PDatabase get_database() const = 0;
    virtual JSON get_server_capabilities() const = 0;
    virtual bool is_home_user(const Event::Pubkey & pubkey) const = 0;
    virtual void client_counter(int increment, std::string_view url) = 0;
    virtual void publish(Event &&ev, const void *publisher) = 0;
    virtual void publish(Event &&ev, const Attachment &attach, const void *publisher) = 0;
    virtual bool check_whitelist(const Event::Pubkey &k) const = 0;
    virtual int get_karma(const Event::Pubkey &k) const = 0;
    virtual bool is_this_me(std::string_view relay) const = 0;
    ///retrieve all known relays (exploring routing events)
    /** There can be empty string as relay which denotes that some users has unknown relay */
    virtual std::vector<std::pair<std::string, Event::Depth> > get_known_relays() const = 0;
    ///retrieve all users on specified relay
    /**
     * @param relay relay to search
     * @return returns pubkey and reference level for every user. Reference level defines how far the
     * user is. Zero means, that this is local user, 1 means that a local user following that user, 2 means
     * that there is intermediate user between this user and local user
     */
    virtual std::vector<std::pair<Event::Pubkey, Event::Depth> > get_users_on_relay(std::string_view relay) const = 0;
    ///Finds attachment by id
    /**
     * @param id id to find
     * @return docid if found, or zero if not
     */
    virtual docdb::DocID find_attachment(const Attachment::ID &id) const = 0;
    virtual std::string get_attachment_link(const Event::ID &mediaHash, std::string_view mime) const = 0;

};

using PApp = std::shared_ptr<IApp>;



}



#endif /* SRC_NOSTR_SERVER_IAPP_H_ */
