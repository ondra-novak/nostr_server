#pragma once
#ifndef SRC_NOSTR_SERVER_BINARY_H_
#define SRC_NOSTR_SERVER_BINARY_H_
#include <array>
#include <string>
#include <string_view>

namespace nostr_server {

template<typename OutIter, typename InIter>
OutIter binary_to_hex(InIter beg, InIter end, OutIter out) {
    while (beg != end) {
        unsigned char val = *beg;
        char a = val >> 4;
        char b = val & 0xF;
        a = a + '0' + ('a' - '0' - 10) * (a > 9);
        b = b + '0' + ('a' - '0' - 10) * (b > 9);
        *out = a;
        ++out;
        *out = b;
        ++out;
        ++beg;
    }
    return out;
}


template<std::size_t byte_count>
class Binary : public std::array<unsigned char, byte_count> {
public:

    using std::array<unsigned char, byte_count>::array;

    template<typename Iter>
    static Binary from_hex(Iter begin, Iter end, Iter &read_end);
    template<typename Iter>
    static Binary from_hex(Iter begin, Iter end);
    static Binary from_hex(std::string_view hexstr);
    static Binary from_hex(const std::array<char, byte_count*2> &hex) {
        return from_hex(hex.begin(), hex.end());
    }

    std::string to_hex() const;

    template<typename Iter>
    Iter to_hex(Iter output) const;

    operator std::array<char, byte_count*2>() const {
        std::array<char, byte_count*2> out;
        to_hex(out.begin());
        return out;
    }



};


template<std::size_t byte_count>
template<typename Iter>
Binary<byte_count> Binary<byte_count>::from_hex(Iter beg, Iter end) {
    Iter dummy;
    return from_hex(beg, end, dummy);
}


template<std::size_t byte_count>
template<typename Iter>
Binary<byte_count> Binary<byte_count>::from_hex(Iter beg, Iter end, Iter &read_end) {
    Binary<byte_count> out;
    std::size_t ofs = 0;
    while (beg != end && ofs < byte_count) {
        char a = *beg;
        unsigned char x = (a - '0') - ('A' - '0' - 10) * (a >= 'A') - ('a' - 'A') * (a >= 'a');
        if (x > 0xF) break;
        ++beg;
        if (beg != end) {
            char b = *beg;
            unsigned char y = (b - '0') - ('A' - '0' - 10) * (b >= 'A') - ('a' - 'A') * (b >= 'a');
            if (y > 0xF) break;
            out[ofs] = (x << 4) | y;
            ++ofs;
            ++beg;
        }
    }
    read_end = end;
    return out;

}

template<std::size_t byte_count>
inline Binary<byte_count> Binary<byte_count>::from_hex(std::string_view hexstr) {
    return from_hex(hexstr.begin(), hexstr.end());
}

template<std::size_t byte_count>
inline std::string Binary<byte_count>::to_hex() const {
    std::string buff;
    buff.resize(byte_count*2);
    to_hex(buff.begin());
    return buff;
}

template<std::size_t byte_count>
template<typename Iter>
inline Iter Binary<byte_count>::to_hex(Iter output) const {
    return binary_to_hex(this->begin(), this->end(), output);

}



}

#endif /* SRC_NOSTR_SERVER_BINARY_H_ */

