#!/usr/bin/env python3
"""Generate the client's move, ability and species display tables."""
import json
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ENGINE = os.environ.get(
    "ENGINE_DIR", os.path.join(REPO, "engine", "pokeplatinum")
)

# the official client's catalogue bases, measured from the official client's Z50 calls.
STR_MOVE_NAME = 110000
STR_MOVE_DESC = 120000
STR_ABILITY_NAME = 210000
STR_ABILITY_DESC = 220000


def die(msg):
    sys.stderr.write("gen_display_data: %s\n" % msg)
    sys.exit(1)


def arg(n, default):
    return sys.argv[n] if len(sys.argv) > n else default


def read_enum(path, prefix, stop=None):
    """generated/*.txt: one CONSTANT per line in id order."""
    order = []
    with open(path) as fh:
        for line in fh:
            name = line.strip()
            if not name or name.startswith("#"):
                continue
            if stop and name == stop:
                break
            if not name.startswith(prefix):
                die("%s: unexpected constant %r" % (path, name))
            order.append(name)
    if not order:
        die("%s: empty" % path)
    return order


def flatten_text(value):
    """A text-bank or data.json string, which is either one string or a list of them."""
    if isinstance(value, list):
        return "".join(value)
    if isinstance(value, str):
        return value
    die("text is %s, not a string" % type(value).__name__)


def read_messages(path, count, label):
    with open(path) as fh:
        messages = json.load(fh)["messages"]
    if len(messages) != count:
        die("%s holds %d %s for %d slots" % (path, len(messages), label, count))
    out = []
    for i, msg in enumerate(messages):
        if "en_US" not in msg:
            die("%s message %d has no en_US" % (path, i))
        out.append(flatten_text(msg["en_US"]))
    return out


def c_escape(s):
    out = []
    for ch in s:
        o = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif o < 32 or o == 127:
            out.append("\\x%02x" % o)
        else:
            out.append(ch)
    return "".join(out)


def load_json(path):
    with open(path) as fh:
        return json.load(fh)


def collect_engine(engine):
    types = read_enum(os.path.join(engine, "generated/pokemon_types.txt"), "TYPE_", "NUM_POKEMON_TYPES")
    type_id = {name: i for i, name in enumerate(types)}

    abilities = read_enum(os.path.join(engine, "generated/abilities.txt"), "ABILITY_")
    ability_names = read_messages(
        os.path.join(engine, "res/text/ability_names.json"),
        len(abilities),
        "ability names",
    )
    ability_descs = read_messages(
        os.path.join(engine, "res/text/ability_descriptions.json"),
        len(abilities),
        "ability descriptions",
    )

    moves = read_enum(os.path.join(engine, "generated/moves.txt"), "MOVE_", "MAX_MOVES")
    move_names = []
    move_descs = []
    move_types = []
    for i, const in enumerate(moves):
        folder = const[len("MOVE_") :].lower()
        path = os.path.join(engine, "res/moves", folder, "data.json")
        if not os.path.isfile(path):
            die("%s: no data.json for %s" % (path, const))
        data = load_json(path)
        if "name" not in data or "description" not in data or "type" not in data:
            die("%s: missing name, description or type" % path)
        typ = data["type"]
        if typ not in type_id:
            die("%s: unknown type %s" % (path, typ))
        move_names.append(data["name"])
        move_descs.append(flatten_text(data["description"]))
        move_types.append(type_id[typ])

    species = read_enum(os.path.join(engine, "generated/species.txt"), "SPECIES_")
    # Egg and Bad Egg sit past the last drawable species and are not a typing.
    skip = {"SPECIES_NONE", "SPECIES_EGG", "SPECIES_BAD_EGG"}
    species_t1 = []
    species_t2 = []
    last_real = 0
    for i, const in enumerate(species):
        if const in skip:
            species_t1.append(-1)
            species_t2.append(-1)
            continue
        folder = const[len("SPECIES_") :].lower()
        path = os.path.join(engine, "res/pokemon", folder, "data.json")
        if not os.path.isfile(path):
            die("%s: no data.json for %s" % (path, const))
        data = load_json(path)
        pair = data.get("types")
        if not isinstance(pair, list) or len(pair) != 2:
            die("%s: types is not a pair" % path)
        if pair[0] not in type_id or pair[1] not in type_id:
            die("%s: unknown type in %s" % (path, pair))
        species_t1.append(type_id[pair[0]])
        species_t2.append(type_id[pair[1]])
        last_real = i

    return {
        "abilities": abilities,
        "ability_names": ability_names,
        "ability_descs": ability_descs,
        "moves": moves,
        "move_names": move_names,
        "move_descs": move_descs,
        "move_types": move_types,
        "species": species,
        "species_t1": species_t1,
        "species_t2": species_t2,
        "species_max": last_real,
        "types": types,
    }


