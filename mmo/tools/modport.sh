#!/bin/sh
# Fill a content package from a cartridge the player already owns.
#
#   modport.sh <engine-dir> <package-dir> <rom> [out-dir]
#   Platinum   native. Nothing to port; the client already draws it.
#   HeartGold  sprites. Six members a species, the same NCGR the loader reads,
#              proved by reading the ported member back through the game.
#   Diamond    sprites, since 2026-08-21. Its sheets are the same container and
#              the same dimensions as Platinum's and scramble the OTHER WAY,
#              the engine's own tools/nitrogfx/gfx.c walks that obfuscation
#              back-to-front for Diamond and front-to-back for Platinum, so a
#              fill re-encodes them on the way in. That direction, and nothing
#              else, was "the sheet does not decode through this sprite path".
#   Black      nothing. A species is not six members there and the sheets are
#              compressed; mmo/tests/import_test.sh measures the shape.
#   map/music  ported by their own tools (tools/portmap.py, tools/portmusic.py)
#              and named here so a recipe line points at them. A map needs a
#              header id, a label and a bgm; the recipe grammar has no column
#              for any of them yet.
#   follower   the same shape again: tools/portfollow.py, since 2026-08-26. A
#              recipe line is a pairing and a follower fill is not one, there
#              is no Sinnoh follower for a Johto one to land on, all 566 cross
#              at once because appended members must run without a gap, and
#              what a species gets is a graphics id that did not exist before.
#   text       refused by name, in the recipe loop. The design notes.
set -eu

