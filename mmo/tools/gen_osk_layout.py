#!/usr/bin/env python3
"""Generate the on-screen keyboard layout from the engine naming screen."""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ENGINE = os.environ.get(
    "ENGINE_DIR", os.path.join(REPO, "engine", "pokeplatinum")
)

NAMING = "src/applications/naming_screen.c"
CHARCODE_H = "include/constants/charcode.h"
CHARMAP = "tools/msgenc/charmap.txt"

NMS_ROWS = 6
NMS_COLS = 13
NMS_PAGES = 5

# Engine NamingScreenControlChars / button ids, from naming_screen.c:60-73.
NMS_CONTROL_DAKU = 0xD001
NMS_CONTROL_HANDAKU = 0xD002
NMS_CONTROL_SPACE = 0xD003
NMS_CONTROL_SKIP = 0xD004
NMS_BUTTON_START = 0xE001
NMS_BUTTON_PAGE_UPPER = 0xE002
NMS_BUTTON_PAGE_LOWER = 0xE003
NMS_BUTTON_PAGE_OTHERS = 0xE004
NMS_BUTTON_PAGE_JP = 0xE005
NMS_BUTTON_PAGE_NUMPAD = 0xE006
NMS_BUTTON_BACK = 0xE007
NMS_BUTTON_OK = 0xE008

# Emitted sentinels. 0 is skip. 0xE002.. match the engine page-button ids
# so a page switch is `cell - OSK_CELL_UPPER`.
OSK_CELL_SKIP = 0
OSK_CELL_UPPER = NMS_BUTTON_PAGE_UPPER
OSK_CELL_LOWER = NMS_BUTTON_PAGE_LOWER
OSK_CELL_OTHERS = NMS_BUTTON_PAGE_OTHERS
OSK_CELL_JP = NMS_BUTTON_PAGE_JP
OSK_CELL_NUMPAD = NMS_BUTTON_PAGE_NUMPAD
OSK_CELL_BACK = NMS_BUTTON_BACK
OSK_CELL_OK = NMS_BUTTON_OK

CONTROL_TO_CELL = {
    "NMS_CONTROL_DAKU": OSK_CELL_SKIP,
    "NMS_CONTROL_HANDAKU": OSK_CELL_SKIP,
    "NMS_CONTROL_SPACE": 0x0020,
    "NMS_CONTROL_SKIP": OSK_CELL_SKIP,
    "NMS_BUTTON_START": OSK_CELL_SKIP,
    "NMS_BUTTON_PAGE_UPPER": OSK_CELL_UPPER,
    "NMS_BUTTON_PAGE_LOWER": OSK_CELL_LOWER,
    "NMS_BUTTON_PAGE_OTHERS": OSK_CELL_OTHERS,
    "NMS_BUTTON_PAGE_JP_UNUSED": OSK_CELL_JP,
    "NMS_BUTTON_PAGE_JP_UNUSED_2": OSK_CELL_NUMPAD,
    "NMS_BUTTON_BACK": OSK_CELL_BACK,
    "NMS_BUTTON_OK": OSK_CELL_OK,
}


def die(msg):
    sys.stderr.write("gen_osk_layout: %s\n" % msg)
    sys.exit(1)


def read_charcode_enum(path):
    text = open(path, encoding="utf-8").read()
    m = re.search(r"enum CharCode\s*\{(.*?)\};", text, re.S)
    if not m:
        die("%s: no enum CharCode" % path)
    value = 0
    out = {}
    for raw in m.group(1).split(","):
        line = raw.split("//", 1)[0].strip()
        if not line:
            continue
        if "=" in line:
            name, rhs = [p.strip() for p in line.split("=", 1)]
            if rhs.startswith("0x") or rhs.startswith("0X"):
                value = int(rhs, 16)
            else:
                value = int(rhs, 10)
        else:
            name = line
        if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
            die("%s: bad enumerator %r" % (path, name))
        out[name] = value
        value += 1
    if "CHAR_EOS" not in out:
        die("%s: CHAR_EOS missing" % path)
    return out


def read_charmap(path):
    rows = {}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.split("//", 1)[0].rstrip("\n")
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.strip()
            if not re.fullmatch(r"[0-9A-Fa-f]{4}", key):
                continue
            if len(value) == 1:
                rows[int(key, 16)] = ord(value)
            elif re.fullmatch(r"\\x[0-9A-Fa-f]{4}", value):
                rows[int(key, 16)] = int(value[2:], 16)
    if not rows:
        die("%s: empty character map" % path)
    return rows


