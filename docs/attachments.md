NIP-XXX
=======

Attachments
-----------

`draft` `optional` `author:ondra-novak`

This NIP solves the problem of sharing and distributing binary content over NOSTR networks such as media - images, short videos music, or in general any files up to a certain size

Key Takeaways
-------------

* Replaces all previous attempts to implement file sharing in NOSTR such a NIP-95 and NIP-96
* It's addresses requests for encrypted files in private messages
* Defines new commands at the protocol level ("ATTACH","FETCH","LINK")
* Defines a new tag "attachment"
* Uses the binary messages in the websocket connection
* This is an optional extension, the client can easily find out, that relay doesn't support this NIP




New tag "attachment"
-------------
The proposal introduces a new tag  "attachment" with the following format

```
["attachment","hash_hex","size_in_bytes","mime_type","optional(dimensions)","optional(blurhash)"]
```

* **hash_hex** - hexadecimal representation of SHA256 hash of the attached binary file. This string also serves as **attachment-id**
* **size_in_bytes** - size of the attachment in bytes
* **mime_type** - mime type of the attachment 
* **dimensions** - dimensions as defined in NIP-94, optional. 
* **blurhash** - NIP-94 blurhash, optional

* One event can have more than one of these tags, then it carries more attachments.
* Each relay defines the maximum attachment size and also the maximum number of attachments within an event
* Attachment can be attached to any "kind". Attachments for `kind:4` are always encrypted

The presence of the `attachment` attribute only informs the client that the relay should also have its own attachment content available in binary form. Therefore, the client should prepare for the visualization of the post, taking into account that attachments are available.


Upload
------
Events must be published simultaneously with attachments. It is necessary to prevent the client from publishing the event but not uploading the appropriate attachments. Similary, it shouldn't be possible to upload attachments first and then publish the event that refers to them. The operation must be atomic

For this reason, a new command is introduced: "ATTACH"

**Protocol flow**

```
client: ["ATTACH", <event>]
relay:  ["OK","<event-id">,true,"continue"]
client: <binary message for first attachment>
relay:  ["ATTACH",<attachment-id>, true, ""]
client: <binary message for second attachment>
relay:  ["ATTACH",<attachment-id>, true, ""]
...
...
client: <binary message for last attachment>
relay:  ["ATTACH",<attachment-id>, true, ""]
     //event is published
```

If the relay does not support this NIP, it will not be able to respond to the new command, or it will respond with "NOTICE". If the relay does not respond to the command within a certain time (e.g. 10 seconds), then the client should verify that the relay responds to other commands (for example "REQ") and evaluate the situation as if the relay does not support the functionality. If the relay responds to the command with a "NOTICE" response, the client must assume that the relay does not support the function and try another method

The protocol flow is designed in such a way that it is possible to post other commands between individual phases. In general, the relay should respond to a binary message by calculating its SHA256 hash and checking whether there is an open request to add an event with an attachment.

* **when ATTACH command is received on the relay** - check validity of the event, prepare to receive attacahment (prepare metadata)
* **when binary message is received on the relay** - calculate hash, find prepared metadata for the attachment, if all attachments are stored, publish the event.
* **when connection is closed before completion** - discard any prepared metadata and delete already stored attachments

Errors - ATTACH
-----------------------------
```
relay:  ["OK","<event-id">,false,"error code: description"]
```
When an error occurs after an ATTACH command, the client must not send any binary messages

* **max size: <number>** - one of the attachments exceeded the maximum size allowed, the relay sends this limit as a number in the error description section
* **max count: <number>** - the number of attachments has exceeded the allowed limit. Again, the relay sends this limit as a number in the error description section
* **malformed: ** - event is malformed, missing mandatory fields, or event doesn't have attachments

Errors- binary messages
--------------------------------------
```
relay:  ["ATTACH","<attachment-id">,false,"error code: description"]
```
Relay must respond to each binary message with a ["ATTACH"] response. The response has the same format as "OK"

* **invalid_attachment:** -  The sent binary message doesn't match any expected attachment. For example, the hash or size doesn't match.


Encryption - kind:4
-------------------

Attachments are sent encrypted for "kind:4". The same encryption is used as in the encryption of the message itself. An initialization vector (16 bytes) is stored at the beginning of the binary message and then the binary content itself.

```
[IV 16 bytes][ encrypted content ]
```

Download attachment
-------------------

The "FETCH" command is used to download the attachment from the relay

**Protocol flow**

```
client: ["FETCH","<attachment-id>"]
relay:  ["FETCH","<attachment-id>", true, "mime/type"]
relay:  <binary message>
```

If the attachment does not exist, the response looks like this


```
client: ["FETCH","<attachment-id>"]
relay:  ["FETCH","<attachment-id>", false, "missing: not found"]
```
In this case, the relay must not generate a binary message

Encrypted attachments must be decrypted at client side.