if [ $# -lt 3 ]; then
    echo "usage: modport.sh <engine-dir> <package-dir> <rom> [out-dir]" >&2
    exit 2
fi

ENGINE=$1
PKG=$2
ROM=$3
OUT=${4:-$2}

RECIPE="$PKG/port.recipe"

[ -d "$ENGINE/pc" ]  || { echo "modport: no engine port at $ENGINE/pc" >&2; exit 1; }
[ -f "$RECIPE" ]     || { echo "modport: no recipe at $RECIPE" >&2; exit 1; }
[ -f "$PKG/mod.toml" ] || { echo "modport: no mod.toml at $PKG/mod.toml" >&2; exit 1; }
if [ -d "$ROM" ]; then
    echo "modport: $ROM is a directory, a decompilation checkout is not a" \
         "source for this client. Content crosses from a cartridge image the" \
         "player owns, not from a source tree; hand this an .nds file" >&2
    exit 1
fi
[ -f "$ROM" ]        || { echo "modport: no ROM at $ROM" >&2; exit 1; }

PYTHON=${PYTHON:-python3}

# A kind that no cartridge can serve is refused first, off the recipe alone.
while read -r want kind src dst rest; do
    case "$want" in ''|\#*) continue ;; esac
    case "$kind" in
    mmodel|overworld)
        # Refused on a measurement. The two archives are the same mixture of
        # resources, Platinum 421 BTX0, 24 BMD0 and 4 LZ77 members in 470,
        # HeartGold 832 / 14 / 3 in 863, and they share 108 members' content
        # while agreeing on only 11 indices, so the id spaces are unrelated and
        # a table is needed. What is missing is anything to build that table
        # from: an item's two members and a trainer's five are a function of a
        # named id in a tree we can read, and an overworld person's are not.
        # Platinum reaches 470 members from 278 OBJ_EVENT_GFX ids through a
        # lookup that lives in undecompiled overlay 5, so there is no source
        # here that names one member. The day that lookup is named this becomes
        # a kind; until then it is a refusal and not a gap.
        echo "modport: $RECIPE: '$kind' is refused rather than unported --" \
             "an overworld person's members are not a function of any id named" \
             "in a tree we can read (Platinum's 278 gfx ids reach 470 members" \
             "through undecompiled overlay 5), so there is nothing to build a" \
             "table from; see mmo/ASSETS.md" >&2
        exit 1 ;;
    build_model|building)
        # Also a measurement. Platinum keeps one 590-member archive where
        # HeartGold has two (bm_field 340 + bm_room 222) and the two share four
        # members in total, so nothing lines up. Worse for a port: a building's
        # textures are not in the archive at all, they come from the area
        # texture set the map names, so a donated model arrives untextured
        # even when it is placed, which is what mods/bodies' own untextured
        # prop already shows.
        echo "modport: $RECIPE: '$kind' is refused rather than unported --" \
             "a building's textures live in the area texture set its map" \
             "names, not in the model archive, so a ported one draws" \
             "untextured; the two archives share four members in any case;" \
             "see mmo/ASSETS.md" >&2
        exit 1 ;;
    cry)
        # A cry is a sound, and a sound here is one SDAT record among the
        # 2133 / 771 / 771 measured below, so it is the music refusal, and
        # naming it separately is worth it because a player asking for a cry
        # is not asking for a track and should not have to work out that they
        # got the same answer.
        echo "modport: $RECIPE: '$kind' is refused rather than unported --" \
             "a cry is a record inside the cartridge's one SDAT, so it is the" \
             "'music' refusal: the parts reference each other by index and a" \
             "sound cannot be a member; see mmo/ASSETS.md" >&2
        exit 1 ;;
    music|sound|sdat)
        # Not A RECIPE line, and no longer a refusal about the format. An SDAT
        # is one container rather than an archive of members, Platinum's holds
        # 2133 sequences, 771 banks and 771 wave archives, HeartGold's 2379 /
        # 778 / 778, and every reference between them is an index into its own
        # Tables, so "port member N" has no meaning and a track crosses only by
        # writing its bank and waves in beside ours and renumbering. That is an
        # SDAT writer, and since 2026-08-21 there is one: mmo/tools/portmusic.py.
        # What is still missing is a way to say it here, because a track needs a
        # destination sequence id that the recipe grammar has no column for. Run
        # the tool directly until it does.
        echo "modport: $RECIPE: '$kind' is not a kind this driver carries --" \
             "an SDAT is one container (2133 sequences / 771 banks / 771 wave" \
             "archives here, 2379 / 778 / 778 there) whose parts reference" \
             "each other by index, so a track is not a member;" \
             "mmo/tools/portmusic.py writes one in, and mmo/ASSETS.md has the" \
             "measurement" >&2
        exit 1 ;;
    map)
        # Same shape: the porter exists, the recipe grammar does not reach it.
        # mmo/tools/portmap.py converts HeartGold maps into this game's field
        # archives, translating every tile through mmo/TERRAIN_MAP, which is
        # what used to be missing and what the design notes refused on, and
        # carrying the buildings, the people and the doors with them. It needs a
        # map header id, a place name and a BGM per map to be addressable, and a
        # run takes several maps at once because a package's members have to
        # start at the image's counts and run without a gap. None of that is a
        # column here. Scripts still do not cross, so a ported map is silent;
        # that refusal is on its own page.
        echo "modport: $RECIPE: '$kind' is not a kind this driver carries --" \
             "mmo/tools/portmap.py ports one, and needs a header id, a label" \
             "and a bgm that a recipe line has no column for; see" \
             "mmo/MAPFORMATS.md" >&2
        exit 1 ;;
    follower|follow_mon|tsurepoke)
        # Same shape as map and music: the porter exists, the grammar does not
        # reach it. A recipe line is a pairing, this item's icon onto that
        # item's, and a follower fill is not one. There is no Sinnoh follower
        # for a Johto one to land on, the whole set crosses at once because
        # appended members have to run without a gap, and what a species gets is
        # a graphics id that did not exist before. mmo/tools/portfollow.py does
        # it, checking the three oracles the design notes measures before it writes
        # a byte.
        echo "modport: $RECIPE: '$kind' is not a kind this driver carries --" \
             "a follower is appended with a graphics id of its own rather than" \
             "landing on one of this game's, and all 566 cross at once;" \
             "mmo/tools/portfollow.py fills a package with them, and" \
             "mmo/ASSETS.md has the measurement" >&2
        exit 1 ;;
    text)
        # Still refused on the measurement. A foreign region's script opcodes
        # are numbered by the game they came from and no source this repository
        # can read names one of them, in any generation; the design notes counts it.
        echo "modport: $RECIPE: '$kind' is refused rather than unported --" \
             "a foreign region's script opcodes have no oracle in any source" \
             "here; see mmo/MAPFORMATS.md" >&2
        exit 1 ;;
    esac
