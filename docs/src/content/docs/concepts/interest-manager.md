---
title: The InterestManager
description: How the server decides which players receive a packet.
---

A player walking across a map should appear on the screens of the players
standing there and nobody else. Sending every packet to everyone online would
be wasteful, and would leak things players should not see.

That is what the `InterestManager` is for. It lives in
`server.game/src/main/kotlin/de/fiereu/openmmo/server/game/world/interest/`.

## The idea

Sessions are grouped, and a group is named by an `InterestKey`: "this map",
"this guild", "this battle". A session can be in several at once.

The manager itself is deliberately dumb. It knows three things:

- `join(ctx, key)` and `leave(ctx, key)`
- `members(key)`
- `broadcast(key, packet, exclude)`

It does not know what a map or a guild is. What a group means is decided by the
service that uses it.

## Movement, as an example

`PresenceService` builds overworld presence on top of it, one group per map.
When a player spawns, `enter` joins them to their map's group and exchanges
snapshots with the players already there. A `MovementPacket` goes to
`broadcastMove`, which fans it out to that map's group only. Warping or walking
off an edge calls `refresh`, which despawns from the old group and spawns into
the new one.

## When to use it

Whenever one player's action should reach a specific set of others: guild chat,
battle updates, trade windows, parties. Global traffic like server
announcements is `MultiplayerService`'s job instead.

## Using it

1. Pick or add a key. `InterestKey` is a sealed interface; map, guild and
   battle variants already exist.
2. Inject the manager. It is a Dagger singleton.
3. `join` when a player enters your group and `leave` when they exit.
   `leaveAll(ctx)` clears a session on disconnect.
4. `broadcast(key, packet, exclude = sender)` to notify the group.

```kotlin
interestManager.join(ctx, GuildInterestKey(guildId))
interestManager.broadcast(GuildInterestKey(guildId), chatPkt)
```

If your feature needs more than membership, follow `PresenceService`: put a
small service on top and keep the meaning there.
