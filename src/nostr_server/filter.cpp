/*
 * filter.cpp
 *
 *  Created on: 21. 6. 2023
 *      Author: ondra
 */



#include "filter.h"

namespace nostr_server {

template<typename V>
auto cmp_shorten(const V &v) {
    return [&](const auto &x) {
        unsigned int count = x.second;
        const auto chk = x.first;
        auto chk_beg = chk.begin();
        auto chk_end = chk_beg;
        std::advance(chk_end, count);
        return std::equal(chk_beg, chk_end, v.begin());
    };
}


bool Filter::test(const Event &doc) const {
try {
    if (!authors.empty() && std::find_if(authors.begin(), authors.end(), cmp_shorten(doc.author)) == authors.end()) return false;
    if (!ids.empty() && std::find_if(ids.begin(), ids.end(), cmp_shorten(doc.id)) == ids.end()) return false;
    if (!kinds.empty() && std::find(kinds.begin(), kinds.end(), doc.kind) == kinds.end()) return false;
    for (const auto &[t, contents]: tags) {
        const Event::Tag *f  = nullptr;
        for (const auto &c : contents) {
            f = doc.find_indexed_tag(t,c);
            if (f) break;
        }
        if (!f) return false;
    }
    if (since.has_value()) {
        if (doc.created_at < *since) return false;
    }
    if (until.has_value()) {
        if (doc.created_at > *until) return false;
    }
    return true;
} catch (...) {
    return false;
}

}


Filter Filter::create(const JSON &f) {
    Filter out;
    const auto &kv = f.keypairs();
    for (const auto &[k, v]: kv) {
        if (k == "authors") {
            const auto &a = v.array();
            for (const auto &c: a)  {
                auto hx = c.as<std::string_view>();
                auto pk = Event::Pubkey::from_hex(hx);
                out.authors.push_back({pk,std::min<unsigned char>(pk.size(),hx.size()/2)});
            }
        } else if (k == "ids") {
            const auto &a = v.array();
            for (const auto &c: a)  {
                auto hx = c.as<std::string_view>();
                auto id = Event::ID::from_hex(hx);
                out.ids.push_back({id,std::min<unsigned char>(id.size(),hx.size()/2)});
            }
        } else if (k == "kinds") {
            const auto &a = v.array();
            for (const auto &c: a)  {
                out.kinds.push_back(c.as<unsigned int>());
            }
        } else if (k.substr(0,1) == "#") {
            auto t = k.substr(1);
            if (t.size() == 1) {
                out.tags.push_back({t[0],{}});
                auto &tt = out.tags.back().second;
                const auto &a = v.array();
                for (const auto &c: a) {
                    tt.push_back(c.as<std::string>());
                }
            }
        } else if (k == "since") {
            out.since = v.as<std::time_t>();
        } else if (k == "until") {
            out.until = v.as<std::time_t>();
        } else if (k == "limit") {
            out.limit = v.as<unsigned int>();
        } else if (k == "search") {
            out.ft_search = v.as<std::string_view>();
        }
    }
    std::sort(out.tags.begin(), out.tags.end());
    return out;
}


}
