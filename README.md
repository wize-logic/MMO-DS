# OpenMMO DS

An open source server and client for a small persistent Pokemon world.
Still in development, not ready for real use.

Two programs, one product:

- **Servers** (Kotlin, Gradle). A login server on 2106 and a game server
  on 7777.
- **Client** (`mmo/`, C). The game window, a viewer and a launcher, built
  on top of a DS engine port.

## Get it

Clone with the engine submodule. It is the tree the client is fused with, and
it is also where the Sinnoh data comes from, so it carries its weight twice.

```bash
git clone --recurse-submodules https://github.com/wize-logic/OpenMMO-DS
cd OpenMMO-DS
```

Already cloned without it:

```bash
git submodule update --init --recursive
```

### Game data for the other two regions

Sinnoh needs nothing further. Hoenn and Kanto are generated from two upstream
decompilations that this project does not distribute: `pokeemerald` and
`pokefirered`. Clone each under `decomp/` by name, or point `DECOMP_DIR` at a
directory that already holds them.

```bash
./gradlew :codegen:printDecompTrees   # says which path each tree resolved to
```

## Set up a machine

`./setup.sh` installs what is missing and stages the rest. It is safe to run
twice, and it never overwrites something you have edited.

```bash
./setup.sh              # install and stage
./setup.sh --check      # say what is missing, change nothing
```

It wants a JDK 25 and docker. Gradle refuses anything older than 25, so if
you install a JDK by hand, point `JAVA_HOME` at it.

## Run the servers

Configuration lives in a `.env` file at the repository root. It is gitignored.
Copy the template and edit the values:

```bash
cp .env.example .env
docker compose up -d          # the two postgres databases
./start-server.sh             # login on 2106, game on 7777
./stop-server.sh
```

Windows has the same pair as `start-server.ps1` and `stop-server.ps1`.

### Your first account

A fresh database has no users. Set `OPENMMO_ADMIN_USERNAME` and
`OPENMMO_ADMIN_PASSWORD` in `.env` and the server creates that account on
startup, but only while the user table is still empty. Changing the values
later does nothing, so it is not a way to reset a password.

More accounts after the first:

```bash
./gradlew :server.login:run --args="create-user NAME PASSWORD"
```

The website in `web/` is the way players get one.

### Reaching the server from another machine

The login server hands the client an address to dial the game server on. It
defaults to loopback, which serves a client on the same machine and nothing
else. Set `GAME_SERVER_PUBLIC_IPV4` (and `GAME_SERVER_PORT` if it is not 7777)
to anything reachable from outside. A friend who signs in and then lands on
their own `127.0.0.1` is this default still in place.

An empty value is not the same as an unset one. It replaces the default with
nothing, and the server refuses to start rather than advertise an address no
client can reach.

## Build the client

Three of the four need nothing but a compiler:

```bash
make -C mmo            # the headless client
make -C mmo viewer     # the window
make -C mmo launcher   # the front door
```

The playable game is the engine and this client's netcode compiled into one
binary. The engine is the `engine/pokeplatinum` submodule, and the build finds
it there on its own. Set `ENGINE_DIR` if you keep your own checkout somewhere
else.

```bash
make -C mmo fused
```

That reads a built game image out of the engine tree, so build the image once
first, with the engine's own toolchain. Do that before anything else here: it
is also what fetches the engine's own subprojects, and `make fused` checks its
patches against them. The build stops and names what it wants rather than
guessing.

Then, with the servers up:

```bash
./play.sh              # straight into the game
./play.sh --menu       # open the front door instead
```

### The other three toolchains

The same client, retargeted. Each is `mmo/Makefile` with only what genuinely
differs set, and each drives the engine's matching makefile for its fused half.

**Android**, `armeabi-v7a`, for a handheld running stock Android:

```bash
make -f Makefile.android status     # build/android/libopenmmo.so
make -f Makefile.android programs   # build it and read its ABI back out
make -f Makefile.android apk        # package it
make -f Makefile.android apk-live   # package it pinned to a live server
```

It wants an NDK, r27c by default under `~/.local/opt/android-ndk-r27c`; set
`NDK=` for another. `ANDROID_API` is a floor, 26. The makefile itself compiles
only C, no Java and no Gradle; packaging an APK additionally needs `aapt2`,
`zipalign` and `apksigner` from an SDK. `apk-live` reads the release pin out of
`mmo/dist.sh`, so it refuses rather than shipping an APK that dials nowhere.
`mmo/android` holds the app around it: the manifest, the activity, the launcher
icons, the SDL shim and the front door. The engine's own Android build is
`pc/Makefile.android` in the engine submodule.

**Windows**, via mingw:

```bash
make -f Makefile.win winlibs        # fetch and build SDL2, raylib, freetype, once
make -f Makefile.win programs
```

Wants `i686-w64-mingw32`.

**32-bit ARM**, for a device or for qemu:

```bash
make -f Makefile.arm status
make -f Makefile.arm run ARGS='--help'   # run it here, under qemu-arm
```

Wants `arm-linux-gnueabihf`.

You supply your own game image. The launcher looks for one and says so when
it cannot find it. Nothing here ships game data.

## Tests

```bash
./gradlew build        # the servers, with formatting and lint
make -C mmo test       # the client suite, about 25 minutes
```

The client suite skips the parts that need a live server, so run the servers
first if you want those covered.

## Layout

| Path | What it is |
| --- | --- |
| `server.login`, `server.game`, `server.web` | the three services |
| `protocols.login`, `protocols.game` | packet definitions |
| `network`, `bytecodec`, `common` | the shared plumbing |
| `codegen` | game data generated at build time |
| `keys` | key generation for signing |
| `launcher` | the launcher that provisions a client |
| `mmo` | the C client, viewer and front door |
| `mmo/android` | the Android app around it |
| `engine` | the engine the client is fused with |
| `decomp` | where the upstream data trees go |
| `web` | the public site and the registration form |
| `docs` | the documentation site |
| `deploy` | compose files for running released images |

## Documentation

The docs site lives in `docs/`, and is published at
[docs.openmmo.dev](https://docs.openmmo.dev/).

```bash
cd docs && npm install && npm run dev
```

## Releases

[release-please](https://github.com/googleapis/release-please) cuts releases
from the commit history. Pull requests are squash merged, so their titles
become the commit messages it reads and have to follow
[Conventional Commits](https://www.conventionalcommits.org/). `feat` bumps the
minor version, `fix` the patch version.

Merging the release pull request tags the release, attaches the server
archives and publishes a container image per server:

```bash
docker pull ghcr.io/openmmo-org/openmmo-login:latest
docker pull ghcr.io/openmmo-org/openmmo-game:latest
```

`dev` tags track the default branch and are built without waiting for the
test suite, so they can be broken in a way `latest` is not.

`deploy/` holds a compose stack per server. Copy `deploy/.env.example`, fill
it in, and bring the game side up first.

The version lives in `gradle.properties` and applies to every module. Do not
edit it by hand.

## License

Two licences, side by side. The work written for this project is AGPL-3.0, in
[LICENSE](LICENSE). The parts derived from the engine the client is fused with,
everything under `mmo/mods/openmmo`, are GPL-3.0 like the engine itself, whose
text ships with the `engine/pokeplatinum` submodule. The fused binary combines
both. [NOTICE](NOTICE) says which file falls where.

No game data is here, and none ships with it.
