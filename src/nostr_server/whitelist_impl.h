#include "whitelist.h"

namespace nostr_server {

template<typename Emit>
void WhiteListIndexFn::operator ()(Emit emit, const EventOrAttachment &evatt) const {

    if (!std::holds_alternative<Event>(evatt)) return;
    const Event &ev = std::get<Event>(evatt);

    if (ev.ref_level) return;

    auto update_counter = [&](unsigned int (Karma::*val)) {
            ev.for_each_tag("p",[&](const Event::Tag &t){
                auto pubkey = Event::Pubkey::from_hex(t.content);
                auto v = emit(pubkey);
                if constexpr(emit.erase) {
                    if (v) {
                        Karma k = *v;
                        --(k.*val);
                        v.put(k);
                    }
                } else {
                    Karma k;
                    if (v) k = *v;
                    ++(k.*val);
                    v.put(k);
                }
            });
    };
    if (ev.kind < kind::Encrypted_Direct_Messages && ev.ref_level == 0)
    {
        if constexpr(!emit.erase) {
            auto v = emit(ev.author);
            if (!v) {
                Karma k;
                k.local = true;
                v.put(k);
            } else if (!v->local) {
                v->local = true;
                v.put(*v);
            }
        }
    } else {
        auto v = emit(ev.author);
        if (!v || !v->local) return;
    }

    switch (ev.kind) {
        case kind::Short_Text_Note: update_counter(&Karma::mentions);break;
        case kind::Contacts: update_counter(&Karma::followers);break;
        case kind::Encrypted_Direct_Messages: update_counter(&Karma::directmsgs);break;
        case kind::Mute_List: update_counter(&Karma::mutes);break;
        default:break;
    }
}


}
