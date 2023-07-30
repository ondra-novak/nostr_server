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
    std::vector<std::pair<char,std::vector<std::string> > > tags;
    std::optional<std::time_t> since;
    std::optional<std::time_t> until;
    std::optional<unsigned int> limit;
    std::string ft_search;

    bool test(const Event &doc) const;
    static Filter create(const JSON &f);


};


}



#endif /* SRC_NOSTR_SERVER_FILTER_H_ */
