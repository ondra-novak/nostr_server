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
* Defines new commands at the protocol level ("ATTACH","FETCH")
* Defines a new tag "attachment"
* Uses the binary messages in the websocket connection
* This is an optional extension, the client can easily find out, that relay doesn't support this NIP


New tag "attachment"
-------------
The proposal introduces a new tag  "attachment" with the following format

```
["attachment","hash_hex","size_in_bytes","mime_type","features1", "feature2", ....]
```

* **hash_hex** - hexadecimal representation of SHA256 hash of the attached binary file. This string also serves as **attachment-id**
* **size_in_bytes** - size of the attachment in bytes
* **mime_type** - mime type of the attachment 
* **features...** - optional fields with various features in format `<field>=<value>`
    * **blurhash=** - contains NIP-94 blurhash
    * **purpose=** - purpose of the attachment in context of current note. This can help client
to select optimal rendering. Suggested purposes
        * **thumbnail** - thubnail image of a following video
        * **story** - all attachments marked by this purpose are played as a story
        * **inline** - attachment is rendered in text, referenced by `#[<index>]`. This purpose hides this attachment from the list of attachments in note's detail.

```
["attachment","12887...","64000","image/jpeg","blurhash=qwdjq3...", "purpose=thumbnail"]
["attachment","874788...","2258147","video/mpeg4", "purpose=story"]
["attachment","ae3c758...","2158799","video/mpeg4", "purpose=story"]
["attachment","788eq78...","254123","image/jpeg", "purpose=story"]
```
        

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
relay:  ["ATTACH",<attachment-id>, true, "continue"]
client: <binary message for second attachment>
relay:  ["ATTACH",<attachment-id>, true, "continue"]
...
...
client: <binary message for last attachment>
relay:  ["ATTACH",<attachment-id>, true, "complete"]
     //event is published
```

The protocol flow is designed in such a way that it is possible to post other commands between individual phases. Possible implementation on a relay:

```
- on ATTACH command
   - validate the event
   - store the event in a temporary storage associated with current connection

- on binary message
   - calculate SHA256 hash of the binary message
   - find matching attachment in list of attachment of events stored in the temporary storage
   - store the binary message as new attachment in the temporary storage
   - check if all attachments have been uploaded
        - is so, publish the stored event and attachments and delete them from temporary storage

 - on connection closed before completion
   - destroy temporary storage (no publish)
   
```


**NOTE** If the relay does not support this NIP, it will not be able to respond to the new command, or it will respond with "NOTICE". If the relay does not respond to the command within a certain time (e.g. 10 seconds), then the client should verify that the relay responds to other commands (for example "REQ") and evaluate the situation as if the relay does not support the functionality. If the relay responds to the command with a "NOTICE" response, the client must assume that the relay does not support the function and try another method

Response - ATTACH
--------------------------------------

Response for ATTACH is similar as response to EVENT. It uses "OK" response.

Errors - ATTACH
-----------------------------
When an error occurs after an ATTACH command, the client must not send any binary messages in this case.

```
relay:  ["OK","<event-id">,false,"max_attachment_size: <number>"]
```

one of the attachments exceeded the maximum size allowed, the relay sends this limit as a number in the error description section

```
relay:  ["OK","<event-id">,false,"max_attachment_count: <number>"]
```

the number of attachments has exceeded the allowed limit. Again, the relay sends this limit as a number in the error description section


```
relay:  ["OK","<event-id">,false,"invalid: <description>"]
```

event is malformed, missing mandatory fields, or event doesn't have attachments


Response - binary messages
--------------------------------------

Relay must respond to each binary message with a ["ATTACH"] response. The response has the same format as "OK"

awaiting more attachments

```
relay:  ["ATTACH","<attachment-id">,true,"continue"]
```

event has been posted

```
relay:  ["ATTACH","<attachment-id">,true,"complete"]
```

**NOTE** keywords `continue` and `complete` are optional. They are intended for debugging purposes. Clients should not rely on them. The client always knows, how many attachments have to be send to publish the event.



Errors - binary messages
--------------------------------------

```
relay:  ["ATTACH","<attachment-id">,false,"invalid: mismatch"]
```

The sent binary message doesn't match any expected attachment. For example, the hash or size doesn't match.


Encryption - kind:4
-------------------

Attachments are sent encrypted for "kind:4". The same encryption is used as in the encryption of the message itself. An initialization vector (16 bytes) is stored at the beginning of the binary message and then the binary content itself.

```
[IV 16 bytes][ encrypted content ]
```

Download 
--------

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


Reuse existing attachment
-------------------------

To avoid reupload of the same file, you can use special form of FETCH command to 
attach existing attachment to a new event. This can be useful for long articles in case that user just posts an update of the article.

```
client: ["FETCH","<attachment-id>","ATTACH"]
relay: ["ATTACH","<attachment-id>",true,"continue/complete"]
```

in case that attachment doesn't exists

```
client: ["FETCH","<attachment-id>","ATTACH"]
relay: ["ATTACH","<attachment-id>",false,"missing: not found"]
```

in case that attachment doesn't match to any expected attachment

```
client: ["FETCH","<attachment-id>","ATTACH"]
relay: ["ATTACH","<attachment-id>",false,"invalid: mismatch"]
```

This command also "locks" the attachment in order to prevent to be scarped by a garbage collector (see below)


Changes in relay information document (NIP-11)
-----------------------------------------------

New items are added in the "limitation" section

```
 "limitation": {
    "max_attachment_count": 4,
    "max_attachment_size": 262144,
  }
