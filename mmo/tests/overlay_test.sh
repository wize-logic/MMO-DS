#!/bin/sh
# A hub is loose files over the player's image, not a
# rebuild.
#
#   1. static: every directory under `mods/` except the compile-time
#      plugin is a package (`mod.toml`, no `.c`, engine layout). The
#      hub's map record pins its own header id. The bodies gfx and
#      prop records pin 276 and 590. The sprigatito species record
#      pins 496. `mods/openmmo` (C and patches) is a different
#      directory.
#   2. the cook, run from a copy of the tracked sources so the working
#      tree is untouched, reports "no rebuild" for every authored
#      package, freezes every record's id, and appends NARC members
#      past the cartridge's last. The engine's built image and its
#      `build/rom/.ninja_log` do not move across it, `ninja -C
#      build/rom` is not on this path.
#   3. the fused build serves it: the packages load in order, the game's
#      own `MapHeader_Get*` answer for the new header, and the appended
#      land-data member reads back through the game's own NARC path.
#      The bodies package is claimed on its own too: gfx 276 / mmodel
#      470 as an NSBTX, prop 590 as an untextured NSBMD, a bodies-only
#      boot serves those members, and without it they are not claimed.
#      Every authored map header answers; every authored species id
#      grows `pl_pokegra` and `PC_LAB_SPRITE` names it. `sprite.c`
#      locates that id against the grown count and still refuses
#      without it.
#   4. the bytes are the package's, not the cartridge's: with the
#      packages off, the same member is not claimed; with them on, a
#      vanilla member is not claimed either, the overlay serves what it
#      declares and the image serves the rest.
#   5. the two doors stay apart: `openmmo` in PC_MODS is a boot error
#      that names `MODS=`, and a package whose `requires` is unmet
#      refuses by name rather than loading half a hub.
#   6. the fused build draws it. Getters answering is not a picture.
#      PC_LAB warps onto the cooked header; MAPSCAN names that header
#      and the magenta person; OnTransition fires SetTrainerFlag; the
#      text bank renders one string and reports failed=0; a dumped
#      field frame's top screen has the magenta person and the cyan
#      prop. Walking a planted vanilla door is not the oracle
#      (content/events/ would replace that map's whole event list).
#   7. the hub field load is also a script-coverage run. `PC_SCRIPT_COV`
#      writes a `.cov` and the port's `pc/tests/pc_scrcov.py` names the
#      opcodes; a load that never reaches `SCRCMD_SETTRAINERFLAG` is a
#      failed test, not a walk-there-and-see.
set -eu

ROOT=${1:?usage: overlay_test.sh <mmo-root> <build-dir> <engine-dir>}
BUILD=${2:?usage: overlay_test.sh <mmo-root> <build-dir> <engine-dir>}
ENGINE=${3:?usage: overlay_test.sh <mmo-root> <build-dir> <engine-dir>}

FUSED="$BUILD/fused/pokeplatinum"
CLIENT="$BUILD/openmmo-client"
ROM="${PC_ROM:-$ENGINE/build/rom/pokeplatinum.us.nds}"
MODS="$ROOT/mods"

fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; fail=1; }

# A file's identity as this check cares about it: size and mtime. Cheap
# enough to take around every step, and it moves when a build writes.
stamp() { # PATH
    if [ -e "$1" ]; then
        ls -l --time-style=+%s "$1" | awk '{print $5, $6}'
    else
        echo absent
    fi
}

echo "content packages this repo owns are content, not code:"

for dir in "$MODS"/*/; do
    pkg=$(basename "$dir")
    if [ "$pkg" = openmmo ]; then
        continue
    fi
    if [ ! -f "$dir/mod.toml" ]; then
        bad "$pkg has a mod.toml"
        continue
    fi
    # The four directories a package's payload may live in, plus the two
    # states a ported one is legally in without any of them. Its bytes are the
    # player's cartridge's and are never committed, so a clean checkout has
    # only the recipe that says where they come from; and a region port writes
    # `.cooked/` rather than `narc/`, because a map header table and a
    # billboard list are cooked artifacts with nothing here to cook them from.
    # A composed sound package is the third legal state with none of those:
    # its bytes are written at Play out of the player's own cartridges, so a
    # clean checkout holds only the mod.toml that names the pair.
    composed=0
    case "$pkg" in sound_*_*) composed=1 ;; esac
    if find "$dir" \( -name '*.c' -o -name '*.h' \) | grep -q .; then
        bad "$pkg carries no C"
    elif [ "$composed" -eq 0 ] \
            && [ ! -d "$dir/content" ] && [ ! -d "$dir/records" ] \
            && [ ! -d "$dir/narc" ] && [ ! -d "$dir/replace" ] \
            && [ ! -d "$dir/.cooked" ] && [ ! -f "$dir/port.recipe" ]; then
        bad "$pkg follows the engine package layout"
    else
        ok "$pkg is a mod.toml and content, with no C in it"
    fi
done

MAPID=$(sed -n 's/.*"id"[[:space:]]*:[[:space:]]*\([0-9]*\).*/\1/p' \
        "$MODS/hub/records/maps/hub.json" 2>/dev/null | head -1)
AREA=$(sed -n 's/.*"area"[[:space:]]*:[[:space:]]*\([0-9]*\).*/\1/p' \
        "$MODS/hub/records/maps/hub.json" 2>/dev/null | head -1)
