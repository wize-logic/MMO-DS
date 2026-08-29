---
title: Writing a code generator
description: How to add a build time generator that turns raw game data into Kotlin.
---

Most game data (maps, moves, species) is built at compile time. The `:codegen`
module reads the raw data files under `decomp/` and turns them into Kotlin
`*Def` objects. The generated files are not committed. They are regenerated on
every build.

## How it fits together

```
decomp/...                       the raw data
        │  parsed by
        ▼
codegen/src/generator/kotlin/    the generator, build time only
        │  rendered through a JTE template
        ▼
codegen/build/generated/...      the generated Kotlin, throwaway
        │  compiled into
        ▼
codegen/src/main/kotlin/         the Def and Registry the server uses
```

Each generator is registered in `codegen/build.gradle.kts` and runs as its own
Gradle task: `generateMaps`, `generateMoves`, `generatePokemon` and so on.

## Pick a data source first

Three ways to choose which tree to read:

- **By region**, for data that differs per region, like maps. The generator
  gets one `region|path` argument per region.
- **One source of truth**, for data that is the same everywhere, like moves and
  species. Read one tree and accept its values rather than trying to merge two.
- **Whatever the client speaks**, for data the client indexes itself, like
  items. The client answers in one table no matter which region the player is
  in, so the generator follows that numbering.

Rule of thumb: if the same thing exists in both games, use one source. If it
belongs to one region, go by region. If the client does the lookup, follow the
client.

## Adding one

1. Write the parser under `codegen/src/generator/kotlin/`.
2. Write the JTE template that renders the `*Def` objects.
3. Register the task in `codegen/build.gradle.kts`.
4. Add the runtime `Def` and `Registry` under `codegen/src/main/kotlin/`.

`./gradlew :codegen:printDecompTrees` prints the tree each generator resolved,
which is the first thing to check when one cannot find its input.
