#!/usr/bin/env python3
"""Generate the table of item ids the server and the engine both have."""
import json
import os
import re
import sys
import unicodedata

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ENGINE = os.environ.get(
    "ENGINE_DIR", os.path.join(REPO, "engine", "pokeplatinum")
)


def arg(n, default):
    return sys.argv[n] if len(sys.argv) > n else default


ITEMS_TXT = arg(1, os.path.join(ENGINE, "generated/items.txt"))
ITEM_DATA = arg(2, os.path.join(ENGINE, "res/items/data"))
SERVER_NAMES = arg(3, os.path.join(REPO, "codegen/items/item_names.json"))
OUT = arg(4, os.path.join(REPO, "mmo/src/idmap_items.gen.h"))
GBA_H = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(ITEMS_TXT))),
    "include/constants/gba/items.h",
)

# The names ItemDataParser refuses to register, so the server can never send one.
PLACEHOLDER_NAMES = {"None", "?????", "???", "-", "?", ""}


def parse_gba_enum(path):
    """GBA_ITEM_* name -> numeric id, walking the engine's own enum."""
    out = {}
    n = None
    with open(path) as f:
        for line in f:
            m = re.search(r"(GBA_ITEM_\w+)\s*(=\s*(\d+))?", line)
            if not m:
                continue
            if m.group(3) is not None:
                n = int(m.group(3))
            else:
                n = 0 if n is None else n + 1
            out[m.group(1)] = n
    if "GBA_ITEM_POTION" not in out or out["GBA_ITEM_POTION"] != 13:
        raise SystemExit(
            "gen_idmap_items: GBA_ITEM_POTION is not 13 in %s, the enum walked wrong"
            % path
        )
    return out


def engine_items(items_txt, data_dir, gba_ids):
    """Engine item id -> {name, gba_num} for the ids that have item data."""
    out = {}
    with open(items_txt) as f:
        constants = [line.strip() for line in f if line.strip()]
    for item_id, constant in enumerate(constants):
        if item_id == 0 or not constant.startswith("ITEM_"):
            continue
        path = os.path.join(data_dir, constant[len("ITEM_") :].lower() + ".json")
        if not os.path.exists(path):
            continue  # ITEM_UNUSED_nnn and friends: an id with no item behind it
        with open(path) as f:
            data = json.load(f)
        gba_name = data.get("gbaID") or "GBA_ITEM_NONE"
        gba_num = gba_ids.get(gba_name)
        if gba_num is None:
            raise SystemExit(
                "gen_idmap_items: %s names gbaID %s, which is not in the GBA enum"
                % (path, gba_name)
            )
        out[item_id] = {
            "name": data["name"],
            "gba_num": gba_num,
        }
    return out


FILL_TABLE = os.path.join(REPO, "mmo/ITEM_FILL")


def fill_items(path):
    """Engine item id -> {name, gba_num 0} for the ids a HeartGold fill appends
    past this game's own table (tools/portitems.py writes the table). These
    have no GBA number and no item data file in this tree; the fill's row is
    the cartridge's, and the id is the one both games give the item."""
    out = {}
    if not os.path.isfile(path):
        return out
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            item_id, name = line.split(" ", 1)
            out[int(item_id)] = {"name": name.strip(), "gba_num": 0}
    return out


def server_items(names_json):
    """Server item index -> display name, for the indices it registers."""
    with open(names_json) as f:
        names = json.load(f)
    return {
        i: name
        for i, name in enumerate(names)
        if i != 0 and name.strip() not in PLACEHOLDER_NAMES
    }


def comparable(name):
    """Fold the two trees' spellings far enough to compare them."""
    return re.sub(r"[^a-z0-9]", "", unicodedata.normalize("NFKD", name).lower())


def ascii_only(name):
    """The emitted header is a C source file, so keep its comments to ASCII."""
    folded = unicodedata.normalize("NFKD", name)
    return "".join(c for c in folded if ord(c) < 128)


def gba_note(info):
    if info["gba_num"]:
        return "gba %d" % info["gba_num"]
    return "no gba"


def find_named(engine, want):
    for item_id, info in engine.items():
        if info["name"] == want:
            return item_id, info
    raise SystemExit("gen_idmap_items: no engine item named %r" % want)