GFXID=$(sed -n 's/.*"id"[[:space:]]*:[[:space:]]*\([0-9]*\).*/\1/p' \
        "$MODS/bodies/records/gfx/magenta_person.json" 2>/dev/null | head -1)
PROPID=$(sed -n 's/.*"id"[[:space:]]*:[[:space:]]*\([0-9]*\).*/\1/p' \
        "$MODS/bodies/records/props/cool_house.json" 2>/dev/null | head -1)
SPECIESID=$(sed -n 's/.*"id"[[:space:]]*:[[:space:]]*\([0-9]*\).*/\1/p' \
        "$MODS/sprigatito/records/species/sprigatito.json" 2>/dev/null | head -1)
if [ -n "${MAPID:-}" ]; then
    ok "the hub's map record pins header $MAPID"
else
    bad "the hub's map record pins its own header id"
fi
if [ "${GFXID:-}" = 276 ]; then
    ok "the bodies gfx record pins id 276"
else
    bad "the bodies gfx record pins id 276"
fi
if [ "${PROPID:-}" = 590 ]; then
    ok "the bodies prop record pins id 590"
else
    bad "the bodies prop record pins id 590"
fi
if [ "${SPECIESID:-}" = 496 ]; then
    ok "the sprigatito species record pins id 496"
else
    bad "the sprigatito species record pins id 496"
fi

