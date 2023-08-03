# experimental NIP proposal 

## new commands

```
c: ["FILE", <event>]
r: ["OK",<event-id>,true/false,"continue/error"]
c: <binary message>
r: ["FILE", "<hash>", true/false, "desc"]
```

```
c: ["FETCH",<file-id>]
r: ["FETCH",<id>, true/false, mime/error]
r: <binary message>
```

```
c: ["LINK",<id>],
r: ["LINK",<id>, true/false, url/desc]
```



## media event

```
{
    "id": "<id>"
    "content": "media description"
    "kind":<any kind>,
    "tags":[
        ["attachment","name"],
        ["x","SHA256 hash hex"],
        ["m","mime/type"],
        ["dim","dimensions"],
        ["size","<size in bytes>"],
        ["blurhash","...."],
        ["explicit","keyword"]
        ["expiration","<timestamp>"]
    ]
    "sig":"...",
}
```

* **content** - content of event (depend of kind)
* **attachment** - (mandatory) contains name of attachment, can be empty string, however
presence of this tag signals, that there is attachment, so client must read other tags to
find out other informations about the attachment
* **x** - (mandatory) contains SHA256 hash of the content as hex (NIP-94)
* **m** - (mandatory) contains mime type (NIP-94)
* **size** - (mandatory) file size in bytes (NIP-94)
* **dim** - dimensions (NIP-94)
* **blurhash** - NIP-94
* **expiration** - NIP-40
* **explicit** - if presented, the content is marked as excplicit and should be covered. The
value should contain keyword associated with kind of explicit content. Similar to NIP-56, `nudity`,`violence`,`religion`,`disgusting`,`nsfw`...



## protocol flow: upload

* client sends ["FILE", <event>]
* relay responds by ["OK"] where it can accept or reject the file. In this state, the event
is not stored in the database and should not be searchable
* client sends binary message contains file content
* relay responds by ["FILE","<hash>"] - so upload is complete or rejected

when FILE is sent, the event is held in connection's context. When binary message arrives, it
is matched against the FILE event and if there is a match, the event is stored in database along
with the file content.

If connection is closed before content arrives, the no event is saved, and anounced file is lost

Client can anounce multiple files without sending its content

## protocol flow: download

* client sends ["FETCH",<hash>]
* relay responds ["FETCH",<hash>,<status>,<description>]
* when <status=true>. relay sends content as binary message

* status=true, file is ready, description=mime/type
* status=false, file is not ready, description=error message

## protocol flow: link

* **NOTE** - LINK doesn't support encryption
* client sends ["LINK",<hash>]
* relay respons ["LINK",<hash>,<status>,<url/error>]
* status=true, fourth argument is url
* status=false, fourth argument is error

## error messages

* `r: ["OK",<event-id>,false,"max-size:<size>]` - request rejected, because file is too long. The relay also includes maximum allowed size of the file
* `r: ["FILE","<hash>",false,"file-mismatch:"]` - the relay received a binary message, which doesn't
match to any anounced files. For example, hash is different, file as incorrect size, etc


## encryption

* Files can by encrypted. Decryption is done on client side.
* Files attached to `kind:4` must be encrypted. File should be stored encrypted in format `<iv><encrypted-content>`. Shared secret (for kind:4) is used as a key

## media attached to kind:1

* client should render media (image) for kind:1 below the note

## standalone file

* use kind:1063 (NIP-94)

## reference to such a file 

* just link event of kind-1063 into a note


* 
