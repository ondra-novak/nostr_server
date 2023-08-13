/*
 * routing.h
 *
 *  Created on: 12. 8. 2023
 *      Author: ondra
 */

#ifndef SRC_NOSTR_SERVER_ROUTING_H_
#define SRC_NOSTR_SERVER_ROUTING_H_

#include "event.h"
#include <docdb/indexer.h>

namespace nostr_server {

struct RoutingIndexFn {
    static constexpr int revision = 1;
    template<typename Emit>
    void operator ()(Emit emit, const EventOrAttachment &evatt) const;
};

using RoutingIndex = docdb::Indexer<docdb::Storage<EventDocument>, RoutingIndexFn, docdb::IndexType::multi>;


}



#endif /* SRC_NOSTR_SERVER_ROUTING_H_ */
