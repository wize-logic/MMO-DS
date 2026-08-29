---
title: Deploying the feed and the launcher
description: How to publish a feed and build a launcher that trusts it.
---

Players run a launcher. It needs a feed to know which server to send the client
to. The feed is a handful of static files on a web host, and the launcher is
built once pointing at that host.

For local work you need none of this. `./gradlew :launcher:dev` serves a feed
on loopback.

## 1. Make a feed key

```bash
./gradlew :keys:generateFeed
```

That writes `keys/build/feed.private.pem` and `keys/build/feed.public.pem`.

Keep the private key out of git and somewhere safe. Losing it means every
launcher already out there stops trusting your feed, and the only fix is a new
launcher release.

## 2. Do not let git touch the bytes

The files are signed byte for byte, so add this to `.gitattributes` wherever
you keep them:

```
* -text
```

Without it git may rewrite line endings on checkout, the bytes stop matching
the signature, and every launcher rejects the feed.

## 3. Write and sign `main.xml`

```bash
openssl dgst -sha256 -sign feed.private.pem -out main.sig256 main.xml
```

Re-run that every time you edit `main.xml`. An old signature looks the same as
a forged one.

## 4. Publish the server key

```bash
openssl dgst -sha256 -sign feed.private.pem \
  -out game.public.pem.sig256 game.public.pem
```

Publish only the public half. The private key belongs on the server and nowhere
else.

## 5. Build the launcher

```bash
./gradlew :launcher:packageDistributionForCurrentOS \
  -Popenmmo.feedOrigin=https://feed.openmmo.dev
```

Two keys go in at build time and they behave differently. The feed key is baked
in and can only be changed by shipping a new launcher. The server key arrives
through the feed, so it can be rotated without one.
