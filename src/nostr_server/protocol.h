#pragma once
#ifndef SRC_NOSTR_SERVER_PROTOCOL_H_
#define SRC_NOSTR_SERVER_PROTOCOL_H_

#include <coroserver/static_lookup.h>

namespace nostr_server {

NAMED_ENUM(Command,
        unknown,
        REQ,
        EVENT,
        CLOSE,
        COUNT,
        EOSE,
        NOTICE,
        AUTH,
        OK,
        FILE,
        RETRIEVE,
);
constexpr NamedEnum_Command commands={};

}




#endif /* SRC_NOSTR_SERVER_PROTOCOL_H_ */
