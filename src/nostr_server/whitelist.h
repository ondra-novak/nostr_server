#pragma once
#ifndef _SRC_NOSTR_SERVER_WHITELIST_H_
#define _SRC_NOSTR_SERVER_WHITELIST_H_

#include "event.h"
#include "iapp.h"


#include <docdb/incremental_aggregator.h>
#include <algorithm>

namespace nostr_server {

struct Karma {
    unsigned int followers;
    unsigned int mutes;
    unsigned int mentions;
    unsigned int directmsgs;

    int get_score() const { 
        return std::min(2U, mentions) + std::min(2U, directmsgs)+followers-mutes;
    }
};

struct KarmaDocument {
    using Type = Karma;
    template<typename Iter>
    static Iter to_binary(const Karma &what, Iter at) {
        const char *from = reinterpret_cast<const char *>(&what);
        const char *to = from+sizeof(Karma);
        return std::copy(from, to, at);
    }
    template<typename Iter>
    static Karma from_binary(Iter &at, Iter end) {
        Karma what;
        char *from = reinterpret_cast<const char *>(&what);
        char *to = from+sizeof(Karma);
        while(from != to && at != end) {
            *from = *at;
            ++from;
            ++at;
        }
        return what;
    }
};

struct WhiteListIndexFn {
    static constexpr int revision = 3;
    template<typename Emit> void operator ()(Emit emit, const Event &ev) const {
        


    }
};

using WhiteListIndex = docdb::IncrementalAggregator<IApp::Storage, WhiteListIndexFn, KarmaDocument>;


}


#endif