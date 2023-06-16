# Experimental extension of NOSTR protocol - store images and small binary data


## Event: kind 21010 (ephemeral event)

* **content** - description of file
* **tags** ["content_type"] - context_type
           ["size"] - "expected size"           

## Flow

* client sends EVENT kind 21010
* client sends binary frame with image
* server sends ["OK", id, true, "image url"]
* client sends message kind 1 




