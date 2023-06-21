/*
 * filter.cpp
 *
 *  Created on: 21. 6. 2023
 *      Author: ondra
 */



#include "filter.h"

namespace nostr_server {


bool Filter::test(const docdb::Structured &doc) const {
try {
    if (!authors.empty()) {
        auto t = doc["pubkey"].as<std::string_view>();
        if (std::find_if(authors.begin(), authors.end(), [&](const std::string_view &a){
            return t.compare(0, a.size(), a) == 0;
        }) == authors.end()) {
            return false;
        }
    }
    if (!ids.empty()) {
        if (std::find(ids.begin(), ids.end(), doc["id"].as<std::string_view>()) == ids.end()) {
            return false;
        }
    }
    if (!kinds.empty()) {
        if (std::find(kinds.begin(), kinds.end(), doc["kind"].as<unsigned int>()) == kinds.end()) {
            return false;
        }
    }
    if (!tags.empty()) {
        std::uint64_t checks = 0;
        const auto &doc_tags = doc["tags"].array();
        for (const auto &tag : doc_tags) {
            if (tag[0].contains<std::string_view>() && tag[1].contains<std::string_view>()) {
                std::string_view tagstr = tag[0].as<std::string_view>();
                std::string_view value = tag[1].as<std::string_view>();
                if (tagstr.size() == 1) {
                    char t = tagstr[0];
                    auto iter = std::lower_bound(tags.begin(), tags.end(), std::pair(t, value), std::less<std::pair<char, std::string_view> >());
                    if (iter != tags.end() && iter->first == t && iter->second == value) {
                        checks |= tag2mask(t);
                    }
                }
            }
        }
        if ((checks & tag_mask) != tag_mask) return false;
    }
    if (since.has_value()) {
        if (doc["created_at"].as<std::time_t>() < *since) return false;
    }
    if (until.has_value()) {
        if (doc["created_at"].as<std::time_t>() > *until) return false;
    }
    return true;
} catch (...) {
    return false;
}

}


Filter Filter::create(const docdb::Structured &f) {
    Filter out;
    const auto &kv = f.keypairs();
    for (const auto &[k, v]: kv) {
        if (k == "authors") {
            const auto &a = v.array();
            for (const auto &c: a)  {
                out.authors.push_back(c.as<std::string>());
            }
        } else if (k == "ids") {
            const auto &a = v.array();
            for (const auto &c: a)  {
                out.ids.push_back(c.as<std::string>());
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
