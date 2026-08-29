#!/bin/sh
# The design notes against the constructors it claims to reuse.
set -eu

ROOT=${1:?usage: widgets_test.sh <mmo-root> [engine-dir]}
ENGINE=${2:-}

DOC="$ROOT/WIDGETS.md"
DIALOG="$ROOT/mods/openmmo/src/openmmo_dialog.c"
UNION="$ROOT/mods/openmmo/src/openmmo_union.c"
PLAYER="$ROOT/mods/openmmo/src/openmmo_player.c"
KIT="$ROOT/src/widget.c"
DRAW="$ROOT/mods/openmmo/src/openmmo_widget.c"
LIMITS="$ROOT/ENGINE_LIMITS.md"

for f in "$DOC" "$DIALOG" "$UNION" "$PLAYER" "$KIT" "$DRAW" "$LIMITS"; do
    if [ ! -f "$f" ]; then
        echo "widgets: SKIP (no $f)"
        exit 0
    fi
done

fail=0
echo "the widget page and the code it describes agree:"

ok()  { echo "  ok   $1"; }
bad() { echo "  FAIL $1"; echo "       $2"; fail=1; }

page=$(tr '\n' ' ' < "$DOC" | tr -s ' ')

says() {
    what=$1
    shift
    for want in "$@"; do
        case "$page" in
        *"$want"*) ;;
        *)
            bad "$what" "WIDGETS.md does not say: $want"
            return
            ;;
        esac
    done
    ok "$what"
}

has() {
    if grep -Fq "$2" "$3"; then
        ok "$1"
    else
        bad "$1" "$3 does not contain: $2"
    fi
}

# The same claim, satisfied by either spelling. A literal is a measurement of
# the engine's number; the engine's own constant is that number and cannot
# drift from it, so a file that names the constant passes more strongly than
# one that repeats the digits.
either() {
    if grep -Fq "$2" "$4" || grep -Fq "$3" "$4"; then
        ok "$1"
    else
        bad "$1" "$4 contains neither: $2 nor: $3"
    fi
}

says "the page names the four seated constructors" \
    "FieldMessage_AddWindow" "Menu_MakeYesNoChoice" "Menu_New" "ListMenu_New"
says "the page names the unused signpost and touch yes/no" \
    "FieldMessage_AddSignpostWindow" "YesNoTouchMenu_New"
says "the page lists Gx0 as new" "Gx0" "instance-window"
says "the page lists the already-new chat and entry" \
    "src/chatwin.c" "src/entry.c"
says "the page publishes the field message layout" "(2, 19)" "27×4"
says "the page publishes the field yes/no template" "(25, 13)" "6×4" "0x21F"

has "the fused dialog still calls FieldMessage_AddWindow" \
    "FieldMessage_AddWindow" "$DIALOG"
has "the fused dialog still calls FieldMessage_DrawWindow" \
    "FieldMessage_DrawWindow" "$DIALOG"
has "the fused dialog still calls FieldMessage_Print" \
    "FieldMessage_Print" "$DIALOG"
has "the fused dialog still calls Menu_MakeYesNoChoice" \
    "Menu_MakeYesNoChoice" "$DIALOG"
has "the fused dialog still calls Menu_NewAndCopyToVRAM" \
    "Menu_NewAndCopyToVRAM" "$DIALOG"
has "the fused yes/no still uses the engine template numbers" \
    ".tilemapLeft = 25," "$DIALOG"
either "the fused yes/no still uses base tile 0x21F" \
    ".baseTile = 0x21F," ".baseTile = BASE_TILE_YES_NO_MENU," "$DIALOG"
has "the fused union list still calls ListMenu_New" \
    "ListMenu_New" "$UNION"
has "the fused union list still calls FieldMessage_AddWindow" \
    "FieldMessage_AddWindow" "$UNION"
has "the fused union list is still at (1, 2)" \
    "1, 2, 20," "$UNION"
has "the fused player menu still calls Menu_NewAndCopyToVRAM" \
    "Menu_NewAndCopyToVRAM" "$PLAYER"
has "the fused player menu still calls FieldMessage_AddWindow" \
    "FieldMessage_AddWindow" "$PLAYER"
has "the fused player menu still names Challenge" \
    "Challenge" "$PLAYER"
has "the fused player menu still names Invite to Link" \
    "Invite to Link" "$PLAYER"
