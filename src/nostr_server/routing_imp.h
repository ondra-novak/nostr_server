#pragma once
#ifndef SRC_NOSTR_SERVER_ROUTING_IMP_H_
#define SRC_NOSTR_SERVER_ROUTING_IMP_H_

#include "routing.h"

namespace nostr_server {

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
                        relay = t.additional_content[0];
                        emit({relay, Event::Pubkey::from_hex(t.content)}, ev.ref_level);
                    }
                }
            });
            if (ev.kind == kind::Contacts && !ev.content.empty()) {
                try {
                    auto json =docdb::Structured::from_json(ev.content);
                    auto &obj = json.keypairs();
                    for (const auto &[key, value]: obj) {
                        if (value["write"].as<bool>()) {
                            emit({key, ev.author}, ev.ref_level);
                        }
                    }
                } catch (...) {

                }
            }
            break;
        case kind::Relay_List_Metadata:
            ev.for_each_tag("r", [&](const Event::Tag &t){
                if (!t.additional_content.empty() && t.additional_content[0] == "read") return;
                emit({t.content, ev.author}, ev.ref_level);
            });
            break;
        default:
            break;

    }



}


}





#endif /* SRC_NOSTR_SERVER_ROUTING_IMP_H_ */
