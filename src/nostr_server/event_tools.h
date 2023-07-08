/*
 * event_tools.h
 *
 *  Created on: 18. 6. 2023
 *      Author: ondra
 */

#ifndef SRC_NOSTR_SERVER_EVENT_TOOLS_H_
#define SRC_NOSTR_SERVER_EVENT_TOOLS_H_
#include "publisher.h"

namespace nostr_server {


template<typename Fn>
void enum_tags(const Event &event, Fn fn) {
    const Event::Array &tags = event["tags"].array();
    for (const Event &t: tags) {
        const Event::Array &tarr = t.array();
        if (tarr.size() > 1 && tarr[0].contains<std::string>() && tarr[1].contains<std::string>()) {
            fn(tarr);
        }
    }
}

template<typename Fn>
const Event::Array *find_tag_if(const Event &event, Fn fn) {
    const Event::Array &tags = event["tags"].array();
    auto iter = std::find_if(tags.begin(), tags.end(), [&](const Event &t){
        const Event::Array &tarr = t.array();
        if (tarr.size() > 1 && tarr[0].contains<std::string>() && tarr[1].contains<std::string>()) {
            return fn(tarr);
        }
        return false;
    });
    if (iter == tags.end()) return nullptr;
    else return &iter->array();
}


inline Event create_event(unsigned int kind, std::string_view content, std::initializer_list<std::initializer_list<std::string_view> > tags) {
    Event::Array t;
    std::transform(tags.begin(), tags.end(), std::back_inserter(t), [&](const auto &tag) {
        Event::Array taginfo;
        std::transform(tag.begin(), tag.end(), std::back_inserter(taginfo), [&](const std::string_view &item) {
            return std::string(item);
        });
        return taginfo;
    });

    return {
            {"kind", kind},
            {"content",std::string(content)},
            {"tags", std::move(t)}
    };
}

}




#endif /* SRC_NOSTR_SERVER_EVENT_TOOLS_H_ */