if grep -Fq '"Spectate"' "$PLAYER"; then
    bad "the fused player menu does not offer Spectate" \
        "$PLAYER still contains a Spectate row"
else
    ok "the fused player menu does not offer Spectate"
fi
has "the limits page now points at the catalogue" \
    "WIDGETS.md" "$LIMITS"

says "the page names the kit's two halves" \
    "mmo/include/widget.h" "mods/openmmo/src/openmmo_widget.c"
says "the page still says a pick is not sent" "printed, not sent"
has "the renderer still draws a grid with Menu_NewAndCopyToVRAM" \
    "Menu_NewAndCopyToVRAM" "$DRAW"
has "the renderer still draws a list with ListMenu_New" \
    "ListMenu_New" "$DRAW"
has "the renderer still draws text with FieldMessage_AddWindow" \
    "FieldMessage_AddWindow" "$DRAW"
has "the renderer still scrolls with the engine's maxDisplay" \
    "tmpl.maxDisplay" "$DRAW"
# Menu_DestroyForExit frees the window pointer and the choice list. The
# renderer's window is a field of the screen, so calling it there is a
# heap corruption that only shows up on the press that closes the menu.
if grep -Fq "Menu_DestroyForExit(ws->menu" "$DRAW"; then
    bad "the renderer still tears a menu down with Menu_Free" \
        "$DRAW calls Menu_DestroyForExit on a window it did not allocate"
else
    has "the renderer still tears a menu down with Menu_Free" \
        "Menu_Free(ws->menu" "$DRAW"
fi
has "the kit still compiles every shape the store holds" \
    "mmo_screen_from_menu_prompt" "$KIT"
has "the kit still refuses to invent a reply opcode" \
    "int reply_op;" "$ROOT/include/widget.h"

if [ -n "$ENGINE" ]; then
    FM="$ENGINE/include/field_message.h"
    MENU="$ENGINE/include/menu.h"
    LIST="$ENGINE/include/list_menu.h"
    TOUCH="$ENGINE/include/yes_no_touch_menu.h"
    RW="$ENGINE/include/render_window.h"
    FM_C="$ENGINE/src/field_message.c"
    SCR="$ENGINE/src/scrcmd.c"
    MSG="$ENGINE/src/overlay005/script_message.c"
    if [ ! -f "$FM" ] || [ ! -f "$MENU" ] || [ ! -f "$LIST" ]; then
        echo "  skip engine half (no field_message.h / menu.h / list_menu.h)"
    else
        has "the engine still exports FieldMessage_AddWindow" \
            "FieldMessage_AddWindow" "$FM"
        has "the engine still exports Menu_MakeYesNoChoice" \
            "Menu_MakeYesNoChoice" "$MENU"
        has "the engine still exports Menu_New" \
            "Menu_New" "$MENU"
        has "the engine still exports ListMenu_New" \
            "ListMenu_New" "$LIST"
        if [ -f "$TOUCH" ]; then
            has "the engine still exports YesNoTouchMenu_New" \
                "YesNoTouchMenu_New" "$TOUCH"
        fi
        if [ -f "$RW" ]; then
            has "the engine still sizes the message window 27x4" \
                "MESSAGE_WINDOW_TILE_W     27" "$RW"
            has "the engine still sizes the yes/no 6x4" \
                "YES_NO_MENU_TILE_W     6" "$RW"
        fi
        if [ -f "$FM_C" ]; then
            has "FieldMessage_AddWindow is still (2, 19) 27x4 on MAIN_3" \
                "Window_Add(bgConfig, window, BG_LAYER_MAIN_3, 2, 19, 27, 4," "$FM_C"
        fi
        if [ -f "$SCR" ]; then
            has "the field-script yes/no still calls Menu_MakeYesNoChoice" \
                "Menu_MakeYesNoChoice" "$SCR"
            has "the field-script yes/no template is still (25, 13)" \
                ".tilemapLeft = 25," "$SCR"
            either "the field-script yes/no template is still base 0x21F" \
                ".baseTile = 0x21F," ".baseTile = BASE_TILE_YES_NO_MENU," "$SCR"
        fi
        if [ -f "$MSG" ]; then
            has "the field-script message box still calls FieldMessage_AddWindow" \
                "FieldMessage_AddWindow" "$MSG"
        fi
    fi
fi

if [ "$fail" -ne 0 ]; then
    echo "widgets: checks failed"
    exit 1
fi
echo "widgets: all checks passed"
