#pragma once
#ifndef SRC_NOSTR_SERVER_ATTACHMENTS_H_
#define SRC_NOSTR_SERVER_ATTACHMENTS_H_
#include "event.h"

#include <algorithm>
#include <memory>
#include <map>
#include <optional>
namespace nostr_server {



class AttachmentUploadControl {
public:

    enum Status {
        ok,
        malformed,
        max_attachments,
        max_size,
        invalid_mime,
        invalid_size,
        invalid_hash,
    };

    struct AttachmentMetadata {
        bool valid = false;
        Attachment::ID id = {};
        std::string mime = {};
    };

    AttachmentUploadControl(unsigned int max_attachments, std::size_t max_size);

    Status register_event(Event &&ev);
    AttachmentMetadata check_binary_message(std::string_view msg) const;
    bool attachment_published(const AttachmentLock &lock);

    template<typename Fn>
    void flush_events_to_publish(Fn &&fn);

protected:

    unsigned int _max_attachments;
    std::size_t _max_size;

    struct Info {
        Attachment::ID id;
        std::size_t size;
        std::string mime;
        AttachmentLock lock;
    };

    using IDView = std::basic_string_view<unsigned char>;

    std::vector<Event> _events;
    std::map<IDView, std::unique_ptr<Info> > _attmap;

    std::vector<std::unique_ptr<Info> > _tmp_infos;



};


template<typename Fn>
inline void AttachmentUploadControl::flush_events_to_publish(Fn &&fn) {
    auto end = _events.end();
    bool something_done = false;
    auto new_end = std::remove_if(_events.begin(), _events.end(), [&](Event &ev){

        bool ok = true;
        ev.for_each_tag("attachment", [&](const Event::Tag &tag){
            auto attid = Attachment::ID::from_hex(tag.content);
            auto iter = _attmap.find({attid.data(), attid.size()});
            if (iter == _attmap.end() || iter->second->lock == nullptr) ok = false;
        });
        if (ok) {
            fn(ev);
            something_done = true;
            return true;
        } else {
            return false;
        }


    });
    if (something_done) {
        _events.erase(new_end, end);

        std::map<IDView, std::unique_ptr<Info> > ip;
        for (const Event &ev: _events) {
            ev.for_each_tag("attachment", [&](const Event::Tag &tag){
                auto attid = Attachment::ID::from_hex(tag.content);
                auto iter = _attmap.find({attid.data(), attid.size()});
                if (iter != _attmap.end()) {
                    ip.emplace(iter->first, std::move(iter->second));
                    _attmap.erase(iter);
                }
            });
        }

        std::swap(ip, _attmap);
    }
}

}
#endif /* SRC_NOSTR_SERVER_ATTACHMENTS_H_ */
