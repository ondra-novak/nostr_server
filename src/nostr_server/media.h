#pragma once
#ifndef SRC_NOSTR_SERVER_MEDIA_H_
#define SRC_NOSTR_SERVER_MEDIA_H_

#include "binary.h"
#include <docdb/structured_document.h>
#include <docdb/map.h>

#include <string>
namespace nostr_server {

struct MediaType {
    std::string mime;
    std::string content;
};

struct MediaDocument {
    using Srl = docdb::StructuredDocument<>;
    using Type = MediaType;
    template<typename Iter>
    static Iter to_binary(const Type &m, Iter outiter) {
       outiter = Srl::string_to_binary(0, m.mime, outiter);
       return std::copy(m.content.begin(),m.content.end(), outiter);
    }
    template<typename Iter>
    static MediaType from_binary(Iter &at, Iter end) {
       unsigned char flg = *at;
       ++at;
       std::string mime = Srl::string_from_binary(flg, at, end);
       std::string content;
       std::size_t sz = std::distance(at, end);
       content.resize(sz);
       std::copy(at,end, content.begin());
       at = end;
       return {std::move(mime), std::move(content)};
    }
};

class MediaStorage : public docdb::Map<MediaDocument> {
public:
    using docdb::Map<MediaDocument>::Map;
};



}



#endif /* SRC_NOSTR_SERVER_MEDIA_H_ */
