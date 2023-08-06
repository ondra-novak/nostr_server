#pragma once
#ifndef SRC_NOSTR_SERVER_ATTACHMENTS_H_
#define SRC_NOSTR_SERVER_ATTACHMENTS_H_
#include "event.h"

#include <algorithm>
#include <memory>
#include <map>
#include <optional>
namespace nostr_server {



///Contains state of ATTACH command, tracks already received attachments
class AttachmentUploadControl {
public:

    enum EventStatus {
        ///event received and validated
        ok,
        ///event is malformed
        malformed,
        ///too many attachments
        max_attachments,
        ///one attachment is too large
        max_size,
        ///invalid mime type
        invalid_mime,
        ///invalid size
        invalid_size,
        ///invalid hash
        invalid_hash,
        ///event has no attachments
        no_attachments
    };


    enum AttachmentStatus {
        ///Accepted, but need more attachments
        need_more,
        ///Accepted and this was last required attachment
        complete,
        ///Rejected, attachment mismatch
        mismatch
    };

    struct AttachmentMetadata {
        ///contains true, if binary message is valid attachment
        bool valid = false;
        ///contains attachment ID
        Attachment::ID id = {};
        ///contains attachment's mime type
        std::string mime = {};
    };

    ///Constructs state object
    AttachmentUploadControl(std::size_t max_attachments, std::size_t max_size);

    ///Called on ATTACH command,
    /**
     * @param ev the event which was carried with the attach command
     * @return status of validation see EventStatus
     */
    EventStatus on_attach(Event &&ev);

    ///Called on binary message
    /**
     * This object doesn't publish the binary message, it only extracts metadata,
     *
     * @param msg binary message
     * @return extracted metadata if message is attachment
     */
    AttachmentMetadata on_binary_message(std::string_view msg) const;

    ///Must be called after attachment is stored
    /**
     * @param lock lock object which is result of storing the attachment. This
     * prevents GC to scarp the attachment from the database
     * @return AttachmentStatus
     */
    AttachmentStatus attachment_published(const AttachmentLock &lock);
    ///Retrieve event
    std::optional<Event> get_event() const;
    ///Retrieve event
    std::optional<Event> &get_event();

    ///Reset the state, unlock all locked attachments
    void reset();

protected:

    std::size_t _max_attachments;
    std::size_t _max_size;

    std::optional<Event> _event;
    struct Info {
        Attachment::ID id;
        std::size_t size;
        std::string mime;
        AttachmentLock lock;
    };

    std::vector<Info> _attachments;

};


}
#endif /* SRC_NOSTR_SERVER_ATTACHMENTS_H_ */
