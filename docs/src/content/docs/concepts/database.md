---
title: The database
description: Where state lives, how the schema changes, and how dev data is seeded.
---

Two PostgreSQL databases, one per server:

- **login-db**: accounts, owned by `server.login`.
- **game-db**: characters, pokemon and items, owned by `server.game`.

They stay separate on purpose. The game server never looks up an account, it
trusts the `userId` the session token carries. That is also why
`characters.user_id` has no foreign key: it points into the other database.

Both use HikariCP for pooling, Flyway for migrations and jOOQ for queries.

## Migrations are the source of truth

The schema is plain SQL in each server's `src/main/resources/db/migration/`.
Those files are read twice:

- **At build time** jOOQ parses them and generates Kotlin classes. No database
  and no docker needed to build.
- **At startup** Flyway runs them against the real database before anything
  listens. A broken database stops the server there.

To change the schema, add `V<next>__short_name.sql`. Never edit a migration
that has already been merged, and keep the DDL to standard SQL, because the
jOOQ parser does not know the exotic Postgres extensions.

## Memory is the live version

Gameplay does not touch the database. `CharacterStore` loads a character when
its player signs in, and every read and write after that hits memory. A change
marks the character dirty and a background flusher writes it back after a short
debounce. Warps and disconnects flush at once, and a disconnect also evicts the
character once its last write landed.

So the database trails memory by a few seconds at most, and only connected
players are cached. Writes are atomic per character, because scripts write from
their own coroutine while packets are answered on another. A write aimed at a
character an eviction already removed is refused and logged, not dropped
quietly.

## Dev data

Seed data does not belong in `db/migration`, because everything there runs in
every environment. Seeds live in `src/main/resources/db/dev/` as repeatable
migrations (`R__seed_something.sql`), so they never collide with a real schema
version.

Every statement has to be idempotent, since a repeatable migration runs again
whenever its file changes. Use `ON CONFLICT DO NOTHING`. Fixed ids keep the
entity tag in the low 16 bits, which is what stops them colliding with runtime
ids.

`db/dev` is only applied when `db.seedDev` is on. It defaults to true locally
and is turned off in production with `LOGIN_DB_SEED_DEV=false` and
`GAME_DB_SEED_DEV=false`.

Dev characters are the exception. A character built by SQL drifts from what a
real new character gets, so `DevCharacterSeeder` makes one per region at
startup through the same `CharacterStore.createCharacter` the client uses. It
skips a region that already has its character, so it never overwrites one you
have played.
