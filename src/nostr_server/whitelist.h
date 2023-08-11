#pragma once
#ifndef _SRC_NOSTR_SERVER_WHITELIST_H_
#define _SRC_NOSTR_SERVER_WHITELIST_H_

#include "event.h"
#include "iapp.h"
#include "kinds.h"

#include <docdb/incremental_aggregator.h>
#include <algorithm>

namespace nostr_server {

struct Karma {
    unsigned int followers = 0;
    unsigned int mutes = 0;
    unsigned int mentions = 0;
    unsigned int directmsgs = 0;
    bool local = false;

    int get_score() const {
        return (local?2:0)+std::min(2U, mentions) + std::min(2U, directmsgs)+followers-mutes;
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
        char *from = reinterpret_cast<char *>(&what);
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
    static constexpr int revision = 4;
    template<typename Emit> void operator ()(Emit emit, const EventOrAttachment &evatt) const {

        if (!std::holds_alternative<Event>(evatt)) return;
        const Event &ev = std::get<Event>(evatt);

        if (ev.ref_level) return;

        auto update_counter = [&](unsigned int (Karma::*val)) {
                ev.for_each_tag("p",[&](const Event::Tag &t){
                    auto pubkey = Event::Pubkey::from_hex(t.content);
                    auto v = emit(pubkey);
                    if constexpr(emit.erase) {
                        if (v) {
                            Karma k = *v;
                            --(k.*val);
                            v.put(k);
                        }
                    } else {
                        Karma k;
                        if (v) k = *v;
                        ++(k.*val);
                        v.put(k);
                    }
                });
        };
        if (ev.kind < kind::Encrypted_Direct_Messages)
        {
            if constexpr(!emit.erase) {
                auto v = emit(ev.author);
                if (!v) {
                    Karma k;
                    k.local = true;
                    v.put(k);
                } else if (!v->local) {
                    v->local = true;
                    v.put(*v);
                }
            }
        } else {
            auto v = emit(ev.author);
            if (!v || !v->local) return;
        }

        switch (ev.kind) {
            case kind::Short_Text_Note: update_counter(&Karma::mentions);break;
            case kind::Contacts: update_counter(&Karma::followers);break;
            case kind::Encrypted_Direct_Messages: update_counter(&Karma::directmsgs);break;
            case kind::Mute_List: update_counter(&Karma::mutes);break;
            default:break;
        }
    }
};


using WhiteListIndex = docdb::IncrementalAggregator<IApp::Storage, WhiteListIndexFn, KarmaDocument>;


}


#endif
