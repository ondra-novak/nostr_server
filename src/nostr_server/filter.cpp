/*
 * filter.cpp
 *
 *  Created on: 21. 6. 2023
 *      Author: ondra
 */



#include "filter.h"

namespace nostr_server {


bool Filter::test(const Event &doc) const {
try {
    if (!authors.empty() && std::find(authors.begin(), authors.end(), doc.author) == authors.end()) return false;
    if (!ids.empty() && std::find(ids.begin(), ids.end(), doc.id) == ids.end()) return false;
    if (!kinds.empty() && std::find(kinds.begin(), kinds.end(), doc.kind) == kinds.end()) return false;
    if (!tags.empty()) {
        std::uint64_t checks = 0;
        for (const auto &tag : doc.tags) {
            if (tag.tag.size() == 1) {
                char t = tag.tag[0];
                auto iter = std::lower_bound(tags.begin(), tags.end(), std::pair(t, tag.content), std::less<std::pair<char, std::string_view> >());
                if (iter != tags.end() && iter->first == t && iter->second == tag.content) {
                    checks |= tag2mask(t);
                }
            }
        }
        if ((checks & tag_mask) != tag_mask) return false;
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
                Event::Pubkey pk;
                auto hx = c.as<std::string_view>();
                binary_from_hex(hx.begin(), hx.end(), pk);
                out.authors.push_back(std::move(pk));
            }
        } else if (k == "ids") {
            const auto &a = v.array();
            for (const auto &c: a)  {
                Event::ID id;
                auto hx = c.as<std::string_view>();
                binary_from_hex(hx.begin(), hx.end(), id);
                out.ids.push_back(std::move(id));
            }
        } else if (k == "kinds") {
            const auto &a = v.array();
            for (const auto &c: a)  {
                out.kinds.push_back(c.as<unsigned int>());
            }
        } else if (k.substr(0,1) == "#") {
            auto t = k.substr(1);
            if (t.size() == 1) {
                char n = t[0];
                auto mask = tag2mask(n);
                if (mask) {
                    const auto &a = v.array();
                    StringOptions opts;
                    for (const auto &c: a)  {
                        out.tags.push_back(std::pair(n,c.as<std::string>()));
                    }
                    out.tag_mask |= mask;
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
