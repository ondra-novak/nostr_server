/*
 * filter.h
 *
 *  Created on: 21. 6. 2023
 *      Author: ondra
 */

#ifndef SRC_NOSTR_SERVER_FILTER_H_
#define SRC_NOSTR_SERVER_FILTER_H_
#include "publisher.h"

#include <ctime>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nostr_server {

using StringOptions = std::vector<std::string>;
using PubkeyOptions = std::vector<std::string>;
using NumberOptions = std::vector<unsigned int>;

struct Filter {
    using ShortenID = std::pair<Event::ID, unsigned char>;
    using ShortenPubkey = std::pair<Event::Pubkey, unsigned char>;


    std::vector<ShortenID> ids;
    std::vector<ShortenPubkey> authors;
    std::vector<Event::Kind> kinds;
    std::vector<std::pair<char,std::string>  > tags;
    std::optional<std::time_t> since;
    std::optional<std::time_t> until;
    std::optional<unsigned int> limit;
    std::uint64_t tag_mask = 0;
    std::string ft_search;

    bool test(const Event &doc) const;
    static Filter create(const JSON &f);

    static constexpr int tag2bit(char tag) {
        return (tag >= '0' && tag <= '9')*(tag -'0'+1)
             + (tag >= 'A' && tag <= 'Z')*(tag -'A'+11)
             + (tag >= 'a' && tag <= 'z')*(tag -'a'+37) - 1;
    }
    static constexpr std::uint64_t tag2mask(char tag) {
        auto n = tag2bit(tag);
        if (n < 0) return 0;
        else return (std::uint64_t(1) << n);
    }

    auto tags_begin() const {
        return tags.begin();
    }
    auto tags_end() const {
        return tags.end();
    }
    auto tags_next(auto iter) const {
        auto e = tags_end();
        if (iter == e) return e;
        char c = iter->first;
        do {
            ++iter;
        } while (iter != e && iter->first == c);
        return iter;
    }

};


}



#endif /* SRC_NOSTR_SERVER_FILTER_H_ */