done < "$RECIPE"

# What the image says it is. The porter prints one line; the code is its first
# word, and anything else here is an image this tool cannot reason about.
ident=$("$PYTHON" "$ENGINE/pc/modport.py" --rom "$ROM" --identify) || exit 1
code=${ident#port: }
code=${code%% *}
case "$code" in
[A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9]) ;;
*) echo "modport: could not read a game code out of $ROM ($ident)" >&2; exit 1 ;;
esac

# Which slot this image fills.
REGISTRY=${OPENMMO_CARTRIDGES:-$(dirname "$0")/../CARTRIDGES}
if [ ! -f "$REGISTRY" ]; then
    echo "modport: no cartridge registry at $REGISTRY" >&2
    exit 1
fi

# code slot name status kinds, from the row for this code or, failing that,
# the first row of the same game. Prints nothing when neither exists.
cart_row() {
    awk -v want="$1" '
        /^[[:space:]]*(#|$)/ { next }
        $1 == "slot" { line = $0
                       sub(/^slot[[:space:]]+[^ \t]+[[:space:]]+"/, "", line)
                       sub(/"[[:space:]]*$/, "", line)
                       name[$2] = line; next }
        $1 == "lang" { next }
        {
            note = $0
            sub(/^([^ \t]+[ \t]+){4}/, "", note)   # past code slot status kinds
            sub(/^"[^"]*"[ \t]+/, "", note)         # past a quoted title
            sub(/^-[ \t]+/, "", note)               # or an absent one
            if ($1 == want)          { exact = $1 "|" $2 "|" $3 "|" $4 "|" note }
            else if (substr($1, 1, 3) == substr(want, 1, 3) && slot == "")
                                     { slot = $1 "|" $2 "|" $3 "|" $4 "|" note }
        }
        END {
            row = (exact != "") ? exact : slot
            if (row == "") exit 1
            split(row, f, "|")
            print f[1] "|" f[2] "|" name[f[2]] "|" f[3] "|" f[4] "|" f[5]
        }
    ' "$REGISTRY"
}

# Which direction each game scrambles its sprite character data. A game that
# disagrees with this one has its sheets re-encoded after the porter writes
# them; that disagreement is the whole of "Diamond extracts and does not draw".
sprite_mode() {
    awk -v c="$1" '$1 == "sprite" && $2 == c { print $3; found = 1 }
                   END { exit !found }' "$REGISTRY" 2>/dev/null || echo 0
}
HOST_CODE=CPUE
host_scramble=$(sprite_mode "$HOST_CODE"); host_scramble=${host_scramble:-0}
src_scramble=$(sprite_mode "$code"); src_scramble=${src_scramble:-0}

row=$(cart_row "$code") || row=
if [ -z "$row" ]; then
    echo "modport: $code is not a cartridge this client knows ($ROM)" >&2
    exit 1
fi
lang=$(awk -v c="$(printf %s "$code" | cut -c4)" \
    '$1 == "lang" && $2 == c { print $3; found = 1 } END { exit !found }' \
    "$REGISTRY") || lang=unknown

row_code=$(echo "$row" | cut -d'|' -f1)
slot_name=$(echo "$row" | cut -d'|' -f3)
status=$(echo "$row" | cut -d'|' -f4)
kinds=$(echo "$row" | cut -d'|' -f5)
note=$(echo "$row" | cut -d'|' -f6-)
[ "$kinds" = "-" ] && kinds=


if [ "$row_code" != "$code" ]; then
    echo "modport: that build of $slot_name is not one this client has read" \
         "($code). $ROM is a cartridge we know the game of and not one we have" \
         "measured; mmo/CARTRIDGES is where a measured one is written down" >&2
    exit 1
fi
if [ "$status" != read ]; then
    # The status is the category and the note is the measurement behind it. A
    # player told only the category has been told a verdict; the note is what
    # makes it a reading of their own cartridge.
    echo "modport: $slot_name ($code) does not fill a package, mmo/CARTRIDGES" \
         "records it as '$status': $note" >&2
    exit 1
fi

# Which two members hold an ITEM'S picture. An icon is a character sheet and a
# palette, and the two games number them differently, so a recipe line names
# items and mmo/ITEM_ICONS turns those names into four member numbers. The
# table is generated from the engine's own item id map and HeartGold's own item
# table, and committed, a fill needs neither tree.
ICONS=${OPENMMO_ITEM_ICONS:-$(dirname "$0")/../ITEM_ICONS}

# The item-icon archive on each side. HeartGold's NitroFS has no names, so its
# archives are addressed the way its own decomp maps them (filesystem.mk:
# files/itemtool/itemdata/item_icon.narc is files/a/0/1/8).
SRC_ICON_NARC=a/0/1/8
DST_ICON_NARC=itemtool/itemdata/item_icon.narc

# The trainer-front archive on each side, and where a class's run of five
# starts. mmo/TRAINER_GFX is generated from the engine's own naix and
# HeartGold's own class constants.
SRC_TRGRA_NARC=a/0/5/8
DST_TRGRA_NARC=poketool/trgra/trfgra.narc
GFX=${OPENMMO_TRAINER_GFX:-$(dirname "$0")/../TRAINER_GFX}

# The first of one class's five members on one side, or nothing.
gfx_row() {
    awk -v side="$1" -v cls="$2" '
        /^[[:space:]]*(#|$)/ { next }
        $1 == side && $2 == cls { print $3; found = 1; exit }
        END { exit !found }
    ' "$GFX"
}

# <ncgr> <nclr> <items sharing that palette> for one side's item, or nothing.
icon_row() {
    awk -v side="$1" -v item="$2" '
        /^[[:space:]]*(#|$)/ { next }
        $1 == side && $2 == item { print $3, $4, $5; found = 1; exit }
        END { exit !found }
    ' "$ICONS"
}

# Read the whole recipe before writing anything, so a file with a bad line in
# the middle leaves no half-filled package behind.
lines=0
while read -r want kind src dst rest; do
    case "$want" in ''|\#*) continue ;; esac
    if [ -n "${rest:-}" ] || [ -z "${dst:-}" ]; then
        echo "modport: $RECIPE: cannot read '$want $kind $src $dst ${rest:-}'" >&2
        exit 1
    fi
    # One package per cartridge. A recipe naming two codes has no single answer
    # to "which cartridge filled this", which is what 20.7's stamp and a
    # package's own requirements both need; and one image cannot answer both
    # halves anyway, so the fill would half-succeed. Refuse the recipe, not the
    # image, and say what the shape is.
    if [ -n "${first_want:-}" ] && [ "$want" != "$first_want" ]; then
        echo "modport: $RECIPE names $first_want and $want. A package is" \
             "filled by one cartridge: give the second its own package and" \
             "order the two in mod.toml, so what filled each is answerable" >&2
        exit 1
    fi
    first_want=$want
    if [ "$want" != "$code" ]; then
        echo "modport: $RECIPE asks for $want and this image is $code ($ROM)" >&2
        exit 1
    fi
    # A kind the porter can carry still has to be one this slot serves: the
    # registry row is what says so, and a line asking a cartridge for something
    # it was never measured to hold is refused naming both.
    case "$kind" in
    pokemon|item_icon|trainer)
        case ",$kinds," in
        *",$kind,"*) ;;
        *) echo "modport: $RECIPE asks $slot_name for '$kind' and" \
                "mmo/CARTRIDGES says it serves: ${kinds:--}" >&2
           exit 1 ;;
        esac
        # Both endpoints have to be items each side actually has. The table is
        # committed, so this is answerable before a byte of the image is read,
        # a line naming an item HeartGold never had should not cost a fill.
        if [ "$kind" = trainer ]; then
            gfx_row hg "$src" >/dev/null || {
                echo "modport: $RECIPE: $slot_name has no trainer class" \
                     "called '$src'. mmo/TRAINER_GFX lists both games'" \
                     "classes, tagged hg and pl" >&2
                exit 1
            }
            gfx_row pl "$dst" >/dev/null || {
                echo "modport: $RECIPE: this game has no trainer class" \
                     "called '$dst'. mmo/TRAINER_GFX lists both games'" \
                     "classes, tagged hg and pl" >&2
                exit 1
            }
        fi
        if [ "$kind" = item_icon ]; then
            # A name with no row is refused, and the refusal names both games:
            # "Heart Gold has no red_apricrn" is a typo somebody can fix, and
            # if the name is really the other game's the answer says so. Never
            # a fallback to entry 0, the official client's own lookup returns null there
            # and draws nothing, which reads exactly like a gap.
            icon_row hg "$src" >/dev/null || {
                other=
                if icon_row pl "$src" >/dev/null 2>&1; then
                    other=", it is one of this game's own item names, and the source column takes the cartridge's"
                fi
                echo "modport: $RECIPE: $slot_name has no item called" \
                     "'$src'$other. mmo/ITEM_ICONS lists both games' names," \
                     "tagged hg and pl" >&2
                exit 1
            }
            icon_row pl "$dst" >/dev/null || {
                other=
                if icon_row hg "$dst" >/dev/null 2>&1; then
                    other=", it is one of $slot_name's item names, and the destination column takes this game's"
                fi
                echo "modport: $RECIPE: this game has no item called" \
                     "'$dst'$other. mmo/ITEM_ICONS lists both games' names," \
                     "tagged hg and pl" >&2
                exit 1
            }
        fi ;;
    *) echo "modport: $RECIPE: '$kind' is not a kind this tool ports" >&2; exit 1 ;;
    esac
    lines=$((lines + 1))
done < "$RECIPE"

if [ "$lines" -eq 0 ]; then
    echo "modport: $RECIPE names nothing to port" >&2
    exit 1
fi

# Whether this image has sprites this engine reads, out of the same line that
# named the game. The porter's own catalog answers it, so there is no second
# table of game codes here to drift out of step with that one.
case "$ident" in
*" pokemon=refused ("*)
    why=${ident#* pokemon=refused (}
    echo "modport: $code has no sprite archive this client can take a species" \
         "out of, the porter reads it as: ${why%)}. That is the archive's" \
         "shape and not a limit to widen; mmo/tests/import_test.sh measures it" >&2
    exit 1 ;;
*" pokemon="*) ;;
*)
    echo "modport: $code is not an image the porter has a sprite catalog for" \
         "($ident)" >&2
    exit 1 ;;
