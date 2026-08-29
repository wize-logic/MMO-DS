---
title: Story flags and vars
description: How progression is stored and why the keys are opaque strings.
---

Progression is two things. **Flags** are booleans and record that something
happened. **Vars** are named integers and record where in a sequence a player
is. Between them they are what makes an npc say something different on a second
visit.

## The api

Scripts reach story state through `ScriptContext`, which forwards to
`StoryService` with the acting character id filled in:

```kotlin
ctx.isFlagSet(HoennFlags.FLAG_ADVENTURE_STARTED)
ctx.setFlag(HoennFlags.FLAG_VISITED_LITTLEROOT_TOWN)
ctx.clearFlag(HoennFlags.FLAG_SYS_POKEMON_GET)

ctx.getVar(HoennVars.VAR_LITTLEROOT_INTRO_STATE)
ctx.setVar(HoennVars.VAR_LITTLEROOT_INTRO_STATE, 3)
```

Two defaults matter:

- A flag that was never set reads as **false**.
- A var that was never set reads as **0**.

So a fresh character needs no seeding. `setVar(key, 0)` removes the key rather
than storing a zero, which keeps the two spellings of "never set" identical.

## Why the keys are opaque strings

`StoryService` takes a `String` and uses it as a map key and nothing else. It
does not know what a flag means. The schema is the same shape:

```sql
CREATE TABLE character_flags (
  character_id BIGINT       NOT NULL REFERENCES characters (id) ON DELETE CASCADE,
  flag_key     VARCHAR(128) NOT NULL,
  PRIMARY KEY (character_id, flag_key)
);
```

A row means the flag is set, no row means unset. `character_vars` adds an `INT`
value column.

That is deliberate. Keeping the keys opaque means a hand authored questline can
drive the same store through the same api, without the persistence layer
learning anything about where today's content came from.

## Where the constants come from

You never type a raw key. Each region gets a generated pair of constant objects
in `de.fiereu.openmmo.story.generated.<region>`, produced by the `story`
generator from the game data's own flag and var headers. It keeps the names and
drops the numbers, because the names are the stable half.
