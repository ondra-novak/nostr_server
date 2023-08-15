# nostr_server
Experimental standalone NOSTR relay for testing, extending - it uses LevelDB as database backend


## Important note

**Experimental implementation**

**In development!!!**


## Requirement

* Linux (compiled on ubuntu/debian)
* GCC (g++) 12+. clang 14+
* libsecp256k1
* libleveldb
* libreadline
* libunac1
* libssl
* pkg_config

## Technologies and libraries involved

* **leveldb** - database backend - you don't need to configure external database
* **docdb** - extension above leveldb - this library is actually *tested* in this product
* **coroserver** - http(s), websocket server and client uses C++20 coroutines.
* **cocls** - support for coroutines in C++20

## Implemented features

* full-fledged node according to approved NIPs {1,5, 9,11,12,16,20,33,42,45,50,97}
* Implements proposal NIP-97 (storing binary content)
* (optional) whitelisting. filters incoming events. Only events from home users, his followers, user in mentions, or direct messages are allowed (to limit spam from random public keys).
* NIP-05 is implemented (automatic, no registration)

## Planned features 

* all possible NIPs
* external sources - to publish events from scripts
* groups - a group is account which reposts received events
* replication - master-to-master replication between multiple relays
* login to server, signer tool, user database


## How NIP05
* your identificator at this relay is `<name>@<relay-url>`
* recommended relay is always `<relay-url>`
* store KIND: 0 (metadata) at this relay with `<name>@<relay-url>`. It must contain an unique identifier. If the identifier is already taken, your event will not be accepted.
* you should be verified now
* identifier '_' is not allowed
* relay administrator can define '_' identifier by publishing the nostr.json document at www/.well-known/nostr. This document only appears for name '_'.
