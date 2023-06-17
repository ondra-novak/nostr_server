# RelayBot

The relaybot is account, which is managed by this relay. You need to set a private
key of the relaybot in config. Then the relay bod listens private messages
and carries received commands

Every command must start by slash `/command`


```
/create_group <create discussion group>
/set_metadata <set metadata of relaybot (Kind: 0)
```

## Groups

Groups are NOSTR accounts that repost content received as a private message from
users. They can be also managed by owners or moderators. 

To control group, there are commands

```
/set_metadata <json> - kind 0
/add_moderator pubkey - generates kind 3 document containing list of moderators
/remove_moderator pubkey - removes moderator from the list
```

### group moderation

Moderation is done by commands or by replies. 

### moderation by command

```
/ban <pubkey> - mute specific user
/unban <pubkey> - unmute specific user
/delete <event> - delete a given discussion article - it cannot delet discussion under it
```

### moderation by replies

Moderato can reply directly to message posted by group account with following commands

```
/ban - mute user and delete message
/delete - delete message, but don't mute user
```

All these commands must be posted to this relay

