# Content packages

The content this repository owns. A package is a folder with a `mod.toml` and
no `.c` in it. The running game overlays its files in front of the player's
image, so nothing here needs a rebuilt game image.

The layout is the engine's. Read `$ENGINE_DIR/pc/mods/README.md` and follow
that tree.

```console
$ make -f $ENGINE_DIR/pc/Makefile cook \
    PC_MODS_DIR=$PWD/mmo/mods PC_MODS=bodies,hub
```

`openmmo/` is the other door: C sources and patches, compiled with
`MODS=openmmo`. Listing it in `PC_MODS` is a boot error. Every other directory
here is a runtime package.

```
mmo/mods/<dirname>/
    mod.toml                 # flat key = value; id, name, version required
    records/                 # frozen ids the cook keeps
    content/                 # what you author (people, props, maps, pokemon, text)
    narc/   or replace/      # optional; a ported member lands here
    .cooked/                 # the cook writes this; gitignored
```

`mod.toml` is the six keys the loader reads. `#` comments and blank lines are
ignored, and a `[table]` header is a boot error. The directory name is what
`PC_MODS` lists; `requires` and `load_after` name the `id`.

## What lives here

| directory | kind | what it is |
|---|---|---|
| `hub/` | authored | a map |
| `bodies/` | authored | a person and a building |
| `sprigatito/` | authored | one species |
| `imports/` | ported | a recipe, not bytes; `make -C mmo import IMPORT_ROM=<other>` fills `narc/` |
| `sound_*/` | ported | soundtrack swaps, one package per pairing |
| `openmmo/` | compile time | the C plugin, see [openmmo/README.md](openmmo/README.md) |

Authored packages cook. A ported one does not, because the porter writes
members the overlay already serves.

`tests/overlay_test.sh` cooks every authored package, boots with `PC_MODS` set,
loads every authored map and runs the hub's field scripts. A missing record id
or a text bank that fails to render fails `make test`.

There is no load order file. Enable a package by naming it in `PC_MODS`. An
unset `PC_MODS` loads nothing here, so a boot that never asked for a hub does
not get one.

`play.sh` defaults `PC_MODS_DIR` to this directory, and `--mods` / `--mods-dir`
override it. The launcher reads the same two from `launcher.cfg` as `mods` and
`mods-dir`.

`.cooked/` is per package and gitignored. Boot checks its digest and refuses a
stale tree by name. It does not cook for you.
