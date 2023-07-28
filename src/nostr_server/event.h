#pragma once
#ifndef SRC_NOSTR_SERVER_EVENT_H_
#define SRC_NOSTR_SERVER_EVENT_H_

#include <docdb/row.h>
#include <docdb/structured_document.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <ctime>


namespace nostr_server {

class SignatureTools;



struct Event {
    struct Tag {
        std::string tag;
        std::string content;
        std::vector<std::string> additional_content;
    };


    using ID = std::array<unsigned char, 32>;
    using IDHex = std::array<char, 64>;
    using Pubkey = std::array<unsigned char, 32>;
    using PrivateKey = std::array<unsigned char, 32>;
    using PubkeyHex = std::array<char, 64>;
    using Signature = std::array<unsigned char, 64>; 
    using SignatureHex = std::array<char, 128>; 


    unsigned int kind = 0;
    std::string content;
    std::vector<Tag> tags;
    std::time_t created_at = 0;

    ID id;
    Pubkey author;
    Signature sig;

    static Event fromJSON(std::string_view json_text);
    std::string toJSON() const;
    ID calc_id() const;
    bool verify(const SignatureTools &sigtool) const;
    bool sign(const SignatureTools &sigtool, const PrivateKey &privkey);
};

struct EventDocument {
    using Srl = docdb::StructuredDocument<>;
    using Type = Event;
    template<typename Iter>
    static Iter to_binary(const Event &ev, Iter out) {
        *out = '\0'; //version
        out = Srl::string_to_binary(0,ev.content,out);
        out = Srl::uint_to_binary(0,ev.kind,out);
        out = Srl::uint_to_binary(0,ev.created_at, out);
        out = Srl::uint_to_binary(0,ev.tags.size(),out);
        for(const auto &t: ev.tags) {
            out = Srl::string_to_binary(0,t.tag,out);
            out = Srl::string_to_binary(0,t.content,out);
            out = Srl::uint_to_binary(0,t.additional_content.size(),out);
            for (const auto &z: t.additional_content) {
                out = Srl::string_to_binary(0,z,out);
            }  
        } 
        std::copy(ev.id.begin(), ev.id.end(), out);
        std::copy(ev.author.begin(), ev.author.end(), out);
        std::copy(ev.sig.begin(), ev.sig.end(), out);
    }
    template<typename Iter>
    static Event from_binary(Iter &at, Iter end) {
        Event ev;
        get_extra(at,end);
        auto x = get_extra(at,end);
        ev.content = Srl::string_from_binary(x, at, end);
        x = get_extra(at,end);
        ev.kind = Srl::uint_from_binary(x,at,end);
        x = get_extra(at,end);
        ev.created_at = Srl::uint_from_binary(x,at,end);
        x = get_extra(at,end);
        std::size_t tag_count = Srl::uint_from_binary(x,at,end);
        ev.tags.reserve(tag_count);
        for (std::size_t i = 0; i < tag_count; ++i) {
            Event::Tag t;
            x = get_extra(at,end);
            t.tag = Srl::string_from_binary(x, at, end);
            x = get_extra(at,end);
            t.content = Srl::string_from_binary(x, at, end);
            x = get_extra(at,end);
            std::size_t add_count = Srl::uint_from_binary(x,at,end);
            for (std::size_t j = 0; j < add_count; ++j)  {
                x = get_extra(at,end);
                t.additional_content.push_back(Srl::string_from_binary(x, at, end));
            }
            ev.tags.push_back(std::move(t));
        }
        load_bin(at, end, ev.id);
        load_bin(at, end, ev.author);
        load_bin(at, end, ev.sig);        
     }

    template<typename Iter>
    static unsigned char get_extra(Iter &at, Iter end) {
        if (at == end) return 0;
        unsigned char r= *at;
        ++at;
        return r;
    }
    template<typename Iter, std::size_t n>
    static void load_bin(Iter &at, Iter end, std::array<unsigned char, n> &out) {
        for (std::size_t i = 0; i < n; ++i) {
            if (at != end) {
                out[i] = *at;
                ++at;
            }
        }
    }

};


class EventParseException: public std::exception {
public:
    enum Error {
        field_id,
        field_content,
        field_signature,
        field_created_at,
        field_tags,
        field_kind,
        field_pubkey,
        tag_mustbe_string,
        tag_value_mustbe_string,
        tagdef_isnot_array,
        invalid_id
    };
    EventParseException(Error err):_err(err) {}

    static std::string_view message(Error err);
    virtual const char *what() const noexcept override;
    Error get_error() const {return _err;}
protected:
    Error _err;
    mutable std::string msg;    
};

template<typename Iter, std::size_t sz>
Iter binary_from_hex(Iter beg, Iter end, std::array<unsigned char, sz> &out){
    std::size_t ofs = 0;
    while (beg != end && ofs < sz) {
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
        }
    }
    return beg;
};

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
    }
    return out;
}

template<typename OutIter, std::size_t sz>
OutIter binary_to_hex(const std::array<unsigned char, sz> &in, OutIter out) {
    return binary_to_hex(in.begin(), in.end(), out);
}

}



#endif