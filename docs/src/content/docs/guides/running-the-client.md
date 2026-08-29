---
title: Running the game client
description: How the launcher provisions a client and points it at a server.
---

The launcher fetches a client, patches it and starts it. It works in its own
folder and never touches an install you already have.

## Start it

Run the servers first, then:

```bash
./gradlew :launcher:dev
```

That does two things. It serves a feed on `https://127.0.0.1:20443` and it opens
the launcher window. Press Play and the client starts.

Leave the command running while you play. The client reads the feed the whole
time it is open, so stopping it early breaks the game.

## Build a real one

```bash
./gradlew :launcher:createDistributable \
  -Popenmmo.feedOrigin=https://feed.openmmo.dev
```

Without the property the launcher points at the loopback dev feed, which is
what you want in development and never in a release.

## Adding a patch

Patches live in `launcher/manifests/manifest-<revision>.toml`, one file per
client version. A patch that no longer matches fails the launch and names
itself, so a client update never corrupts anything quietly.

```toml
[[patches]]
type = "binary_string"
name = "SupportUrl"
target = "@client"
find = "https://support.example.com"
replace = "https://support.openmmo.dev"
```

`target = "@client"` means the game binary, whatever it is called on this
platform.

A replacement has to be exactly as long as the text it replaces. The client
stores the length of every string separately, so a longer one does not fit and
a shorter one leaves the tail behind.
