#include "fulltext.h"
#include <docdb/utf8.h>

#include <algorithm>
#include <iostream>
#include <vector>
#include <unac.h>

#include <clocale>
namespace nostr_server {


template<typename Fn>
void tokenize_text(const std::wstring_view &text, Fn cb) {
    std::locale loc("en_US.UTF-8");
    std::wstring word;
    std::wstring unaced;
    unsigned short buff[16];

    auto flush_word = [&]{
        if (!word.empty()) {
            cb(word,1);
            for (auto d: word) {
                if constexpr(sizeof(d) > 2) {
                    if (d > 0x10000) {
                        unaced.push_back(d);
                        continue;
                    }
                }
                unsigned short ch = static_cast<unsigned short>(d);
                int l;
                unsigned short *p = buff;
                unac_char_utf16(ch,p,l);
                if (l == 0) unaced.push_back(d);
                else {
                    for (int i = 0; i < l; i++) {
                        unaced.push_back(p[i]);
                    }
                }
            }
            cb(unaced,2);
            unaced.clear();
            word.clear();
        }
    };

    for (auto c: text) {
        if (std::isalnum(c, loc)) {
            word.push_back(std::tolower(c,loc));
        } else {
            flush_word();
        }
    }
    flush_word();
}



void tokenize_text(const std::string_view &text, std::vector<WordToken> &tokens) {
    std::wstring wtext;
    {
        auto at = text.begin();
        auto end = text.end();
        while (at != end) {
            auto w = docdb::utf8Towchar(at, end);
            if (w != wchar_t(-1)) {
                wtext.push_back(w);
            }
        }
    }
    std::string tmp;
    tokens.clear();
    tokenize_text(wtext, [&](const std::wstring &text, unsigned int relevance){

        tmp.clear();
        auto iter = std::back_inserter(tmp);
        for (wchar_t c: text) docdb::wcharToUtf8(c, iter);
        if (tmp.size()>15) tmp.resize(15);
        tokens.push_back({tmp, relevance});
    });
    std::sort(tokens.begin(), tokens.end());

    if (!tokens.empty()) {
        auto iter = tokens.begin();
        auto iter2 = iter;
        ++iter2;
        auto r = std::remove_if(iter2, tokens.end(), [&](const WordToken &x){
           if (x.first != iter->first) {
               ++iter;
               return false;
           } else {
               return true;
           }
        });
        tokens.erase(r, tokens.end());
    }
}

}

