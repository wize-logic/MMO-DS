---
title: Scripts
description: Why the client runs cutscenes, and what the server keeps a record of.
---

Almost everything the overworld does when a player presses A is a script.

## The client runs them

The client is built on the game's own code, so it already carries the script
machine the game shipped with: a dispatch table and the bytecode files the
scenes are written in. That machine used to be gated off while the server drove
cutscenes. It is on now, with or without a server.

The old server side version worked. What did not work was deriving it. Every
command needed a matching primitive on the server, and every engine rule had to
be relearned one bug at a time. The engine has none of those bugs, because the
rules are its own.

## The server keeps the record

A scene plays on the client, so the server is told what changed rather than
deciding it. Three opcodes carry that:

| opcode | direction | what it says |
|---|---|---|
| `0xCB` | s2c, on join | the seat: every flag and var this character holds, written before the map loads |
| `0xCB` | c2s | the report: what the script machine has written since the last one |
| `0xCC` | c2s | a warp the client took itself, and where it landed |
| `0xCD` | c2s, once | whether this client runs field scripts at all |

The client watches its own flag block with a shadow diff rather than hooking
the setters, because the engine hands out raw pointers into that block and
clears whole ranges at a time. Reports are coalesced to twice a second, and
nothing waits on an answer, because the write already happened.

Flags and vars are stored per character under `region/vm/flag/N` and
`region/vm/var/N`, in their own namespace so they cannot collide with the named
keys other progression uses. See [Story flags and vars](../story-state/).

This trusts the client. A modified one can claim any flag or destination. That
is the accepted cost of running scenes locally, and the key shape is chosen so
a per script effect manifest can be held against an arriving claim later.

## What the server still decides

Anything that mints or spends state the server owns:

- **Trainer battles.** The party is the server's, so the fight is too.
- **The party, the bag, money and badges.** Each has its own packet.
- **Blackouts.**
- **Story jumps**, which set flags and warp directly.
