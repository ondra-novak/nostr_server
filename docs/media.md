# experimental NIP proposal 

## new commands

```
> ["FILE", <event>]
< ["OK",<event-id>,true/false,"desc"]
> <binary message>
< ["OK", "media:<hash>", true/false, "desc"]
```

```
> ["FETCH",<id>]
< ["FETCH", <id>, true/false, mime/error]
< <binary message>
```

```
> ["LINK",<id>],
< ["LINK",<id>, true/false, url/desc]
```



## media event

```
{
    "id": "<id>"
    "content": "media description"
    "kind":50,
    "tags":[
        ["h","hash"],
        ["title","name"],
        ["type","mime/type"],
        ["size","<size in bytes>"],
        ["width","<width>"],
        ["height","<height>"],
        ["dpi","<height>"],
        ["encrypted","<pubkey>"],
        ["expiration","<timestamp>"]
    ]
    "sig":"...",
}
```

* **content** - file description 
* **h** - (mandatory) file hash as HEX(SHA256(content))
* **type** - (mandatory) mime type
* **size** - (mandatory) file size in bytes
* **title** - file title
* **width** - image width in pixels
* **height** - image height in pixels
* **dpi** - dpi resolution
* **encrypted** - if present, file is encrypted using shared secred - pubkey as hex
* **expiration** - NIP-40

## error messages

```
max-size: <size>
```

Appears after "FILE" when file is too large - relay sends maximum size in bytes

```
file-mismatch: 
```

Appears after binary message, when received message doesn't match to anounced media. For example, hash
is different, message is larger, or different type.

```
file-forbidden:
```

This type of file is not allowed here

## protocol flow upload

* client sends ["FILE", <event>]
* relay responds by ["OK"] where it can accept or reject the file. In this state, the event
is not stored in the database and should not be searchable
* client sends binary message contains file content
* relay responds by ["OK","file:<hash>"] - so upload is complete or rejected

when FILE is sent, the event is held in connection's context. When binary message arrives, it
is matched against the FILE event and if there is a match, the event is stored in database along
with the file content.

If connection is closed before content arrives, the no event is saved, and anounced file is lost

Client can anounce multiple files without sending its content

## protocol flow download

* client sends ["FETCH",<hash>]
* relay responds ["FETCH",<hash>,<status>,<description>]
* when <status=true>. relay sends content as binary message

* status=true, file is ready, description=mime/type
* status=false, file is not ready, description=error message

## protocol flow link

* **NOTE** - LINK doesn't support encryption
* client sends ["LINK",<hash>]
* relay respons ["LINK",<hash>,<status>,<url/error>]
* status=true, fourth argument is url
* status=false, fourth argument is error