esac

# Every line into a staging directory, so nothing is published until all of
# them are through. The porter needs the mod.toml beside them or it writes a
# generated one of its own.
STAGE=$(mktemp -d) || exit 1
trap 'rm -rf "$STAGE"' EXIT INT HUP TERM
cp "$PKG/mod.toml" "$STAGE/mod.toml"
cp "$RECIPE" "$STAGE/port.recipe"

while read -r want kind src dst rest; do
    case "$want" in ''|\#*) continue ;; esac
    if [ "$kind" = trainer ]; then
        # A trainer class is five consecutive members on both sides, sheet,
        # palette, cell bank, animation bank, scan sheet, and both games
        # index them at `class * 5`, so one number each is the whole answer.
        # Same placement problem as an icon and the same answer: the porter
        # writes at the index it read, and the move onto the destination's run
        # is ours.
        src_at=$(gfx_row hg "$src")
        dst_at=$(gfx_row pl "$dst")
        logmark=0
        [ -f "$STAGE/port.log" ] && logmark=$(wc -l < "$STAGE/port.log")
        i=0
        while [ "$i" -lt 5 ]; do
            sm=$((src_at + i))
            dm=$((dst_at + i))
            "$PYTHON" "$ENGINE/pc/modport.py" --rom "$ROM" --out "$STAGE" \
                --narc "$SRC_TRGRA_NARC" --member "$sm" \
                --dest "$DST_TRGRA_NARC" \
                > /dev/null 2> "$STAGE/port.err" || {
                    cat "$STAGE/port.err" >&2
                    echo "modport: $slot_name has no member $sm, '$src' is" \
                         "class $((src_at / 5)) there and that cartridge's" \
                         "trainer archive stops before it, so nothing was" \
                         "written" >&2
                    exit 1
                }
            if [ "$sm" != "$dm" ]; then
                mv "$STAGE/narc/$DST_TRGRA_NARC/$sm" \
                   "$STAGE/narc/$DST_TRGRA_NARC/$dm"
            fi
            i=$((i + 1))
        done
        head -n "$logmark" "$STAGE/port.log" > "$STAGE/port.log.keep"
        mv "$STAGE/port.log.keep" "$STAGE/port.log"
        echo "trainer $code:$SRC_TRGRA_NARC/$src_at..$((src_at + 4)) ($src) ->" \
             "narc/$DST_TRGRA_NARC/$dst_at..$((dst_at + 4)) ($dst)" \
             >> "$STAGE/port.log"
        echo "port: heartgold $src ($src_at..$((src_at + 4))) ->" \
             "$DST_TRGRA_NARC members $dst_at..$((dst_at + 4)) as $dst"
        continue
    fi
    if [ "$kind" = item_icon ]; then
        # Two members, and the porter writes a raw member at ITS OWN index,
        # which is the source's, not the destination's. So the placement is
        # ours: extract, then move each member onto the slot the destination
        # item draws from. The package layout (narc/<path>/<index>) is what
        # pc_modfs walks, so a move inside it is addressing, not surgery.
        set -- $(icon_row hg "$src")
        src_ncgr=$1 src_nclr=$2
        set -- $(icon_row pl "$dst")
        dst_ncgr=$1 dst_nclr=$2 dst_users=$3
        if [ "${dst_users:-1}" -gt 1 ]; then
            echo "modport: $RECIPE: '$dst' draws from a palette $dst_users" \
                 "items share, so this line would repaint the others too;" \
                 "mmo/ITEM_ICONS names the count" >&2
            exit 1
        fi
        # The porter logs a raw member at the index it read, which is the
        # source's. After the move that line names a file that is not there,
        # so the two are replaced by one that says both ends.
        logmark=0
        [ -f "$STAGE/port.log" ] && logmark=$(wc -l < "$STAGE/port.log")
        for pair in "$src_ncgr $dst_ncgr" "$src_nclr $dst_nclr"; do
            set -- $pair
            "$PYTHON" "$ENGINE/pc/modport.py" --rom "$ROM" --out "$STAGE" \
                --narc "$SRC_ICON_NARC" --member "$1" --dest "$DST_ICON_NARC" \
                > /dev/null 2> "$STAGE/port.err" || {
                    cat "$STAGE/port.err" >&2
                    echo "modport: $src member $1 was refused, so nothing" \
                         "was written" >&2
                    exit 1
                }
            if [ "$1" != "$2" ]; then
                mv "$STAGE/narc/$DST_ICON_NARC/$1" \
                   "$STAGE/narc/$DST_ICON_NARC/$2"
            fi
        done
        head -n "$logmark" "$STAGE/port.log" > "$STAGE/port.log.keep"
        mv "$STAGE/port.log.keep" "$STAGE/port.log"
        echo "item_icon $code:$SRC_ICON_NARC/$src_ncgr,$src_nclr ($src) ->" \
             "narc/$DST_ICON_NARC/$dst_ncgr,$dst_nclr ($dst)" \
             >> "$STAGE/port.log"
        echo "port: heartgold $src ($src_ncgr,$src_nclr) -> $DST_ICON_NARC" \
             "members $dst_ncgr,$dst_nclr as $dst"
        continue
    fi
    "$PYTHON" "$ENGINE/pc/modport.py" --rom "$ROM" --out "$STAGE" \
        --pokemon "$src" --as "$dst" 2> "$STAGE/port.err" || {
            cat "$STAGE/port.err" >&2
            echo "modport: $src was refused, so nothing was written" >&2
            exit 1
        }
    # The porter says nothing on stderr when the art it just copied decodes
    # through this engine's sprite path, and warns when it does not (Diamond).
    # A package the loader will not draw is worse than no package: it loads,
    # and the species is a blank. So a warning is a refusal here.
    if [ -s "$STAGE/port.err" ]; then
        # The porter warns when the art it just copied will not decode through
        # this engine's sprite path. For one cartridge we now know exactly why,
        # it scrambles the other way, and can fix it, so that warning is
        # handled rather than fatal. Every other warning is still a refusal: a
        # package that loads and draws a blank is worse than no package.
        if [ "$src_scramble" != 0 ] && [ "$src_scramble" != "$host_scramble" ] \
                && grep -q "does not decode" "$STAGE/port.err"; then
            :
        else
            cat "$STAGE/port.err" >&2
            echo "modport: the porter ported '$src' and then said it will" \
                 "not draw, so nothing was written" >&2
            exit 1
        fi
    fi

    # Re-encode the four character sheets into this game's direction. The
    # palettes are not scrambled and are left alone. The destination members
    # come out of the line the porter just logged, so nothing here recomputes
    # the species arithmetic the porter already did.
    if [ "$src_scramble" != 0 ] && [ "$src_scramble" != "$host_scramble" ]; then
        span=$(sed -n 's|^pokemon .* \([a-z_/.]*\)/\([0-9]*\)\.\.\([0-9]*\)$|\1 \2 \3|p' \
               "$STAGE/port.log" | tail -1)
        [ -n "$span" ] || {
            echo "modport: could not read where '$src' landed, so its sheets" \
                 "were not re-encoded and nothing was written" >&2
            exit 1
        }
        set -- $span
        sheet_narc=$1; sheet_lo=$2
        n=0
        while [ "$n" -lt 4 ]; do
            f="$STAGE/narc/$sheet_narc/$((sheet_lo + n))"
            "$PYTHON" "$(dirname "$0")/rescramble.py" "$f" \
                "$src_scramble" "$host_scramble" || {
                    echo "modport: '$src' sheet $((sheet_lo + n)) would not" \
                         "re-encode, so nothing was written" >&2
                    exit 1
                }
            n=$((n + 1))
        done
    fi