def main():
    if not os.path.isfile(GBA_H):
        raise SystemExit("gen_idmap_items: no gba item enum at %s" % GBA_H)

    gba_ids = parse_gba_enum(GBA_H)
    engine = engine_items(ITEMS_TXT, ITEM_DATA, gba_ids)
    filled = fill_items(FILL_TABLE)
    for item_id, info in filled.items():
        if item_id in engine:
            raise SystemExit("gen_idmap_items: mmo/ITEM_FILL names %d, which the engine has"
                             % item_id)
        engine[item_id] = info
    server = server_items(SERVER_NAMES)
    shared = sorted(set(engine) & set(server))
    divergent = [
        i for i in shared if comparable(engine[i]["name"]) != comparable(server[i])
    ]

    gba_set = [i for i in engine if engine[i]["gba_num"]]
    gba_ne = [i for i in gba_set if engine[i]["gba_num"] != i]
    collisions = []
    for i in gba_ne:
        other = engine.get(engine[i]["gba_num"])
        if other is not None:
            collisions.append(i)

    potion_id, potion = find_named(engine, "Potion")
    dusk_id, dusk = find_named(engine, "Dusk Ball")
    if potion["gba_num"] == potion_id:
        raise SystemExit(
            "gen_idmap_items: Potion's gba id equals its engine id; the third numbering vanished"
        )
    if potion["gba_num"] != dusk_id:
        raise SystemExit(
            "gen_idmap_items: Potion gba %d is not the Dusk Ball engine id %d"
            % (potion["gba_num"], dusk_id)
        )
    if dusk["gba_num"] != 0:
        raise SystemExit("gen_idmap_items: Dusk Ball unexpectedly has a gba id")
    if potion_id not in shared or dusk_id not in shared:
        raise SystemExit("gen_idmap_items: Potion or Dusk Ball dropped out of the shared set")

    lines = [
        "/* Generated by tools/gen_idmap_items.py; Do not edit. */",
        "",
        "static const u16 MMO_ITEM_SHARED[] = {",
    ]
    for item_id in shared:
        note = gba_note(engine[item_id])
        if item_id in divergent:
            comment = "engine %s / server %s; %s" % (
                ascii_only(engine[item_id]["name"]),
                ascii_only(server[item_id]),
                note,
            )
        else:
            comment = "%s; %s" % (ascii_only(engine[item_id]["name"]), note)
        lines.append("    %4d, /* %s */" % (item_id, comment))
    lines.append("};")
    lines.append("")
    lines.append("#define MMO_ITEM_SHARED_COUNT %d" % len(shared))
    lines.append("")
    lines.append("/* Three numberings, one Potion. Wire is region-5 + engine id.")
    lines.append(" * gba 13 is a Dusk Ball in this engine. */")
    lines.append("#define MMO_ITEM_WITNESS_POTION_WIRE      %d" % (5000 + potion_id))
    lines.append("#define MMO_ITEM_WITNESS_POTION_ENGINE    %d" % potion_id)
    lines.append("#define MMO_ITEM_WITNESS_POTION_GBA       %d" % potion["gba_num"])
    lines.append("#define MMO_ITEM_WITNESS_DUSKBALL_ENGINE  %d" % dusk_id)
    lines.append("#define MMO_ITEM_WITNESS_DUSKBALL_GBA     %d" % dusk["gba_num"])
    lines.append("#define MMO_ITEM_GBA_SET_COUNT            %d" % len(gba_set))
    lines.append("#define MMO_ITEM_GBA_NE_ENGINE_COUNT      %d" % len(gba_ne))
    lines.append("")

    with open(OUT, "w") as f:
        f.write("\n".join(lines))
    sys.stderr.write(
        "wrote %s: %d shared ids (engine has %d, server has %d), %d name divergences, "
        "%d gba != engine (%d collide)\n"
        % (
            OUT,
            len(shared),
            len(engine),
            len(server),
            len(divergent),
            len(gba_ne),
            len(collisions),
        )
    )
    for item_id in divergent:
        sys.stderr.write(
            "  %4d: engine %r vs server %r\n"
            % (item_id, engine[item_id]["name"], server[item_id])
        )


if __name__ == "__main__":
    main()
