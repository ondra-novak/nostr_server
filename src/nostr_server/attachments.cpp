#include "attachments.h"

#include <openssl/sha.h>

namespace nostr_server {

AttachmentUploadControl::AttachmentUploadControl(unsigned int max_attachments,
        std::size_t max_size)
:_max_attachments(max_attachments)
, _max_size(max_size)
{
}


AttachmentUploadControl::Status AttachmentUploadControl::register_event(Event &&ev) {

    try {
        ev.for_each_tag("attachment", [&](const Event::Tag &tag) {
            //<hash,size,mime
            if (tag.content.size() != 64) throw invalid_hash;
            if (tag.additional_content.size()<2) throw malformed;
             char *p;
            std::size_t sz= std::strtoull(tag.additional_content[0].c_str(),&p,10);
            if (p != tag.additional_content[0].c_str()+tag.additional_content[0].size()) throw invalid_size;
            if (sz == 0) throw invalid_size;
            if (sz >_max_size) throw max_size;
            auto sep = tag.additional_content[1].find('/');
            if (sep == std::string::npos || sep == 0 || sep == tag.additional_content[1].size()-1) throw invalid_mime;
            _tmp_infos.push_back(std::make_unique<Info>(Info{
                Attachment::ID::from_hex(tag.content),
                sz,
                tag.additional_content[1],
                {}
            }));
        });
    } catch (const Status &st) {
        return st;
    }

    if (_tmp_infos.empty()) return malformed;
    if (_tmp_infos.size() > _max_attachments) return max_attachments;
    for (auto &x: _tmp_infos) {
        _attmap.emplace(IDView{x->id.data(),x->id.size()}, std::move(x));
    }
    _tmp_infos.clear();
    _events.push_back(std::move(ev));
    return ok;
}

AttachmentUploadControl::AttachmentMetadata AttachmentUploadControl::check_binary_message(
        std::string_view msg) const {
    Attachment::ID hash;
    SHA256(reinterpret_cast<const unsigned char *>(msg.data()), msg.size(), hash.data());
    auto iter = _attmap.find({hash.data(), hash.size()});
    if (iter == _attmap.end()) return {false,hash};
    if (iter->second->size != msg.size()) return {false,hash};
    return {true, hash, iter->second->mime};


}

bool AttachmentUploadControl::attachment_published(const AttachmentLock &lock) {
    auto iter = _attmap.find({lock->data(), lock->size()});
    if (iter != _attmap.end()) {
        iter->second->lock = lock;
        return true;
    } else {
        return false;
    }

}

}


