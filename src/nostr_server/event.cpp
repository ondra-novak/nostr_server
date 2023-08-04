#include "event.h"
#include "signature.h"
#include <docdb/structured_document.h>
#include <docdb/json.h>
#include <openssl/sha.h>
#include <coroserver/static_lookup.h>

namespace nostr_server {

Event Event::fromJSON(std::string_view json_text)
{
    auto sevent = docdb::Structured::from_json(json_text);
    return fromStructured(sevent);
}
Event Event::fromStructured(const docdb::Structured &sevent) {

    const auto &s_id = sevent["id"];
    const auto &s_pubkey = sevent["pubkey"];
    const auto &s_content = sevent["content"];
    const auto &s_created_at = sevent["created_at"];
    const auto &s_kind = sevent["kind"];
    const auto &s_tags = sevent["tags"];
    const auto &s_sig = sevent["sig"];

    if (!s_id.contains<std::string_view>()) throw EventParseException(EventParseException::field_id);
    if (!s_content.contains<std::string_view>()) throw EventParseException(EventParseException::field_content);
    if (!s_sig.contains<std::string_view>()) throw EventParseException(EventParseException::field_signature);
    if (!s_created_at.contains<std::time_t>()) throw EventParseException(EventParseException::field_created_at);
    if (!s_kind.contains<unsigned int>()) throw EventParseException(EventParseException::field_kind);
    if (!s_pubkey.contains<std::string_view>()) throw EventParseException(EventParseException::field_pubkey);
    if (s_tags.defined() && !s_tags.contains<docdb::Structured::Array>()) throw EventParseException(EventParseException::field_tags);
    Event ev;

    ev.id = Event::ID::from_hex(s_id.as<std::string_view>());
    ev.author = Event::Pubkey::from_hex(s_pubkey.as<std::string_view>());
    ev.sig = Event::Signature::from_hex(s_sig.as<std::string_view>());
    ev.content.append(s_content.as<std::string_view>());
    ev.created_at = s_created_at.as<std::time_t>();
    ev.kind = s_kind.as<unsigned int>();
    const auto &tags = s_tags.array();
    for (const auto &s_row: tags) {
        if (!s_row.contains<docdb::Structured::Array>()) throw EventParseException(EventParseException::tagdef_isnot_array);
        const auto &row = s_row.array();
        if (row.size() < 2) throw EventParseException(EventParseException::tagdef_isnot_array);
        const auto &s_tagid = row[0];
        const auto &s_value = row[1];
        Tag evtag;
        if (!s_tagid.contains<std::string_view>()) throw EventParseException(EventParseException::tag_mustbe_string);
        if (!s_value.contains<std::string_view>()) throw EventParseException(EventParseException::tag_value_mustbe_string);
        evtag.tag.append(s_tagid.as<std::string_view>());
        evtag.content.append(s_value.as<std::string_view>());
        for (std::size_t i = 2; i < row.size(); ++i) {
            evtag.additional_content.push_back(row[i].as<std::string>());
        }
        ev.tags.push_back(std::move(evtag));
    }
    ID cid = ev.calc_id();
    if (cid != ev.id) throw EventParseException(EventParseException::invalid_id);
    ev.build_hash_map();
    return ev;
}

static std::size_t tag_hash(char t, std::string_view content) {
    std::hash<std::string_view> hasher;
    return hasher(content) * 31 + t;
}


void Event::build_hash_map() {
    tag_hash_map.clear();
    tag_hash_map.resize(tags.size()*2+1,0);
    for (const auto &t: tags) {
        if (t.tag.size() != 1) continue;
        std::size_t idx = tag_hash(t.tag[0],t.content) % tag_hash_map.size();
        while (tag_hash_map[idx]) {
            idx = (idx+1) % tag_hash_map.size();
        }
        tag_hash_map[idx] = &t - tags.data() + 1;
    }
}

const Event::Tag *Event::find_indexed_tag(char t, std::string_view content) const {
    if (tag_hash_map.empty()) return nullptr;
    auto idx = tag_hash(t,content) % tag_hash_map.size();;
    while (tag_hash_map[idx]) {
        auto f = tag_hash_map[idx];
        const auto &tr = tags[f-1];
        if (tr.tag[0] == t && tr.content == content) return &tr;
        idx = (idx+1) % tag_hash_map.size();
    }
    return nullptr;
}


docdb::Structured::Array build_tags(const Event &ev) {
    docdb::Structured::Array tags_arr;
    tags_arr.reserve(ev.tags.size());
    for (const auto &t: ev.tags) {
        docdb::Structured::Array row;
        row.reserve(t.additional_content.size()+2);
        row.push_back(std::string_view(t.tag));
        row.push_back(std::string_view(t.content));
        for (const auto &x: t.additional_content) {
            row.push_back(std::string_view(x));
        }
        tags_arr.push_back(std::move(row));
    }
    return tags_arr;
}

std::string Event::toJSON() const {
    return toStructured().to_json();
}
docdb::Structured Event::toStructured() const
{
    IDHex id_str = id;
    SignatureHex sig_str = sig;
    PubkeyHex pubkey_str = author;
    docdb::Structured ev = {
        {"content", std::string_view(content)},
        {"id", std::string(id_str.data(), id_str.size())},
        {"pubkey", std::string(pubkey_str.data(),pubkey_str.size())},
        {"sig", std::string(sig_str.data(), sig_str.size())},
        {"kind", kind},
        {"created_at", created_at}
    };
    ev.set("tags", build_tags(*this));
    return ev;
}

Event::ID Event::calc_id() const
{
    Event::PubkeyHex pubkey_str = author;
    docdb::Structured eventToSign = {
        0,
        std::string_view(pubkey_str.data(), pubkey_str.size()),
        created_at,
        kind,
        build_tags(*this),
        std::string_view(content)
    };
    std::string eventData = eventToSign.to_json(docdb::Structured::flagUTF8);
    Event::ID hash;
    SHA256(reinterpret_cast<const uint8_t*>(eventData.data()), eventData.size(), hash.data());
    return hash;
}

bool Event::verify(const SignatureTools &sigtool) const
{
    return sigtool.verify(id,author,sig);
}

bool Event::sign(const SignatureTools &sigtool, const SignatureTools::PrivateKey &privkey)
{
    if (created_at == 0) {
        created_at = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    }
    id = calc_id();
    return sigtool.sign(privkey,id,sig,author);
}

constexpr auto event_errors = coroserver::makeStaticLookupTable<EventParseException::Error, std::string_view>({
    {EventParseException::field_content,"Field 'content' missing or is not string"},
    {EventParseException::field_id,"Field 'id' missing or is not string"},
    {EventParseException::field_created_at,"Field 'create_at' missing or is not an unsigned number"},
    {EventParseException::field_signature,"Field 'signature' missing or is not string"},
    {EventParseException::field_tags,"Field 'tags' is not an array"},
    {EventParseException::field_kind,"Field 'kind' missing or is not an unsigned number"},
    {EventParseException::field_pubkey,"Field 'pubkey' missing or is not string"},
    {EventParseException::tag_mustbe_string,"Tag must be non-empty string"},
    {EventParseException::tag_value_mustbe_string,"Tag value must be string"},
    {EventParseException::tagdef_isnot_array,"Tag definition must be non-empty array with at least 2 values"},
    {EventParseException::invalid_id,"Invalid event id"},
});


std::string_view EventParseException::message(Error err)
{
    return event_errors[err];
}

const char *EventParseException::what() const noexcept
{
    if (msg.empty()) msg = std::string(message(_err));
    return msg.c_str();
}
}