def emit_data(data, out_path):
    n_ab = len(data["abilities"])
    n_mv = len(data["moves"])
    n_sp = data["species_max"] + 1
    lines = []
    w = lines.append
    w("/* display_data.gen.h, GENERATED by tools/gen_display_data.py; DO NOT EDIT.")
    w(" *")
    w(" * The engine's ability, move and species display tables. Ability names")
    w(" * and descriptions come from res/text/ability_{names,descriptions}.json,")
    w(" * in generated/abilities.txt order. Move names, descriptions and types")
    w(" * come from res/moves/<name>/data.json, in generated/moves.txt order")
    w(" * stopping before MAX_MOVES. Species typings come from")
    w(" * res/pokemon/<name>/data.json, in generated/species.txt order, skipping")
    w(" * NONE / EGG / BAD_EGG. A screen that wants the modernized spelling")
    w(" * (Hail as Snowscape, Sturdy's short description) reads the overlay")
    w(" * first; this file is the Gen-4 fill underneath.")
    w(" */")
    w("")
    w("#define MMO_DISPLAY_ABILITY_COUNT %d" % n_ab)
    w("#define MMO_DISPLAY_MOVE_COUNT    %d" % n_mv)
    w("#define MMO_DISPLAY_SPECIES_COUNT %d" % n_sp)
    w("")
    w("static const char *const MMO_DISPLAY_ABILITY_NAME[MMO_DISPLAY_ABILITY_COUNT] = {")
    for i, name in enumerate(data["ability_names"]):
        w('    "%s", /* %d %s */' % (c_escape(name), i, data["abilities"][i]))
    w("};")
    w("")
    w("static const char *const MMO_DISPLAY_ABILITY_DESC[MMO_DISPLAY_ABILITY_COUNT] = {")
    for i, desc in enumerate(data["ability_descs"]):
        w('    "%s", /* %d */' % (c_escape(desc), i))
    w("};")
    w("")
    w("static const char *const MMO_DISPLAY_MOVE_NAME[MMO_DISPLAY_MOVE_COUNT] = {")
    for i, name in enumerate(data["move_names"]):
        w('    "%s", /* %d %s */' % (c_escape(name), i, data["moves"][i]))
    w("};")
    w("")
    w("static const char *const MMO_DISPLAY_MOVE_DESC[MMO_DISPLAY_MOVE_COUNT] = {")
    for i, desc in enumerate(data["move_descs"]):
        w('    "%s", /* %d */' % (c_escape(desc), i))
    w("};")
    w("")
    w("static const s8 MMO_DISPLAY_MOVE_TYPE[MMO_DISPLAY_MOVE_COUNT] = {")
    row = []
    for i, typ in enumerate(data["move_types"]):
        row.append("%2d" % typ)
        if len(row) == 16 or i == n_mv - 1:
            w("    %s," % ", ".join(row))
            row = []
    w("};")
    w("")
    w("static const s8 MMO_DISPLAY_SPECIES_TYPE1[MMO_DISPLAY_SPECIES_COUNT] = {")
    row = []
    for i in range(n_sp):
        row.append("%2d" % data["species_t1"][i])
        if len(row) == 16 or i == n_sp - 1:
            w("    %s," % ", ".join(row))
            row = []
    w("};")
    w("")
    w("static const s8 MMO_DISPLAY_SPECIES_TYPE2[MMO_DISPLAY_SPECIES_COUNT] = {")
    row = []
    for i in range(n_sp):
        row.append("%2d" % data["species_t2"][i])
        if len(row) == 16 or i == n_sp - 1:
            w("    %s," % ", ".join(row))
            row = []
    w("};")
    w("")
    text = "\n".join(lines) + "\n"
    with open(out_path, "w") as fh:
        fh.write(text)


