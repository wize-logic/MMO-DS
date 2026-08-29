#!/bin/sh
# Start a fight from a cold boot of the fused build.
#
#   battle_lab.sh mint  <root> <build> <engine> <save> [<lab>]
#   battle_lab.sh fight <root> <build> <engine> <save> <spec> [<at>] [<frames>]
#   battle_lab.sh use   <root> <build> <engine> <save> <spec> [<at>] [<frames>]
set -eu

if [ $# -lt 5 ]; then
    echo "usage: battle_lab.sh mint|fight|use <root> <build> <engine> <save> ..." >&2
    exit 2
fi

cmd=$1
ROOT=$2
BUILD=$3
ENGINE=$4
SAVE=$5
shift 5

FUSED="$BUILD/fused/pokeplatinum"
ROM="${PC_ROM:-$ENGINE/build/rom/pokeplatinum.us.nds}"
SETTLE="$ENGINE/pc/replays/lab-settle.txt"
REPLAY="$ENGINE/pc/replays/lab-battle.txt"

if [ ! -x "$FUSED" ]; then
    echo "battle_lab: no fused build at $FUSED" >&2
    exit 2
fi
if [ ! -f "$ROM" ]; then
    echo "battle_lab: no ROM at $ROM" >&2
    exit 2
fi

# A party that survives one turn against anything the six kinds throw, plus
# a rare candy and a potion so --use-item has something to spend. Numeric
# so this stays free of the port's recipe compiler. Same lead battle_seam
# already mints; the items are the only addition.
default_lab() {
    cat <<'EOF'
name SEAM
gender 0
trainer-id 40001
money 60000
party 445 100 0
party 350 100 0
party-move 0 0 89
party-move 1 0 57
item 50 5
item 17 5
map 3 180 777 1
EOF
}

case $cmd in
mint)
    lab=${1:-}
    if [ -z "$lab" ]; then
        lab=${SAVE}.lab
        default_lab > "$lab"
    fi
    if [ ! -f "$lab" ]; then
        echo "battle_lab: no recipe at $lab" >&2
        exit 2
    fi
    if [ ! -f "$SETTLE" ]; then
        echo "battle_lab: no settle script at $SETTLE" >&2
        exit 2
    fi
    mkdir -p "$(dirname "$SAVE")"
    exec env PC_ROM="$ROM" PC_SAVE="$SAVE" PC_LAB="$lab" \
        PC_LAB_AT="${PC_LAB_AT:-1800}" PC_PACE=0 PC_INPUT="$SETTLE" \
        "$FUSED"
    ;;
fight|use)
    spec=${1:?battle_lab: $cmd needs a spec}
    at=${2:-2600}
    frames=${3:-3000}
    if [ ! -f "$SAVE" ]; then
        echo "battle_lab: no save at $SAVE (mint one first)" >&2
        exit 2
    fi
    if [ ! -f "$REPLAY" ]; then
        echo "battle_lab: no battle script at $REPLAY" >&2
        exit 2
    fi
    if [ "$cmd" = fight ]; then
        set -- PC_LAB_BATTLE="$spec" PC_LAB_BATTLE_AT="$at"
    else
        set -- PC_LAB_USE_ITEM="$spec" PC_LAB_USE_AT="$at"
    fi
    exec env PC_ROM="$ROM" PC_SAVE="$SAVE" PC_PACE=0 PC_INPUT="$REPLAY" \
        PC_FRAMES="$frames" "$@" "$FUSED"
    ;;
*)
    echo "battle_lab: unknown command '$cmd'" >&2
    exit 2
    ;;
esac
