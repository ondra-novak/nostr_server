#include "attachments.h"

#include <openssl/sha.h>

namespace nostr_server {

AttachmentUploadControl::AttachmentUploadControl(std::size_t max_attachments, std::size_t max_size)
:_max_attachments(max_attachments)
, _max_size(max_size)
{
}


AttachmentUploadControl::EventStatus AttachmentUploadControl::on_attach(Event &&ev) {

    _attachments.clear();
    _event.reset();

    try {
        //validation section
        ev.for_each_tag("attachment", [&](const Event::Tag &tag) {
            if (tag.content.size() != 64) throw invalid_hash;
            if (tag.additional_content.size()<2) throw malformed;
             char *p;
            std::size_t sz= std::strtoull(tag.additional_content[0].c_str(),&p,10);
            if (p != tag.additional_content[0].c_str()+tag.additional_content[0].size()) throw invalid_size;
            if (sz == 0) throw invalid_size;
            if (sz >_max_size) throw max_size;
            auto sep = tag.additional_content[1].find('/');
            if (sep == std::string::npos || sep == 0 || sep == tag.additional_content[1].size()-1) throw invalid_mime;
            //add attachment
            _attachments.push_back(Info{
                Attachment::ID::from_hex(tag.content),
                sz,
                tag.additional_content[1],
                {}
            });
        });
        if (_attachments.empty()) return no_attachments;
        if (_attachments.size() > _max_attachments) return max_attachments;
    } catch (const EventStatus &st) {
        //in case of exception - reset state
        _attachments.clear();
        return st;
    }
    _event.emplace(std::move(ev));
    return ok;
}

AttachmentUploadControl::AttachmentMetadata AttachmentUploadControl::on_binary_message(
        std::string_view msg) const {
    Attachment::ID hash;
    //create hash
    SHA256(reinterpret_cast<const unsigned char *>(msg.data()), msg.size(), hash.data());
    //find hash
    auto iter = std::find_if(_attachments.begin(), _attachments.end(), [&](const Info &att){
        return att.lock == nullptr && att.id == hash;
    });
    //not found?
    if (iter == _attachments.end()) return {false,hash};
    //different size?
    if (iter->size != msg.size()) return {false,hash};
    //found
    return {true, hash, iter->mime};


}

std::optional<Event> AttachmentUploadControl::get_event() const {
    return _event;
}

std::optional<Event>& AttachmentUploadControl::get_event() {
    return _event;
}

AttachmentUploadControl::AttachmentStatus AttachmentUploadControl::attachment_published(const AttachmentLock &lock) {
    //find attachment to lock
    auto iter = std::find_if(_attachments.begin(), _attachments.end(), [&](const Info &att){
        return att.lock == nullptr && att.id == *lock;
    });
    //not found?
    if (iter == _attachments.end()) return mismatch;
    iter->lock = std::move(lock);

    //find unresolved attachment
    iter = std::find_if(_attachments.begin(), _attachments.end(), [&](const Info &att){
        return att.lock == nullptr;
    });
    //status depend on result of search
    return iter == _attachments.end()?complete:need_more;
}

void AttachmentUploadControl::reset() {
    _event.reset();
    _attachments.clear();
}


}