# Authored packages cook. A directory with content/ or records/ is one;
# imports is a recipe plus narc/ and belongs to import_test.sh.
authored=
authored_csv=
authored_n=0
for dir in "$MODS"/*/; do
    pkg=$(basename "$dir")
    if [ "$pkg" = openmmo ]; then
        continue
    fi
    if [ ! -f "$dir/mod.toml" ]; then
        continue
    fi
    if [ -d "$dir/content" ] || [ -d "$dir/records" ]; then
        if [ -n "$authored" ]; then
            authored="$authored $pkg"
            authored_csv="$authored_csv,$pkg"
        else
            authored="$pkg"
            authored_csv="$pkg"
        fi
        authored_n=$((authored_n + 1))
    fi
done
if [ "$authored_n" -ge 3 ]; then
    ok "authored packages are $authored_csv"
else
    bad "authored packages include bodies, hub and sprigatito (got ${authored_csv:-none})"
fi

if [ -d "$MODS/openmmo/patches" ] && [ ! -f "$MODS/openmmo/mod.toml" ]; then
    ok "the compile-time door is a different directory with no mod.toml"
else
    bad "the compile-time door is a different directory with no mod.toml"
fi

echo "every authored package cooks from its tracked sources, and the cook rebuilds nothing:"

cooked=
if [ ! -f "$ENGINE/pc/modcook.py" ]; then
    echo "  SKIP (no cook under $ENGINE)"
elif ! command -v python3 > /dev/null 2>&1; then
    echo "  SKIP (no python3)"
elif [ -z "$authored" ]; then
    echo "  SKIP (no authored packages to cook)"
else
    for pkg in $authored; do
        cp -r "$MODS/$pkg" "$tmp/"
        rm -rf "$tmp/$pkg/.cooked"
    done

    rom_before=$(stamp "$ROM")
    ninja_before=$(stamp "$ENGINE/build/rom/.ninja_log")
    pcbin_before=$(stamp "$ENGINE/build/pc/pokeplatinum")

    if env -u PC_MODS -u PC_MODS_DIR -u PC_MODFS \
            make -f "$ENGINE/pc/Makefile" cook \
            PC_MODS_DIR="$tmp" PC_MODS="$authored_csv" \
            > "$tmp/cook.log" 2>&1; then
        crc=0
    else
        crc=$?
    fi

    rom_after=$(stamp "$ROM")
    ninja_after=$(stamp "$ENGINE/build/rom/.ninja_log")
    pcbin_after=$(stamp "$ENGINE/build/pc/pokeplatinum")

    if [ "$crc" -ne 0 ]; then
        bad "the cook runs over a copy of the tracked sources (exit $crc)"
        tail -3 "$tmp/cook.log"
    elif [ "$(grep -c 'no rebuild' "$tmp/cook.log")" -eq "$authored_n" ]; then
        ok "every authored package cooks, and each says no rebuild"
        cooked=1
    else
        bad "every authored package cooks, and each says no rebuild"
        tail -3 "$tmp/cook.log"
    fi

    if [ "$rom_before" = "$rom_after" ] && [ "$ninja_before" = "$ninja_after" ] \
            && [ "$pcbin_before" = "$pcbin_after" ]; then
        ok "the built image, its ninja log and the port binary did not move"
    else
        bad "the built image, its ninja log and the port binary did not move"
        echo "       image  $rom_before -> $rom_after"
        echo "       ninja  $ninja_before -> $ninja_after"
        echo "       binary $pcbin_before -> $pcbin_after"
        echo "       (a concurrent engine build is the other explanation:"
        echo "        \`make -C mmo enginecheck\` says whether that tree moved)"
    fi
fi

LAND=fielddata/land_data/land_data.narc
MATRIX=fielddata/mapmatrix/map_matrix.narc
EVENTS=fielddata/eventdata/zone_event.narc
SCRIPTS=fielddata/script/scr_seq.narc
MSG=msgdata/pl_msg.narc
MMODEL=data/mmodel/mmodel.narc
BUILD_MODEL=fielddata/build_model/build_model.narc
POKEGRA=poketool/pokegra/pl_pokegra.narc
land_idx=
matrix_idx=
event_idx=
msg_idx=
mmodel_idx=
if [ -n "$cooked" ]; then
    ids="$tmp/hub/.cooked/ids.toml"
    if grep -q "\"hub:map/hub\" = $MAPID" "$ids" 2>/dev/null; then
        ok "the cook froze hub:map/hub at the record's $MAPID"
    else
        bad "the cook froze hub:map/hub at the record's $MAPID"
        cat "$ids" 2>/dev/null || true
    fi

    land_idx=$(ls "$tmp/hub/.cooked/narc/$LAND" 2>/dev/null | head -1)
    matrix_idx=$(ls "$tmp/hub/.cooked/narc/$MATRIX" 2>/dev/null | head -1)
    event_idx=$(ls "$tmp/hub/.cooked/narc/$EVENTS" 2>/dev/null | head -1)
    msg_idx=$(ls "$tmp/hub/.cooked/narc/$MSG" 2>/dev/null | head -1)
    scr_n=$(ls "$tmp/hub/.cooked/narc/$SCRIPTS" 2>/dev/null | wc -l)
    if [ -n "$land_idx" ] && [ -n "$matrix_idx" ]; then
        ok "the cook wrote land_data/$land_idx and map_matrix/$matrix_idx"
    else
        bad "the cook wrote a land-data member and a matrix member"
        find "$tmp/hub/.cooked/narc" -type f 2>/dev/null | head -8
    fi
    if [ -n "$event_idx" ] && [ -n "$msg_idx" ] && [ "${scr_n:-0}" -ge 2 ]; then
        ok "the cook wrote zone_event/$event_idx, $scr_n scr_seq members, pl_msg/$msg_idx"
    else
        bad "the cook wrote events, scripts and a text bank"
        find "$tmp/hub/.cooked/narc" -type f 2>/dev/null | head -8
    fi
    maps_txt="$tmp/hub/.cooked/generated/cooked_maps.txt"
    if [ -n "${AREA:-}" ] && grep -q "^$MAPID $AREA " "$maps_txt" 2>/dev/null; then
        ok "cooked_maps.txt points header $MAPID at area $AREA"
    else
        bad "cooked_maps.txt points header $MAPID at the record's area"
        cat "$maps_txt" 2>/dev/null || true
    fi

    bodies_ids="$tmp/bodies/.cooked/ids.toml"
    if grep -q "\"bodies:gfx/magenta_person\" = $GFXID" "$bodies_ids" 2>/dev/null \
            && grep -q "\"bodies:prop/cool_house\" = $PROPID" "$bodies_ids" 2>/dev/null; then
        ok "the cook froze bodies:gfx/magenta_person at $GFXID and bodies:prop/cool_house at $PROPID"
    else
        bad "the cook froze the bodies gfx and prop at the records' ids"
        cat "$bodies_ids" 2>/dev/null || true
    fi

    bgfx="$tmp/bodies/.cooked/generated/billboard_gfx.txt"
    eprop="$tmp/bodies/.cooked/generated/extra_props.txt"
    mmodel_idx=
    extra_prop=
    if [ -f "$bgfx" ]; then
        mmodel_idx=$(awk '{print $2; exit}' "$bgfx")
    fi
    if [ -f "$eprop" ]; then
        extra_prop=$(awk '{print $1; exit}' "$eprop")
    fi
    if [ -n "$mmodel_idx" ] && grep -q "^$GFXID $mmodel_idx$" "$bgfx" 2>/dev/null; then
        ok "billboard_gfx.txt points gfx $GFXID at mmodel $mmodel_idx"
    else
        bad "billboard_gfx.txt points gfx $GFXID at an appended mmodel member"
        cat "$bgfx" 2>/dev/null || true
    fi
    if [ "$extra_prop" = "$PROPID" ]; then
        ok "extra_props.txt lists prop $PROPID"
    else
        bad "extra_props.txt lists prop $PROPID"
        cat "$eprop" 2>/dev/null || true
    fi

    mmodel_file="$tmp/bodies/.cooked/narc/$MMODEL/$mmodel_idx"
    prop_file="$tmp/bodies/.cooked/narc/$BUILD_MODEL/$PROPID"
    if [ -n "$mmodel_idx" ] && [ -f "$mmodel_file" ] \
            && [ "$(head -c 4 "$mmodel_file")" = "BTX0" ]; then
        ok "the cook wrote mmodel/$mmodel_idx as an NSBTX"
    else
        bad "the cook wrote an NSBTX at mmodel/$mmodel_idx"
    fi
    if [ -n "$PROPID" ] && [ -f "$prop_file" ] \
            && [ "$(head -c 4 "$prop_file")" = "BMD0" ] \
            && ! grep -a -F TEX0 "$prop_file" >/dev/null; then
        ok "the cook wrote build_model/$PROPID as an untextured NSBMD"
    else
        bad "the cook wrote an untextured NSBMD at build_model/$PROPID"
    fi

    if [ -n "${SPECIESID:-}" ]; then
        if grep -q "\"sprigatito:species/sprigatito\" = $SPECIESID" \
                "$tmp/sprigatito/.cooked/ids.toml" 2>/dev/null; then
            ok "the cook froze sprigatito:species/sprigatito at $SPECIESID"
        else
            bad "the cook froze sprigatito:species/sprigatito at $SPECIESID"
            cat "$tmp/sprigatito/.cooked/ids.toml" 2>/dev/null || true
        fi
        face0=$((SPECIESID * 6))
        poke_n=0
        if [ -d "$tmp/sprigatito/.cooked/narc/$POKEGRA" ]; then
            poke_n=$(ls "$tmp/sprigatito/.cooked/narc/$POKEGRA" | wc -l)
        fi
        if [ -f "$tmp/sprigatito/.cooked/narc/$POKEGRA/$face0" ] \
                && [ "$poke_n" -ge 6 ]; then
            ok "the cook wrote pl_pokegra/$face0 and $poke_n members"
        else
            bad "the cook wrote species $SPECIESID's pl_pokegra members"
        fi
    fi

    # Every record id the author pinned must be the id the cook froze.
    # A missing id here is a failed test, not a walk-there-and-see.
    for pkg in $authored; do
        rec_ids=$(if [ -d "$MODS/$pkg/records" ]; then
            find "$MODS/$pkg/records" -name '*.json' | while read -r f; do
                sed -n 's/.*"id"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$f"
            done | sort -n
        fi)
        cooked_ids=$(sed -n 's/.*= \([0-9][0-9]*\)$/\1/p' \
            "$tmp/$pkg/.cooked/ids.toml" 2>/dev/null | sort -n)
        rec_show=$(printf '%s' "$rec_ids" | tr '\n' ' ')
        cooked_show=$(printf '%s' "$cooked_ids" | tr '\n' ' ')
        if [ -n "$rec_ids" ] && [ "$rec_ids" = "$cooked_ids" ]; then
            ok "$pkg record ids match the cook freeze ($rec_show)"
        else
            bad "$pkg record ids match the cook freeze"
            echo "       records: ${rec_show:-none}"
            echo "       cooked:  ${cooked_show:-none}"
        fi
    done
fi

echo "the windowed play path forwards PC_MODS:"

# play.sh is the other front door. The engine defaults PC_MODS_DIR to pc/mods;
# this script must point it at this repo's mods/ and must not invent a package
# list (an unset PC_MODS loads nothing, including no hub). --print-env is the
# same array the process would get.
if [ ! -x "$FUSED" ]; then
    echo "  SKIP (no fused build: run \`make -C mmo fused\`)"
elif [ ! -f "$ROM" ]; then
    echo "  SKIP (no ROM at $ROM)"
else
    mods_abs=$(cd "$MODS" && pwd)
    if env -u PC_MODS -u PC_MODS_DIR -u PC_MODFS \
            "$ROOT/play.sh" --print-env --rom "$ROM" \
            >"$tmp/play.env" 2>"$tmp/play.err"; then
        if grep -q "^PC_MODS_DIR=$mods_abs$" "$tmp/play.env" \
                && grep -q "^PC_ROM=$ROM$" "$tmp/play.env"; then
            ok "play.sh points PC_MODS_DIR at mods/"
        else
            bad "play.sh points PC_MODS_DIR at mods/"
            cat "$tmp/play.env" "$tmp/play.err"
        fi
        if grep -q "^PC_MODS=" "$tmp/play.env"; then
            bad "unset PC_MODS is not invented as a package list"
        else
            ok "unset PC_MODS is not invented as a package list"
        fi
    else
        bad "play.sh --print-env runs"
        cat "$tmp/play.err"
    fi
    if env -u PC_MODS_DIR -u PC_MODFS PC_MODS=hub \
            "$ROOT/play.sh" --print-env --rom "$ROM" \
            >"$tmp/play.env" 2>"$tmp/play.err" \
            && grep -q "^PC_MODS=hub$" "$tmp/play.env" \
            && grep -q "^PC_MODS_DIR=$mods_abs$" "$tmp/play.env"; then
        ok "play.sh forwards PC_MODS=hub into the port environment"
    else
        bad "play.sh forwards PC_MODS=hub into the port environment"
        cat "$tmp/play.env" "$tmp/play.err" 2>/dev/null || true
    fi
    if "$ROOT/play.sh" --print-env --rom "$ROM" --mods bodies,hub \
            --mods-dir "$tmp" >"$tmp/play.env" 2>"$tmp/play.err" \
            && grep -q "^PC_MODS=bodies,hub$" "$tmp/play.env" \
            && grep -q "^PC_MODS_DIR=$tmp$" "$tmp/play.env"; then
        ok "--mods and --mods-dir override the environment"
    else
        bad "--mods and --mods-dir override the environment"
        cat "$tmp/play.env" "$tmp/play.err" 2>/dev/null || true
    fi
fi

echo "the fused build serves the hub over the player's image:"

# One boot. The probe variables stop the run at the read they name, so
# each of these is a fraction of a second rather than a paced boot.
boot() { # LOG VAR=VALUE ...
    _log=$1; shift
    env -u PC_MODS -u PC_MODS_DIR -u PC_MODFS \
        PC_ROM="$ROM" PC_SAVE=none PC_FRAMES=60 PC_PACE=0 \
        "$@" "$FUSED" > "$_log" 2>&1 && echo 0 || echo $?
}

if [ ! -x "$FUSED" ]; then
    echo "  SKIP (no fused build: run \`make -C mmo fused\`)"
elif [ ! -f "$ROM" ]; then
    echo "  SKIP (no ROM at $ROM)"
elif [ -z "$cooked" ]; then
    echo "  SKIP (nothing cooked to serve)"
else
    rom_before=$(stamp "$ROM")

    rc=$(boot "$tmp/map.log" PC_MODS_DIR="$tmp" PC_MODS=bodies,hub \
              PC_MODFS_PROBE_MAP="$MAPID")
    if [ "$rc" -ne 0 ]; then
        bad "a boot with the two packages returns (exit $rc)"
        tail -3 "$tmp/map.log"
    elif grep -q 'modfs: .*order=\[bodies, hub\]' "$tmp/map.log"; then
        ok "both packages load, in the order they were listed"
    else
        bad "both packages load, in the order they were listed"
        grep -a modfs "$tmp/map.log" || true
    fi

    if grep -q "modfs: probe-map $MAPID area=${AREA:-} matrix=$matrix_idx" \
            "$tmp/map.log"; then
        ok "the game's own getters answer for header $MAPID (area $AREA matrix $matrix_idx)"
    else
        bad "the game's own getters answer for header $MAPID (area $AREA matrix $matrix_idx)"
        grep -a probe-map "$tmp/map.log" || true
    fi

    rc=$(boot "$tmp/count.log" PC_MODS_DIR="$tmp" PC_MODS=bodies,hub \
              PC_MODFS_PROBE_COUNT="$LAND/$land_idx")
    grew=$(sed -n "s|.*probe-count $LAND \([0-9]*\) $land_idx \([0-9]*\) .*|\1 \2|p" \
           "$tmp/count.log")
    set -- ${grew:-}
    count=${1:-}
    size=${2:-}
    if [ "$rc" -ne 0 ] || [ -z "$count" ]; then
        bad "the appended land-data member reads back through the game's NARC path"
        tail -3 "$tmp/count.log"
    elif [ "$count" -eq $((land_idx + 1)) ] && [ "$size" -gt 0 ]; then
        ok "land_data grew to $count members and member $land_idx is $size bytes"
    else
        bad "land_data grows by exactly the member the cook appended"
        echo "       count $count, member $land_idx, size $size"
    fi

    rc=$(boot "$tmp/vanilla.log" PC_MODFS_PROBE_COUNT="$LAND/$land_idx")
    if [ "$rc" -ne 0 ] \
            && grep -q "member not claimed: $LAND/$land_idx" "$tmp/vanilla.log"; then
        ok "without the packages the cartridge does not have that member"
    else
        bad "without the packages the cartridge does not have that member"
        tail -2 "$tmp/vanilla.log"
    fi

    rc=$(boot "$tmp/first.log" PC_MODS_DIR="$tmp" PC_MODS=bodies,hub \
              PC_MODFS_PROBE_COUNT="$LAND/0")
    if [ "$rc" -ne 0 ] \
            && grep -q "member not claimed: $LAND/0" "$tmp/first.log"; then
        ok "with them on, a vanilla member is still the image's"
    else
        bad "with them on, a vanilla member is still the image's"
        tail -2 "$tmp/first.log"
    fi

    rc=$(boot "$tmp/plugin.log" PC_MODS_DIR="$MODS" PC_MODS=openmmo)
    if [ "$rc" -ne 0 ] && grep -q "compile-time plugin; use MODS=" "$tmp/plugin.log"; then
        ok "'openmmo' in PC_MODS is a boot error that names the other door"
    else
        bad "'openmmo' in PC_MODS is a boot error that names the other door"
        tail -2 "$tmp/plugin.log"
    fi

    rc=$(boot "$tmp/requires.log" PC_MODS_DIR="$tmp" PC_MODS=hub \
              PC_MODFS_PROBE_MAP="$MAPID")
    if [ "$rc" -ne 0 ] && grep -q "requires 'bodies'" "$tmp/requires.log"; then
        ok "the hub without what it requires refuses by name"
    else
        bad "the hub without what it requires refuses by name"
        tail -2 "$tmp/requires.log"
    fi

    echo "the fused build serves the bodies package on its own:"

    if [ -z "${mmodel_idx:-}" ] || [ -z "${PROPID:-}" ]; then
        bad "bodies has a cooked mmodel member and a prop id to probe"
    else
        rc=$(boot "$tmp/bodies.log" PC_MODS_DIR="$tmp" PC_MODS=bodies \
                  PC_MODFS_PROBE_COUNT="$MMODEL/$mmodel_idx")
        grew=$(sed -n "s|.*probe-count $MMODEL \([0-9]*\) $mmodel_idx \([0-9]*\) .*|\1 \2|p" \
               "$tmp/bodies.log")
        set -- ${grew:-}
        count=${1:-}
        size=${2:-}
        if [ "$rc" -ne 0 ]; then
            bad "a boot with only bodies returns (exit $rc)"
            tail -3 "$tmp/bodies.log"
        elif grep -q 'modfs: .*order=\[bodies\]' "$tmp/bodies.log"; then
            ok "bodies loads on its own"
        else
            bad "bodies loads on its own"
            grep -a modfs "$tmp/bodies.log" || true
        fi
        if [ -z "$count" ]; then
            bad "the appended mmodel member reads back through the game's NARC path"
            tail -3 "$tmp/bodies.log"
        elif [ "$count" -eq $((mmodel_idx + 1)) ] && [ "$size" -gt 0 ]; then
            ok "mmodel grew to $count members and member $mmodel_idx is $size bytes"
        else
            bad "mmodel grows by exactly the member the cook appended"
            echo "       count $count, member $mmodel_idx, size $size"
        fi

        rc=$(boot "$tmp/prop.log" PC_MODS_DIR="$tmp" PC_MODS=bodies \
                  PC_MODFS_PROBE_COUNT="$BUILD_MODEL/$PROPID")
        grew=$(sed -n "s|.*probe-count $BUILD_MODEL \([0-9]*\) $PROPID \([0-9]*\) .*|\1 \2|p" \
               "$tmp/prop.log")
        set -- ${grew:-}
        count=${1:-}
        size=${2:-}
        if [ "$rc" -ne 0 ] || [ -z "$count" ]; then
            bad "the appended build_model member reads back through the game's NARC path"
            tail -3 "$tmp/prop.log"
        elif [ "$count" -eq $((PROPID + 1)) ] && [ "$size" -gt 0 ]; then
            ok "build_model grew to $count members and member $PROPID is $size bytes"
        else
            bad "build_model grows by exactly the member the cook appended"
            echo "       count $count, member $PROPID, size $size"
        fi

        rc=$(boot "$tmp/vanilla-mmodel.log" PC_MODFS_PROBE_COUNT="$MMODEL/$mmodel_idx")
        if [ "$rc" -ne 0 ] \
                && grep -q "member not claimed: $MMODEL/$mmodel_idx" \
                    "$tmp/vanilla-mmodel.log"; then
            ok "without the packages the cartridge does not have that mmodel member"
        else
            bad "without the packages the cartridge does not have that mmodel member"
            tail -2 "$tmp/vanilla-mmodel.log"
        fi

        rc=$(boot "$tmp/vanilla-prop.log" PC_MODFS_PROBE_COUNT="$BUILD_MODEL/$PROPID")
        if [ "$rc" -ne 0 ] \
                && grep -q "member not claimed: $BUILD_MODEL/$PROPID" \
                    "$tmp/vanilla-prop.log"; then
            ok "without the packages the cartridge does not have that prop member"
        else
            bad "without the packages the cartridge does not have that prop member"
            tail -2 "$tmp/vanilla-prop.log"
        fi

        rc=$(boot "$tmp/mmodel0.log" PC_MODS_DIR="$tmp" PC_MODS=bodies \
                  PC_MODFS_PROBE_COUNT="$MMODEL/0")
        if [ "$rc" -ne 0 ] \
                && grep -q "member not claimed: $MMODEL/0" "$tmp/mmodel0.log"; then
            ok "with bodies on, a vanilla mmodel member is still the image's"
        else
            bad "with bodies on, a vanilla mmodel member is still the image's"
            tail -2 "$tmp/mmodel0.log"
        fi
    fi

    echo "the fused build serves every authored package:"

    order_want=$(printf '%s' "$authored_csv" | sed 's/,/, /g')
    rc=$(boot "$tmp/all.log" PC_MODS_DIR="$tmp" PC_MODS="$authored_csv" \
              PC_MODFS_PROBE_MAP="$MAPID")
    if [ "$rc" -ne 0 ]; then
        bad "a boot with every authored package returns (exit $rc)"
        tail -3 "$tmp/all.log"
    elif grep -q "modfs: .*order=\\[$order_want\\]" "$tmp/all.log"; then
        ok "every authored package loads, in the order they were listed"
    else
        bad "every authored package loads, in the order they were listed"
        grep -a modfs "$tmp/all.log" || true
    fi

    for pkg in $authored; do
        [ -d "$MODS/$pkg/records/maps" ] || continue
        for rec in "$MODS/$pkg/records/maps"/*.json; do
            [ -f "$rec" ] || continue
            hid=$(sed -n 's/.*"id"[[:space:]]*:[[:space:]]*\([0-9]*\).*/\1/p' \
                  "$rec" | head -1)
            [ -n "$hid" ] || continue
            rc=$(boot "$tmp/map-$hid.log" PC_MODS_DIR="$tmp" \
                      PC_MODS="$authored_csv" PC_MODFS_PROBE_MAP="$hid")
            got=$(sed -n "s/.*probe-map $hid area=\\([0-9]*\\) matrix=\\([0-9]*\\).*/\\1 \\2/p" \
                  "$tmp/map-$hid.log")
            set -- ${got:-}
            area_got=${1:-}
            matrix_got=${2:-}
            if [ "$rc" -ne 0 ] || [ -z "$area_got" ]; then
                bad "header $hid answers through the game's own getters"
                grep -a probe-map "$tmp/map-$hid.log" || tail -2 "$tmp/map-$hid.log"
            elif [ "$matrix_got" = 0 ]; then
                bad "header $hid answers with a real matrix, not the past-the-end 0"
                echo "       area=$area_got matrix=$matrix_got"
            else
                ok "header $hid answers (area $area_got matrix $matrix_got)"
            fi
        done
    done

    for pkg in $authored; do
        [ -d "$MODS/$pkg/records/species" ] || continue
        for rec in "$MODS/$pkg/records/species"/*.json; do
            [ -f "$rec" ] || continue
            sid=$(sed -n 's/.*"id"[[:space:]]*:[[:space:]]*\([0-9]*\).*/\1/p' \
                  "$rec" | head -1)
            [ -n "$sid" ] || continue
            face0=$((sid * 6))
            want_count=$((face0 + 6))
            rc=$(boot "$tmp/poke-$sid.log" PC_MODS_DIR="$tmp" \
                      PC_MODS="$authored_csv" \
                      PC_MODFS_PROBE_COUNT="$POKEGRA/$face0")
            grew=$(sed -n "s|.*probe-count $POKEGRA \\([0-9]*\\) $face0 \\([0-9]*\\) .*|\\1 \\2|p" \
                   "$tmp/poke-$sid.log")
            set -- ${grew:-}
            count=${1:-}
            size=${2:-}
            if [ "$rc" -ne 0 ] || [ -z "$count" ]; then
                bad "species $sid's pl_pokegra member $face0 reads back"
                tail -3 "$tmp/poke-$sid.log"
            elif [ "$count" -eq "$want_count" ] && [ "$size" -gt 0 ]; then
                ok "pl_pokegra grew to $count members and member $face0 is $size bytes"
            else
                bad "pl_pokegra grows to $want_count and member $face0 is non-empty"
                echo "       count $count, member $face0, size $size"
            fi

            rc=$(boot "$tmp/vanpoke-$sid.log" \
                      PC_MODFS_PROBE_COUNT="$POKEGRA/$face0")
            if [ "$rc" -ne 0 ] \
                    && grep -q "member not claimed: $POKEGRA/$face0" \
                        "$tmp/vanpoke-$sid.log"; then
                ok "without the packages the cartridge does not have pl_pokegra/$face0"
            else
                bad "without the packages the cartridge does not have pl_pokegra/$face0"
                tail -2 "$tmp/vanpoke-$sid.log"
            fi

            rc=$(boot "$tmp/sprite-$sid.log" PC_MODS_DIR="$tmp" \
                      PC_MODS="$authored_csv" PC_LAB_SPRITE="$sid")
            if [ "$rc" -eq 0 ] \
                    && grep -q "pc_lab: sprite species=$sid " "$tmp/sprite-$sid.log" \
                    && grep -q "pl_pokegra=$want_count" "$tmp/sprite-$sid.log" \
                    && grep -q "failed=0" "$tmp/sprite-$sid.log"; then
                ok "PC_LAB_SPRITE on $sid answers through the overlay (pl_pokegra=$want_count)"
            else
                bad "PC_LAB_SPRITE on $sid answers through the overlay"
                grep -a pc_lab "$tmp/sprite-$sid.log" || tail -3 "$tmp/sprite-$sid.log"
            fi

            if grep -q "openmmo: sprite overlay pl_pokegra=$want_count" \
                    "$tmp/sprite-$sid.log" \
                    && grep -q "openmmo: sprite species $sid pokegra " \
                        "$tmp/sprite-$sid.log"; then
                ok "sprite.c locates $sid against the overlay's $want_count members"
            else
                bad "sprite.c locates $sid against the overlay's $want_count members"
                grep -a 'openmmo: sprite' "$tmp/sprite-$sid.log" || true
            fi

            if [ -x "$CLIENT" ]; then
                if "$CLIENT" sprite --species "$sid" \
                        --pokegra-members "$want_count" \
                        >"$tmp/sprite-cli-$sid.log" 2>&1 \
                        && grep -q "species $sid " "$tmp/sprite-cli-$sid.log"; then
                    ok "openmmo-client sprite locates $sid at $want_count members"
                else
                    bad "openmmo-client sprite locates $sid at $want_count members"
                    cat "$tmp/sprite-cli-$sid.log"
                fi
                if "$CLIENT" sprite --species "$sid" \
                        >"$tmp/sprite-cli-rom-$sid.log" 2>&1; then
                    bad "without the grown count sprite.c still refuses $sid"
                    cat "$tmp/sprite-cli-rom-$sid.log"
                else
                    ok "without the grown count sprite.c still refuses $sid"
                fi
                next=$((sid + 1))
                if "$CLIENT" sprite --species "$next" \
                        --pokegra-members "$want_count" \
                        >"$tmp/sprite-cli-next-$sid.log" 2>&1; then
                    bad "species $next is still refused at $want_count members"
                    cat "$tmp/sprite-cli-next-$sid.log"
                else
                    ok "species $next is still refused at $want_count members"
                fi
            fi
        done
    done

    echo "the fused build draws the hub:"

    # PC_LAB, not a planted vanilla door. content/events/ replaces that
    # map's whole event list. The 1x1 matrix has no neighbours, so the
    # fused assertion hook prints on every missing adjacent cell, 
    # warn, not fatal. The picture is the oracle.
    if [ -z "${msg_idx:-}" ] || [ -z "${GFXID:-}" ]; then
        bad "the dump has a text bank and a person gfx id to look for"
    else
        rc=$(boot "$tmp/text.log" PC_MODS_DIR="$tmp" PC_MODS=bodies,hub \
                  PC_LAB_TEXT="$msg_idx")
        if [ "$rc" -eq 0 ] \
                && grep -q "pc_lab: text bank=$msg_idx entries=1 rendered=1" \
                    "$tmp/text.log" \
                && grep -q "failed=0" "$tmp/text.log"; then
            ok "text bank $msg_idx renders one string and reports failed=0"
        else
            bad "text bank $msg_idx renders one string and reports failed=0"
            grep -a pc_lab "$tmp/text.log" || tail -3 "$tmp/text.log"
        fi

        printf 'map %s 20 9 1\n' "$MAPID" > "$tmp/hub.lab"
        mkdir -p "$tmp/hub-field"
        rc=$(boot "$tmp/field.log" OPENMMO_ASSERT=warn \
                  PC_MODS_DIR="$tmp" PC_MODS="$authored_csv" \
                  PC_SAVE="$tmp/hub.sav" PC_FRAMES=3000 \
                  PC_LAB="$tmp/hub.lab" PC_LAB_AT=1800 \
                  PC_LAB_MAPSCAN="$tmp/hub.scan" \
                  PC_DUMP_FRAMES="$tmp/hub-field" PC_DUMP_FROM=2240 \
                  PC_TRACE_SCRIPT=1800 \
                  PC_SCRIPT_COV="$tmp/hub.cov")
        if [ "$rc" -ne 0 ]; then
            bad "a lab warp onto header $MAPID returns (exit $rc)"
            grep -aE 'pc_lab:|pc-script: f=1813 op=0023' "$tmp/field.log" || \
                tail -5 "$tmp/field.log"
        elif ! grep -q 'pc_lab: applied 1 line(s) at frame 2250' "$tmp/field.log"
        then
            bad "the lab applies the warp at frame 2250"
            grep -a pc_lab "$tmp/field.log" || true
        elif ! grep -q 'op=0023' "$tmp/field.log"; then
            bad "OnTransition fires SetTrainerFlag (op=0023)"
        elif ! grep -q "map $MAPID" "$tmp/hub.scan" 2>/dev/null; then
            bad "MAPSCAN lands on header $MAPID"
            head -5 "$tmp/hub.scan" 2>/dev/null || true
        elif ! grep -q 'player 20 9 ' "$tmp/hub.scan"; then
            bad "MAPSCAN stands the player at 20, 9"
            grep -a '^player ' "$tmp/hub.scan" || true
        elif ! grep -q "object gfx $GFXID script 2 at 20 10" "$tmp/hub.scan"; then
            bad "MAPSCAN sees gfx $GFXID script 2 at 20, 10"
            grep -a '^object ' "$tmp/hub.scan" || true
        else
            ok "PC_LAB map $MAPID 20 9 lands there with gfx $GFXID on (20,10)"
        fi

        dump="$tmp/hub-field/frame-002249.png"
        if [ ! -f "$dump" ]; then
            bad "the dump wrote frame 2249"
            ls "$tmp/hub-field" 2>/dev/null | head -8
        else
            pix=$(python3 - "$dump" <<'PY' || echo "0 0"
import struct, sys, zlib
path = sys.argv[1]
data = open(path, "rb").read()
if data[:8] != b"\x89PNG\r\n\x1a\n":
    print("0 0")
    raise SystemExit
pos, idat, w, h = 8, b"", None, None
while pos < len(data):
    (length,) = struct.unpack(">I", data[pos:pos + 4])
    ctype = data[pos + 4:pos + 8]
    body = data[pos + 8:pos + 8 + length]
    if ctype == b"IHDR":
        w, h = struct.unpack(">II", body[:8])
    elif ctype == b"IDAT":
        idat += body
    pos += 12 + length
raw = zlib.decompress(idat)
if (w, h) != (256, 384) or len(raw) != h * (1 + w * 3):
    print("0 0")
    raise SystemExit
stride = 1 + w * 3
magenta = cyan = 0
for y in range(192):
    if raw[y * stride] != 0:
        print("0 0")
        raise SystemExit
    row = raw[y * stride + 1:(y + 1) * stride]
    for i in range(0, w * 3, 3):
        r, g, b = row[i], row[i + 1], row[i + 2]
        if r == 255 and g == 0 and b == 255:
            magenta += 1
        elif r < 80 and g > 180 and b > 180:
            cyan += 1
print("%d %d" % (magenta, cyan))
PY
)
            set -- ${pix:-}
            magenta=${1:-0}
            cyan=${2:-0}
            if [ "$magenta" -ge 100 ] && [ "$cyan" -ge 100 ]; then
                ok "frame 2249 top screen has the magenta person ($magenta) and cyan prop ($cyan)"
            else
                bad "frame 2249 top screen has the magenta person and cyan prop"
                echo "       magenta $magenta cyan $cyan (want >= 100 each)"
            fi
        fi

        echo "the hub field load is a script-coverage run:"

        if [ ! -f "$ENGINE/pc/tests/pc_scrcov.py" ]; then
            echo "  SKIP (no pc_scrcov.py under $ENGINE)"
        elif [ ! -f "$tmp/hub.cov" ]; then
            bad "the hub field load wrote a PC_SCRIPT_COV file"
        else
            names=$(PYTHONPATH="$ENGINE/pc/tests" python3 - "$tmp/hub.cov" <<'PY' || true
import sys
import pc_scrcov
per = pc_scrcov.read([sys.argv[1]])
print(" ".join(sorted(pc_scrcov.names(pc_scrcov.merge(per)).get("field", []))))
PY
)
            if printf '%s\n' "$names" | grep -F -q SCRCMD_SETTRAINERFLAG \
                    && printf '%s\n' "$names" | grep -F -q SCRCMD_END; then
                ok "pc_scrcov.py names SCRCMD_SETTRAINERFLAG and SCRCMD_END from the hub field load"
            else
                bad "pc_scrcov.py names SCRCMD_SETTRAINERFLAG and SCRCMD_END from the hub field load"
                echo "       field: ${names:-none}"
            fi
        fi
    fi

    if [ "$rom_before" = "$(stamp "$ROM")" ]; then
        ok "the player's image did not move across any of it"
    else
        bad "the player's image did not move across any of it"
    fi
fi

if [ "$fail" -ne 0 ]; then
    echo "overlay: FAILED"
    exit 1
fi
echo "overlay: all checks passed"
