/*
 * fulltext.h
 *
 *  Created on: 13. 6. 2023
 *      Author: ondra
 */

#ifndef SRC_NOSTR_SERVER_FULLTEXT_H_
#define SRC_NOSTR_SERVER_FULLTEXT_H_
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nostr_server {

using WordToken = std::pair<std::string, unsigned char>;



void tokenize_text(const std::string_view &text, std::vector<WordToken> &tokens);



}



#endif /* SRC_NOSTR_SERVER_FULLTEXT_H_ */
