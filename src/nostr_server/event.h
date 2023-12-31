#pragma once
#ifndef SRC_NOSTR_SERVER_EVENT_H_
#define SRC_NOSTR_SERVER_EVENT_H_

#include "binary.h"

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


    using ID = Binary<32>;
    using IDHex = std::array<char, 64>;
    using Pubkey = Binary<32>;
    using PrivateKey = Binary<32>;
    using PubkeyHex = std::array<char, 64>;
    using Signature = Binary<64>;
    using SignatureHex = std::array<char, 128>;
    using Kind = unsigned int;
    using Depth = unsigned char;


    Kind kind = 0;
    std::string content;
    std::vector<Tag> tags;
    std::time_t created_at = 0;
    std::vector<std::uint16_t> tag_hash_map;

    ID id;
    Pubkey author;
    Signature sig;
    ///this event contains binary data NIP97 (search 'x' tag for hash)
    bool nip97 = false;
    ///this event was sent by someone who was authorized using AUTH and was found as local user
    bool trusted = false;
    ///contains reference depth -
    Depth ref_level = 0;

    static Event fromJSON(std::string_view json_text);
    static Event fromStructured(const docdb::Structured &json);
    std::string toJSON() const;
    docdb::Structured toStructured() const;
    ID calc_id() const;
    bool verify(const SignatureTools &sigtool) const;
    bool sign(const SignatureTools &sigtool, const PrivateKey &privkey);
    std::string get_tag_content(std::string_view tag) const {
        auto t = get_tag(tag);
        return t?t->content:std::string();
    }
    const Tag *get_tag(std::string_view tag) const {
        const Tag *r = nullptr;
        for (const Tag &t: tags) if (t.tag == tag) r = &t;
        return r;
    }
    Tag *get_tag(std::string_view tag) {
        Tag *r = nullptr;
        for (Tag &t: tags) if (t.tag == tag) r = &t;
        return r;
    }
    template<typename Fn>
    void for_each_tag(const std::string_view tag, Fn fn) const {
        for (const Tag &t: tags) if (t.tag == tag) fn(t);
    }
    void build_hash_map();
    const Tag *find_indexed_tag(char t, std::string_view content) const;
};

struct Attachment {
    using ID = Binary<32>;

    ID id;
    std::string data;
};

using EventOrAttachment = std::variant<Event, Attachment>;

struct EventDocument {
    using Srl = docdb::StructuredDocument<>;
    using Type = EventOrAttachment;
    template<typename Iter>
    static Iter to_binary(const EventOrAttachment &evatt, Iter out) {
        *out = static_cast<char>(evatt.index());
        if (std::holds_alternative<Event>(evatt)) {
            const Event &ev = std::get<Event>(evatt);
            out = Srl::string_to_binary((ev.nip97?0x80:0)|(ev.trusted?0x40:0),ev.content,out);
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
            *out++=ev.ref_level;
        } else {
            const Attachment &att = std::get<Attachment>(evatt);
            out = std::copy(att.id.begin(), att.id.end(), out);
            out = std::copy(att.data.begin(), att.data.end(), out);
        }
        return out;
    }
    template<typename Iter>
    static EventOrAttachment from_binary(Iter &at, Iter end) {
        unsigned char ex = get_extra(at,end);
        if (ex == 1) {
            EventOrAttachment out{Attachment{}};
            Attachment &att  = std::get<Attachment>(out);
            for (std::size_t i = 0; i < att.id.size() && at != end; i++) {
                att.id[i] = *at++;
            }
            att.data.resize(std::distance(at,end));
            std::copy(at,end,att.data.begin());
            at = end;
            return out;
        } else {
            EventOrAttachment out{Event{}};
            Event &ev = std::get<Event>(out);
            auto x = get_extra(at,end);
            ev.content = Srl::string_from_binary(x, at, end);
            ev.nip97 = (x & 0x80) != 0;
            ev.trusted = (x & 0x40) != 0;
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
            ev.ref_level = get_extra(at, end);
            assert(ev.calc_id() == ev.id);
            ev.build_hash_map();
            return out;
        }
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

using AttachmentLock = std::shared_ptr<Attachment::ID>;
using AttachmentWeakLock = std::weak_ptr<Attachment::ID>;


inline Attachment::ID get_attachment_id(const Event &ev) {
    auto s = ev.get_tag_content("x");
    return Attachment::ID::from_hex(s);
}


}



#endif
