#pragma once
#ifndef SRC_NOSTR_SERVER_PUBLISHER_H_
#define SRC_NOSTR_SERVER_PUBLISHER_H_

#include <cocls/publisher.h>
#include <docdb/structured_document.h>

namespace nostr_server {


using EventType =docdb::StructuredDocument<docdb::Structured::use_string_view> ;
using Event = docdb::Structured;
using EventSource = std::pair<Event, const void *>;

using EventPublisher = cocls::publisher<EventSource>;
using EventSubscriber = cocls::subscriber<EventSource>;

}




#endif /* SRC_NOSTR_SERVER_PUBLISHER_H_ */