def parse_ident_array(text, name):
    m = re.search(
        r"static const (?:charcode_t|u16) %s\[\] = \{([^}]+)\}" % re.escape(name),
        text,
    )
    if not m:
        die("naming_screen.c: no array %s" % name)
    idents = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", m.group(1))
    if not idents:
        die("naming_screen.c: %s is empty" % name)
    return idents


def parse_pointer_table(text, name, width):
    m = re.search(
        r"static const charcode_t \*%s\[\](?:\[%d\])? = \{(.+?)\n\};"
        % (re.escape(name), width),
        text,
        re.S,
    )
    if not m:
        die("naming_screen.c: no table %s" % name)
    body = m.group(1)
    rows = []
    if width > 1:
        for block in re.findall(r"\{([^{}]+)\}", body):
            idents = re.findall(r"[A-Za-z_][A-Za-z0-9_]*", block)
            if len(idents) != width:
                die("naming_screen.c: %s row has %d names, want %d" % (name, len(idents), width))
            rows.append(idents)
    else:
        rows = [[ident] for ident in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", body)]
    if len(rows) != NMS_PAGES:
        die("naming_screen.c: %s has %d pages, want %d" % (name, len(rows), NMS_PAGES))
    return rows


def resolve_cell(ident, enums, charmap, page, row):
    if ident in CONTROL_TO_CELL:
        cell = CONTROL_TO_CELL[ident]
        # Numpad wide-space cells are empty: the overlay ignores them.
        return cell
    if ident == "CHAR_EOS":
        die("CHAR_EOS reached a cell (%s page %d row %d)" % (ident, page, row))
    if ident not in enums:
        die("unknown identifier %s" % ident)
    cc = enums[ident]
    if ident == "CHAR_WIDE_SPACE" and page == 4:
        return OSK_CELL_SKIP
    if cc not in charmap:
        die("charcode 0x%04X (%s) is not in the character map" % (cc, ident))
    return charmap[cc]


def take_row(idents, enums, charmap, page, row):
    body = idents
    if body and body[-1] == "CHAR_EOS":
        body = body[:-1]
    if len(body) < NMS_COLS:
        die("row %d of page %d has %d cells, want %d" % (row, page, len(body), NMS_COLS))
    return [resolve_cell(ident, enums, charmap, page, row) for ident in body[:NMS_COLS]]


def home_row_with_numpad(idents):
    """Put a numpad button in the English home row's SKIP pair."""
    out = list(idents)
    skips = [i for i, name in enumerate(out) if name == "NMS_CONTROL_SKIP"]
    if len(skips) < 2:
        die("sHomeRowAll does not have two SKIP cells to take")
    out[skips[0]] = "NMS_BUTTON_PAGE_JP_UNUSED_2"
    out[skips[1]] = "NMS_BUTTON_PAGE_JP_UNUSED_2"
    return out


def build(engine):
    naming = os.path.join(engine, NAMING)
    enum_path = os.path.join(engine, CHARCODE_H)
    map_path = os.path.join(engine, CHARMAP)
    for path in (naming, enum_path, map_path):
        if not os.path.isfile(path):
            die("missing %s" % path)

    text = open(naming, encoding="utf-8").read()
    if "#define NMS_NUM_ROWS 6" not in text or "#define NMS_NUM_COLS 13" not in text:
        die("naming_screen.c is no longer a 6x13 grid")

    enums = read_charcode_enum(enum_path)
    charmap = read_charmap(map_path)
    arrays = {}
    for name in re.findall(
        r"static const (?:charcode_t|u16) (s(?:CharCodes|HomeRow)\w+)\[\] =",
        text,
    ):
        arrays[name] = parse_ident_array(text, name)

    pages = parse_pointer_table(text, "sCharCodes", 5)
    homes = parse_pointer_table(text, "sHomeRowLayouts", 1)

    # One home row for every page, with the numpad button we add.
    if "sHomeRowAll" not in arrays:
        die("sHomeRowAll missing")
    home = home_row_with_numpad(arrays["sHomeRowAll"])

    grid = []
    for page, row_names in enumerate(pages):
        rows = [take_row(home, enums, charmap, page, 0)]
        for r, name in enumerate(row_names, start=1):
            if name not in arrays:
                die("page %d row %d names %s, which is not an array" % (page, r, name))
            rows.append(take_row(arrays[name], enums, charmap, page, r))
        if len(rows) != NMS_ROWS:
            die("page %d has %d rows" % (page, len(rows)))
        grid.append(rows)

    # Sanity: the English pages type ASCII, slash is on others, digits
    # are on upper row 5 and on the numpad we just linked.
    def has_unit(page, unit):
        return any(unit in row for row in grid[page])

    if not has_unit(0, ord("A")) or not has_unit(1, ord("a")):
        die("upper/lower pages lost the Latin alphabet")
    if not has_unit(2, ord("/")):
        die("others page lost CHAR_SLASH")
    if not has_unit(0, ord("0")) or not has_unit(4, ord("0")):
        die("digits are not on both the letter pages and the numpad")
    if OSK_CELL_NUMPAD not in grid[0][0]:
        die("home row did not receive a numpad button")
    # Every page, including the numpad, must be able to leave.
    if OSK_CELL_UPPER not in grid[4][0]:
        die("numpad home row cannot return to letters")
    # Home-row layouts were read so a drift there fails even though we
    # do not use the numpad-only row any more.
    if homes[4][0] != "sHomeRowNumpad":
        die("page 4 is no longer the numpad home row in the engine")
    return grid


def emit(grid):
    lines = []
    w = lines.append
    w("/* osk_layout.gen.h, GENERATED by tools/gen_osk_layout.py; DO NOT EDIT.\n")
    w(" *\n")
    w(" * The engine naming screen's 6x13 x 5-page grid, re-hosted. Cells are\n")
    w(" * Unicode code points, or the OSK_CELL_* sentinels. The home-row\n")
    w(" * numpad button is ours: the engine's English home row cannot reach\n")
    w(" * page 4. tests/osk_gen_test.sh re-derives this file.\n")
    w(" */\n")
    w("#ifndef OPENMMO_OSK_LAYOUT_GEN_H\n")
    w("#define OPENMMO_OSK_LAYOUT_GEN_H\n\n")
    w("#define OSK_LAYOUT_PAGES %d\n" % NMS_PAGES)
    w("#define OSK_LAYOUT_ROWS  %d\n" % NMS_ROWS)
    w("#define OSK_LAYOUT_COLS  %d\n\n" % NMS_COLS)
    w("#define OSK_CELL_SKIP    0x%04X\n" % OSK_CELL_SKIP)
    w("#define OSK_CELL_UPPER   0x%04X\n" % OSK_CELL_UPPER)
    w("#define OSK_CELL_LOWER   0x%04X\n" % OSK_CELL_LOWER)
    w("#define OSK_CELL_OTHERS  0x%04X\n" % OSK_CELL_OTHERS)
    w("#define OSK_CELL_JP      0x%04X\n" % OSK_CELL_JP)
    w("#define OSK_CELL_NUMPAD  0x%04X\n" % OSK_CELL_NUMPAD)
    w("#define OSK_CELL_BACK    0x%04X\n" % OSK_CELL_BACK)
    w("#define OSK_CELL_OK      0x%04X\n\n" % OSK_CELL_OK)
    w("static const unsigned short OSK_LAYOUT[OSK_LAYOUT_PAGES][OSK_LAYOUT_ROWS][OSK_LAYOUT_COLS] = {\n")
    page_names = ("upper", "lower", "others", "jp", "numpad")
    for p, rows in enumerate(grid):
        w("    { /* %s */\n" % page_names[p])
        for row in rows:
            cells = ", ".join("0x%04X" % c for c in row)
            w("        { %s },\n" % cells)
        w("    },\n")
    w("};\n\n")
    w("#endif /* OPENMMO_OSK_LAYOUT_GEN_H */\n")
    return "".join(lines)


def main():
    engine = sys.argv[1] if len(sys.argv) > 1 else ENGINE
    out = sys.argv[2] if len(sys.argv) > 2 else None
    text = emit(build(engine))
    if out:
        parent = os.path.dirname(os.path.abspath(out))
        if parent and not os.path.isdir(parent):
            os.makedirs(parent)
        with open(out, "w", encoding="utf-8") as fh:
            fh.write(text)
    else:
        sys.stdout.write(text)
    sys.stderr.write(
        "gen_osk_layout: %d pages x %d x %d -> %s\n"
        % (NMS_PAGES, NMS_ROWS, NMS_COLS, out or "stdout")
    )


if __name__ == "__main__":
    main()
