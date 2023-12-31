#pragma once
#ifndef SRC_NOSTR_SERVER_PUBLISHER_H_
#define SRC_NOSTR_SERVER_PUBLISHER_H_

#include "event.h"

#include <cocls/publisher.h>
#include <docdb/structured_document.h>

namespace nostr_server {


using EventSource = std::pair<Event, const void *>;

using EventPublisher = cocls::publisher<EventSource>;
using EventSubscriber = cocls::subscriber<EventSource>;
using JSON = docdb::Structured;


}




#endif /* SRC_NOSTR_SERVER_PUBLISHER_H_ */