```

* **max_attachment_count** - maximum count of attachments per event
* **max_attachment_size** -- maximum size of single attachment in bytes


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

In the case of NIP-95, the relay cannot control the size of the binary content. The relay can also be configured so that text messages are short, but the binary content can be large, for example for storing videos, because the relay places attachments on a separate repository. This NIP also specifies in more detail how limits are communicated to the client. If any of the attachments are too large, the client can reduce the resolution or quality of the stored media to meet the limits set

An unresolved issue relates to garbage collection. If I want to deal with garbage collecting within NIP-95, I encounter various situations where a race condition can occur, so there can be a situation where someone uploads files but they can be deleted before the actual event is published. This is because the protocol treats each event as a separate record with no linkage to other records.

**Binary content should be transferred using the HTTP protocol see NIP-96**

No, it didn't. We're mixing protocols. What if the relay is not an HTTP server? Yes this can happen, an http server can be actually an upstream proxy. The NIP-96 design is also very complex

**What if a client sends an event with the tag "attachment" but sends it via the EVENT command?**

The `FETCH` command simply fails. Attachments aren't available.

**How a client that does not support this NIP work?**

This client can't see the attachments, only the `content` of the event

**What if two clients upload the same attachment (how to resolve hash collisions)**

Same hash = same content. The file is stored only once. The relay can discard duplicate content

**In this NIP, the files are not signed, it is not possible to verify who sent them to the relay. NIP-95, on the other hand, also signs the file**

The hash is always signed, not the content. In the case of NIP-95, the hash includes the author's pubkey, whereas in this proposal only the file hash itself is signed. This allows sharing the same file without reuploading it. And if you are worried about someone "stealing" ownership of the file. That may be the case with NIP-95, no signature will prevent a copy&paste operation. The only way to protect the file is to encrypt it

**Why the tags do not have the form proposed in NIP-94**

That was originally considered. I liked the compatibility with NIP-94. It would be possible to link the file directly to the metadata above NIP-94. But this proposal would not allow to attach multiple attachments to a single event. The motivation was to have it like Twitter, where there can be up to 4 images. Or like in Reels, where you can have short videos, but they can be linked into a longer continuous video. You can also implement a form of streaming longer videos in this way

**Images in Kind:0, image placement in text, smileys, etc.**

Just as I can place anything from tags using #[index], I can reference an attachment this way

**Why does it have to be uploaded atomically?**

This is related to the garbage collection requirement. It could happen that the client uploads attachments, but before publishing the event, the garbage collector comes and because the new attachments don't have a reference, it deletes them

**Can a client initiate multiple ATTACH requests at the same time (i.e. without completing the previous one)?**

This situation is not defined. For simplicity, let us assume that the answer is no, i.e. that a new ATTACH command within the same connection invalidates the previous ATTACH command if the event of that command has not been completed and published. This can possibly be discussed.

**Why does the client have to explicitly link existing attachments to the new event (FETCH+ATTACH), wouldn't it be enough for the relay to do this automatically when it sees that some attachments are already present in the relay?**

It would be possible, but it would significantly complicate the protocol flow. The client knows how many attachments it has to send. If the relay would add some attachments automatically, it would have to communicate this fact to the client. Presumably via an ATTACH response.

However, this still does not address the situation where there may be a race condition, where the same attachment appears on a relay before the request is completed. Therefore, it is better if the client handles the attachment management and the relay behaves passively in this case.


