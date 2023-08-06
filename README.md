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

## Planned features 

* all possible NIPs
* external sources - to publish events from scripts
* groups - a group is account which reposts received events
* replication - master-to-master replication between multiple relays
* NIP-05 on server
* login to server, signer tool, user database
