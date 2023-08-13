#pragma once
#ifndef SRC_NOSTR_SERVER_ROUTING_IMP_H_
#define SRC_NOSTR_SERVER_ROUTING_IMP_H_

#include "routing.h"

namespace nostr_server {

std::string_view RoutingIndexFn::adjust_relay_url(std::string_view url) {
    if (url.compare(0,5,"ws://") && url.compare(0,6,"wss://")) return {};
    auto p1 = url.find("://");
    if (p1 == url.npos) return {};
    auto p2 = url.find("/", p1+3);
    if (p2 == url.npos) return url;
    if (p2 == url.size()-1) return url.substr(0, url.size()-1);
    return url;
}

template<typename Emit>
void RoutingIndexFn::operator ()(Emit emit, const EventOrAttachment &evatt) const {

    if (!std::holds_alternative<Event>(evatt)) return;
    const Event &ev = std::get<Event>(evatt);
    switch(ev.kind) {
        case kind::Short_Text_Note:
        case kind::Reaction:
        case kind::Contacts:
            ev.for_each_tag("p", [&](const Event::Tag &t){
                if (t.content.size() == 64) {
                    std::string_view relay;
                    if (!t.additional_content.empty()) {
                        relay = adjust_relay_url(t.additional_content[0]);
                    }
                    emit({relay, Event::Pubkey::from_hex(t.content)}, ev.ref_level);
                }
            });
            if (ev.kind == kind::Contacts && !ev.content.empty()) {
                try {
                    auto json =docdb::Structured::from_json(ev.content);
                    auto &obj = json.keypairs();
                    for (const auto &[key, value]: obj) {
                        if (value["write"].template as<bool>()) {
                            auto url = adjust_relay_url(key);
                            if (!url.empty()) emit({adjust_relay_url(key), ev.author}, ev.ref_level);
                        }
                    }
                } catch (...) {

                }
            }
            break;
        case kind::Relay_List_Metadata:
            ev.for_each_tag("r", [&](const Event::Tag &t){
                if (!t.additional_content.empty() && t.additional_content[0] == "read") return;
                auto url = adjust_relay_url(t.content);
                if (!url.empty()) emit({url, ev.author}, ev.ref_level);
            });
            break;
        default:
            break;

    }



}


}





#endif /* SRC_NOSTR_SERVER_ROUTING_IMP_H_ */
