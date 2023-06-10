#pragma once
#ifndef SRC_NOSTR_SERVER_PUBLISHER_H_
#define SRC_NOSTR_SERVER_PUBLISHER_H_

#include <cocls/publisher.h>
#include <docdb/structured_document.h>

namespace nostr_server {


using Event = docdb::Structured;

using EventPublisher = cocls::publisher<Event>;
using EventSubscriber = cocls::subscriber<Event>;

}




#endif /* SRC_NOSTR_SERVER_PUBLISHER_H_ */
