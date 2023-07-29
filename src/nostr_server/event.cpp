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

    std::string_view tmp;
    tmp = s_id.as<std::string_view>();
    binary_from_hex(tmp.begin(), tmp.end(), ev.id);
    tmp = s_pubkey.as<std::string_view>();
    binary_from_hex(tmp.begin(), tmp.end(), ev.author);
    tmp = s_sig.as<std::string_view>();
    binary_from_hex(tmp.begin(), tmp.end(), ev.sig);
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
            evtag.additional_content.push_back(row[1].as<std::string>());
        }
        ev.tags.push_back(std::move(evtag));
    }
    ID cid = ev.calc_id();
    if (cid != ev.id) throw EventParseException(EventParseException::invalid_id);
    return ev;
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

std::string Event::toJSON() const
{
    IDHex id_str;
    SignatureHex sig_str;
    PubkeyHex pubkey_str;
    binary_to_hex(id,id_str.begin());
    binary_to_hex(sig, sig_str.begin());
    binary_to_hex(author, pubkey_str.begin());
    docdb::Structured ev = {
        {"content", std::string_view(content)},
        {"id", std::string_view(id_str.data(), id_str.size())},
        {"pubkey", std::string_view(pubkey_str.data(),pubkey_str.size())},
        {"sig", std::string_view(sig_str.data(), sig_str.size())},
        {"kind", kind},
        {"created_at", created_at}
    };
    ev.set("tags", build_tags(*this));
    return ev.to_json();
}

Event::ID Event::calc_id() const
{
    Event::PubkeyHex pubkey_str;
    binary_to_hex(author, pubkey_str.begin());
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
    return nullptr;
}
}