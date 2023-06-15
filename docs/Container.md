# Container

Build this server in container

For example

```podman build . -t nostr_server:latest```

Then run nostr server with default configuration
```podman run -it --rm -v data:/app/data --net=host nostr_server:latest```

If you want override configuration, you can mount it aswell by using ```-v conf:/app/conf```