Getting the URL for the attachment (optional)
----------------------------------

If it is more convenient for the client to have an URL to the given attachment available instead of processing the binary message, then it can use the LINK command

```
client: ["LINK","<attachment-id>"]
relay:  ["LINK","<attachment-id>",true,"https://...."]
```

If the attachment does not exist, the response looks like this

```
client: ["LINK","<attachment-id>"]
relay:  ["LINK","<attachment-id>", false, "missing: not found"]
```


Changes in relay information document (NIP-11)
-----------------------------------------------

New items are added in the "limitation" section

```
 "limitation": {
    "attachment_max_count": 4,
    "attachment_max_size": 262144,
  }
```

* **attachment_max_count** - maximum count of attachments per event
* **attachment_max_size** -- maximum size of single attachment in bytes

Garbage collecting
------------------

Relay should perform garbage collecting of attachments without any reference on events.


FAQ
---

**Why binary messages. NOSTR is not designed to transmit binary messages**

I believe this is simply an excuse by people who have become comfortable with the status quo. That's why they are inventing special ways of encoding messages to "pass" binary content without taking full advantage of the potential of the technology that was chosen for NOSTR.

WebSocket was finalized into RFC 6455 in 2011. Now we are in 2023, 12 years later, programmers are afraid to use binary messages to transfer binary content. It's about on par with some email systems still relying on 7-bit Internet.

Introducing the transfer of binary content using binary messages solves a lot of issues. And if there are developers who use languages or libraries that do not support binary messages in websocket, please consider changing the library or even changing the language. Binary messages are supported by javascript in every browser.

**The advantage of NIP-95 is that it does not need a special relay implementation. This NIP requires massive development on both the relay and client side**

That's not true. If you read NIP-95 carefully, there are several requirements for how the relay should handle binary content - which is stored as base64. These are not trivial requirements. For example, the actual binary content is transmitted as an event, but it must not be indexed. This means adding a bunch of exceptions to the indexes.

If I want to have some control over the binary content, as a relay developer I have to do more programming, which I have to do in this proposal anyway. Plus some things are more difficult to deal with than using binary messages. For example, I have to allow the same interface for presenting normal events and the same for presenting NIP-95 events, yet handle them diametrically differently.

Relay also cannot dictate the size of the binary content. There is a limit on the size of each kind, but clients don't examine this much and most of them react to the error message by simply not allowing the operation. For example, a client uploading a large image that the relay rejects might reduce the resolution or compression quality of the image to fit to the relay's limits. This is the motivation behind this proposal.

An unresolved issue relates to garbage collection. If a reference to a shared binary content disappears, does the relay have the right to delete this content? If I take an ordinary relay that does not implement NIP-95, then these events stay there - forever. What is the correct behavior?

**Binary content should be transferred using the HTTP protocol see NIP-96**

No, it didn't. We're mixing protocols. What if the relay is not an HTTP server? Yes this can happen, an http server can be actually an upstream proxy. The NIP-96 design is also very complex

**What if a client sends an event with the tag "attachment" but sends it via the EVENT command?**

Every client should be prepared for a situation where the attachment is not stored on the relay. The `FETCH` command will simply fail. That will happen in this case as well. So the event will be published but the attachments will not be visible

**How a client that does not support this NIP work?**

This client can't see the attachments, only the `content` of the event

**What if two clients upload the same attachment (how to resolve hash collisions)**

The proposal assumes that collisions of hashes with different content will not occur. If someone succeeds, then they are very lucky. In general, a relay can overwrite a file under the same hash if it is uploaded multiple times, or alternatively, it can keep the original file and discard the new upload (and act as if the upload was successful)

**In this NIP, the files are not signed, it is not possible to verify who sent them to the relay. NIP-95, on the other hand, also signs the file**

The hash is always signed, not the content. In the case of NIP-95, the hash includes the author's pubkey, whereas in this proposal only the file hash itself is signed. This allows sharing the same file without reuploading it. And if you are worried about someone "stealing" ownership of the file. That may be the case with NIP-95, no signature will prevent a copy&paste operation. The only way to protect the file is to encrypt it

**Why the tags do not have the form proposed in NIP-94**

That was originally considered. I liked the compatibility with NIP-94. It would be possible to link the file directly to the metadata above NIP-94. But this proposal would not allow to attach multiple attachments to a single event. The motivation was to have it like Twitter, where there can be up to 4 images. Or like in Reels, where you can have short videos, but they can be linked into a longer continuous video. You can also implement a form of streaming longer videos in this way

**Images in Kind:0, image placement in text, smileys, etc.**

Just as I can place anything from tags using #[index], I can reference an attachment this way

**Why does it have to be uploaded atomically?**

This is related to the garbage collection requirement. It could happen that the client uploads attachments, but before publishing the event, the garbage collector comes and because the new attachments don't have a reference, it deletes them