def unescape_xml(s):
    s = s.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", '"')
    s = s.replace("&apos;", "'").replace("&amp;", "&")
    # The catalogue writes a newline as the two characters '\' 'n'.
    s = s.replace("\\n", "\n")
    return s


def read_overlay(path):
    text = open(path, encoding="utf-8").read()
    rows = []
    seen = set()
    for m in re.finditer(r'<string id="(\d+)"[^>]*>(.*?)</string>', text, re.S):
        sid = int(m.group(1))
        in_move = STR_MOVE_NAME <= sid < STR_MOVE_NAME + 20000
        in_ability = STR_ABILITY_NAME <= sid < STR_ABILITY_NAME + 20000
        if not (in_move or in_ability):
            continue
        if sid in seen:
            die("%s: string id %d listed twice" % (path, sid))
        seen.add(sid)
        rows.append((sid, unescape_xml(m.group(2))))
    if not rows:
        die("%s: no strings at the four display bases" % path)
    rows.sort()
    return rows


def emit_overlay(rows, out_path):
    lines = []
    w = lines.append
    w("/* display_overlay.gen.h, GENERATED by tools/gen_display_data.py; DO NOT EDIT.")
    w(" *")
    # Split so this file is not itself an origin marker.
    w(" * origin:" + " official | MMO_DISPLAY_OVERLAY, the official client's strings_en.xml at the")
    w(" * four catalogue bases the official client fills from ROM banks and then overlays:")
    w(" * move names 110000, move descriptions 120000, ability names 210000,")
    w(" * ability descriptions 220000. An id present here wins over the engine")
    w(" * table. Hail (258) is Snowscape. Custom abilities start at 500.")
    w(" */")
    w("")
    w("#define MMO_DISPLAY_OVERLAY_COUNT %d" % len(rows))
    w("")
    w("static const struct {")
    w("    int id;")
    w("    const char *text;")
    w("} MMO_DISPLAY_OVERLAY[MMO_DISPLAY_OVERLAY_COUNT] = {")
    for sid, text in rows:
        w("    { %d, \"%s\" }," % (sid, c_escape(text)))
    w("};")
    w("")
    with open(out_path, "w") as fh:
        fh.write("\n".join(lines) + "\n")


def main():
    engine = arg(1, ENGINE)
    out_data = arg(2, os.path.join(REPO, "mmo/src/display_data.gen.h"))
    xml = arg(3, "")
    out_overlay = arg(4, os.path.join(REPO, "mmo/src/display_overlay.gen.h"))

    data = collect_engine(engine)
    emit_data(data, out_data)
    sys.stderr.write(
        "gen_display_data: %d abilities, %d moves, species 1..%d -> %s\n"
        % (
            len(data["abilities"]),
            len(data["moves"]),
            data["species_max"],
            out_data,
        )
    )

    if xml:
        if not os.path.isfile(xml):
            die("no string catalogue at %s" % xml)
        rows = read_overlay(xml)
        emit_overlay(rows, out_overlay)
        sys.stderr.write(
            "gen_display_data: %d overlay strings -> %s\n" % (len(rows), out_overlay)
        )


if __name__ == "__main__":
    main()
