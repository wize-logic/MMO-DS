# openmmo

The client, built as a mod of the engine tree.

The engine already draws both DS screens in software, ships a viewer, plays,
saves and pumps a comms hook at the top of every frame. This client is not a
second program beside it. It is code and patches compiled into that binary,
with the game's own flags, headers and ABI, and a mod is the one supported way
to do that without editing the engine tree.

## Layout

```
mmo/mods/openmmo/
    src/*.c                new client code, built with the game's flags
    patches/<path>.patch   changes stacked onto the engine's own sources
```

`src/` holds the glue: the net pump on the comms hook, connection state drawn
in the guest frame, boot into a session, and the crowd capacities the overworld
needs. `patches/` holds the places where authority moves from the engine to the
server. Every patch is the same shape, a declaration and a call into `src/`, so
that a drifting engine breaks the build instead of quietly changing behaviour.

## What you have to supply

No game content ships here. Everything comes from a built game image produced
by the engine tree's own toolchain, which this project only reads.

At build time `make fused` runs the engine's build, which needs two files from
`$ENGINE_DIR/build/rom/`:

- `main.nef.xMAP`, the link map. Arena bounds, stack sizes and the overlay
  digest are lifted out of it.
- `trainer_ai_script.o`, the trainer AI bytecode, linked in as a table. A
  zeroed stub crashes the first battle.

So build the image once in the engine tree before `make fused`. The build stops
with a clear message rather than guessing.

At run time the binary memory maps the image itself for every asset, map,
sprite and string.

## Building it

```console
$ make -C mmo fused
```

That builds `libopenmmo.a`, runs the engine's own build with three overrides
(its mod mechanism pointed at this directory, `MODS=openmmo`, and the archive
added to the link) and lands everything in `mmo/build/fused/`.

To open the window, run the game and its viewer. They are two processes on
purpose: the game is 32 bit and the host SDL2 is 64 bit, so they meet in a
shared memory page rather than in a link.

```console
$ mmo/build/fused/pokeplatinum --rom $ENGINE_DIR/build/rom/pokeplatinum.us.nds \
      --view openmmo &
$ mmo/build/fused/pcview openmmo
```

Run that from a shell that can reach your display. Under WSL that means your
own terminal.

With no window, `--frames N --dump-frames DIR` writes a PNG per frame, which is
how the build gets checked in a script.

## The engine pin

`mmo/ENGINE_COMMIT` names the engine commit this tree is built against, and
`mmo/ENGINE_SURFACE` carries content hashes for the files the patches touch.
`make -C mmo pincheck` says whether the tree in front of you still matches.
When the pin moves, a bump has to show that the patches still apply and the
suite still passes.