done < "$RECIPE"
rm -f "$STAGE/port.err"

# What filled this package.

title=$(printf '%s\n' "$ident" | sed -n 's/.*"\([^"]*\)".*/\1/p')
stamp_line="fill $code $slot_name ($lang) ${title:-no title}"

# A second cartridge does not quietly win. Filling a package that another image
# already filled would leave a directory whose bytes came from two cartridges
# and whose log says so only if somebody reads to the bottom. Refuse, and name
# the one that is already in there; OPENMMO_REFILL=1 is the way to say you mean
# it, and it says what it replaced.
if [ -f "$OUT/port.log" ]; then
    was=$(sed -n 's/^fill \([A-Z0-9][A-Z0-9][A-Z0-9][A-Z0-9]\) .*/\1/p' \
          "$OUT/port.log" | tail -1)
    if [ -n "$was" ] && [ "$was" != "$code" ]; then
        if [ "${OPENMMO_REFILL:-0}" = 1 ]; then
            echo "modport: $OUT was filled from $was and is being re-filled" \
                 "from $code; every member both name is now $code's" >&2
        else
            echo "modport: $OUT was already filled from $was and this image is" \
                 "$code. Two cartridges in one package is a directory nobody" \
                 "can account for; fill a package per cartridge, or set" \
                 "OPENMMO_REFILL=1 to replace what $was put there" >&2
            exit 1
        fi
    fi
fi

mkdir -p "$OUT"
if [ "$OUT" != "$PKG" ]; then
    cp "$STAGE/mod.toml" "$OUT/mod.toml"
    cp "$STAGE/port.recipe" "$OUT/port.recipe"
fi
if [ -d "$STAGE/narc" ]; then cp -R "$STAGE/narc" "$OUT/"; fi
echo "$stamp_line" >> "$OUT/port.log"
if [ -f "$STAGE/port.log" ]; then cat "$STAGE/port.log" >> "$OUT/port.log"; fi

echo "modport: $lines line(s) from $code into $OUT"
