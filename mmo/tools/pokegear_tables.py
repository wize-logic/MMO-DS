#!/usr/bin/env python3
"""Regenerate mmo/mods/openmmo/src/openmmo_pokegear_tables.c from the pokeheartgold
decompilation: the tables HeartGold's Pokegear keeps in C (places, fly points, landing
tiles, the radio's map lists, Mom's call intro per map, the phone's script table, greetings
and contact banks), with every map id read out of the tree's own include/constants/maps.h.
"""
import os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "mods", "openmmo", "src", "openmmo_pokegear_tables.c")


def heartgold_dir():
    if len(sys.argv) > 1:
        return sys.argv[1]
    p = subprocess.run(["sh", os.path.join(ROOT, "tools", "decomp_dir.sh"), "pokeheartgold"],
                       capture_output=True, text=True)
    return p.stdout.strip()


def defines(path, prefix):
    out = {}
    for line in open(path, errors="replace"):
        m = re.match(r"#define\s+(%s\w*)\s+(0x[0-9A-Fa-f]+|\d+)" % prefix, line)
        if m:
            out[m.group(1)] = int(m.group(2), 0)
    return out


def main():
    hg = heartgold_dir()
    if not os.path.isdir(hg):
        sys.exit("no heartgold checkout at %r" % hg)
    ids = defines(os.path.join(hg, "include/constants/maps.h"), "MAP_")
    fly = defines(os.path.join(hg, "include/constants/flypoints.h"), "FLYPOINT_")
    items = defines(os.path.join(hg, "include/constants/items.h"), "ITEM_")
    flags = defines(os.path.join(hg, "include/constants/flags.h"), "FLAG_")
    stypes = defines(os.path.join(hg, "include/constants/phone_scripts.h"), "PHONESCRIPTTYPE_")

    def val(tok):
        tok = tok.strip()
        for d in (ids, fly, items, flags, stypes):
            if tok in d:
                return d[tok]
        m = re.match(r"msg_\d+_(\d+)$", tok)
        if m:
            return int(m.group(1))
        if tok.startswith("0x"):
            return int(tok, 16)
        if re.match(r"^-?\d+$", tok):
            return int(tok)
        raise SystemExit("pokegear_tables: cannot read %r" % tok)

    def body(text, head):
        a = text.index(head) + len(head)
        b = re.compile(r"\n\s*\};").search(text, a).start()
        return text[a:b]

    pg = os.path.join(hg, "src/application/pokegear")
    out = []
    w = out.append
    w("/*\n * openmmo_pokegear_tables.c, HeartGold's Pokegear tables, generated.\n *\n"
      " * origin: decomp | pokeheartgold include/constants/maps.h, src/application/\n"
      " * pokegear/map/pokegear_map.c, overlay_101_021F79B4.c, asm/unk_0203BA5C.s,\n"
      " * radio/pokegear_radio.c, radio/shows/pokemon_talk.c, src/data/map_headers.h,\n"
      " * phone/phone_script_defs.c, phone/phone_generic_headers.c,\n"
      " * src/phonebook_dat.c, phone/scripts/phone_scripts_childhood_friend.c,\n"
      " * phone/extra_ethan_lyra_data.c. GPLv3 beside our AGPLv3.\n *\n"
      " * DO NOT EDIT: mmo/tools/pokegear_tables.py writes this file from the\n"
      " * decompilation, so every map id is the tree's own.\n */\n\n"
      "#include \"openmmo_pokegear_tables.h\"\n")

    # the hundred places
    text = open(os.path.join(pg, "map/pokegear_map.c")).read()
    rows = []
    for blk in re.findall(r"\{(.*?)\}", body(text, "sLocationSpecs[PGMAP_NUM_LOCATIONS] = {"), re.S):
        f = dict(re.findall(r"\.(\w+)\s*=\s*([^,\n]+)", blk))
        v = [val(f[k]) for k in ("mapId", "x", "y", "width", "height", "objXoffset", "objYoffset",
                                 "flavorText", "tilemapUnk174BlockID", "tilemapUnk170SrcX",
                                 "tilemapUnk170SrcY", "tilemapUnk170DestWidth", "tilemapUnk170DestHeight")]
        rows.append("    { %s }, /* %s */" % (", ".join(str(x) for x in v), f["mapId"].strip()[4:]))
    assert len(rows) == 100, len(rows)
    w("\n/* The hundred places the map knows: HeartGold's map id, its cell rect, the\n"
      " * marker's offset, its flavor text row in bank 273, and the panel patch. */\n"
      "const PokegearMapLocationSpec gPokegearLocationSpecs[PGMAP_NUM_LOCATIONS] = {\n"
      + "\n".join(rows) + "\n};\n")

    # the fly points
    text = open(os.path.join(pg, "map/overlay_101_021F79B4.c")).read()
    rows = []
    for blk in re.findall(r"\{([^{}]*)\}", body(text, "gMapFlypointParams[] = {")):
        toks = [t for t in blk.split(",") if t.strip()]
        rows.append("    { %s }, /* %s */" % (", ".join(str(val(t)) for t in toks), toks[0].strip()[4:]))
    assert len(rows) == 27, len(rows)
    w("\n/* The fly points: the town named, the map warped to, its flag, its cell and\n"
      " * the panel patch drawn while unvisited. */\n"
      "const MapFlypointParam gPokegearFlypoints[PGMAP_NUM_FLYPOINTS] = {\n" + "\n".join(rows) + "\n};\n")

    # the landing tiles (asm sSpawnMaps: fly point map, x, y on the source's own matrix)
    rows = []
    for line in open(os.path.join(hg, "asm/unk_0203BA5C.s")):
        m = re.match(r"\s*spawn\s+(.*)", line)
        if m:
            t = [x.strip() for x in m.group(1).split(",")]
            rows.append("    { %d, %d, %d }, /* %s */" % (val(t[6]), val(t[7]), val(t[8]), t[6][4:]))
    assert len(rows) == 30, len(rows)
    w("\n/* Where a Fly lands: the town's map and the tile, on HeartGold's matrix. */\n"
      "const FlyLanding gPokegearFlyLandings[PGMAP_NUM_LANDINGS] = {\n" + "\n".join(rows) + "\n};\n")

    def clist(path, name):
        text = open(path).read()
        return [val(t) for t in body(text, name + "[] = {").split(",") if t.strip()]

    alph = clist(os.path.join(pg, "radio/pokegear_radio.c"), "sAlphMaps")
    talk = clist(os.path.join(pg, "radio/shows/pokemon_talk.c"), "sFilterLandmarks")
    w("\n/* The Ruins of Alph, whose signal is the Unown's. */\n"
      "const u16 gPokegearAlphMaps[%d] = { %s };\n" % (len(alph), ", ".join(str(x) for x in alph)))
    w("\n/* The places the talk show never names. */\n"
      "const u16 gPokegearTalkFilter[%d] = { %s };\n" % (len(talk), ", ".join(str(x) for x in talk)))

    # mom's call intro per map, and the map's own region
    text = open(os.path.join(hg, "src/data/map_headers.h")).read()
    intro = [0] * (max(ids.values()) + 1)
    for m in re.finditer(r"\[(MAP_\w+)\]\s*=\s*\{(.*?)\n\s*\}", text, re.S):
        p = re.search(r"\.momCallIntroParam\s*=\s*(\d+)", m.group(2))
        if m.group(1) in ids and p:
            intro[ids[m.group(1)]] = int(p.group(1))
    w("\n/* Which of Mom's greetings a map earns (bank 664 row 7 + this). */\n"
      "const u8 gPokegearMomCallIntro[POKEGEAR_HG_MAPS] = {\n"
      + "\n".join("    " + ", ".join(str(x) for x in intro[i:i + 20]) + "," for i in range(0, len(intro), 20))
      + "\n};\n")
    assert len(intro) == 541, len(intro)

    # the phone's script table
    text = open(os.path.join(pg, "phone/phone_script_defs.c")).read()
    rows = []
    for blk in re.findall(r"\{([^{}]*(?:\{[^{}]*\}[^{}]*)?)\}", body(text, "gPhoneCallScriptDef[] = {")):
        f = dict(re.findall(r"\.(\w+)\s*=\s*([^,\n{]+)", blk))
        m = re.search(r"\.msgIds\s*=\s*\{\s*([^,]+),\s*([^}]+)\}", blk)
        if not m:
            rows.append("    { { 0, 0 }, 0, 0, 0 },")
            continue
        rows.append("    { { %d, %d }, %d, %d, %d }," % (val(m.group(1)), val(m.group(2)), val(f["scriptType"]),
                                                     val(f["param0"]), val(f["param1"])))
    assert len(rows) == 456, len(rows)
    w("\n/* Every phone script: the line for each player (rows of the caller's bank),\n"
      " * and the side effect HeartGold keys on it. */\n"
      "const PhoneCallScriptDef gPokegearPhoneScripts[POKEGEAR_PHONE_SCRIPTS] = {\n" + "\n".join(rows) + "\n};\n")

    # greetings
    text = open(os.path.join(pg, "phone/phone_generic_headers.c")).read()
    rows = []
    for blk in re.findall(r"\{([^{}]*)\}", body(text, "sGreetingMsgIDs[][12] = {")):
        rows.append("    { %s }," % ", ".join(str(val(t)) for t in blk.split(",") if t.strip()))
    assert len(rows) == 8, len(rows)
    w("\n/* The greeting a caller opens with: eight groups, by outgoing/incoming and\n"
      " * the time of day, for each player (bank 640). */\n"
      "const u8 gPokegearGreetings[8][12] = {\n" + "\n".join(rows) + "\n};\n")

    # contact banks
    text = open(os.path.join(hg, "src/phonebook_dat.c")).read()
    banks = re.findall(r"NARC_msg_msg_(\d+)_bin,\s*//\s*(PHONE_CONTACT_\w+)", body(text, "sPhoneMessageGmm[] = {"))
    assert len(banks) == 75, len(banks)
    w("\n/* Each contact's text bank: row 0 the name, the rest the calls. */\n"
      "const u16 gPokegearContactBanks[POKEGEAR_PHONE_CONTACTS] = {\n"
      + "\n".join("    %d, /* %s */" % (int(b), n[14:]) for b, n in banks) + "\n};\n")

    # the friend's remarks per map
    text = open(os.path.join(pg, "phone/scripts/phone_scripts_childhood_friend.c")).read()
    maps = [val(t) for t in body(text, "ov101_021F86CC[] = {").split(",") if t.strip()]
    kinds = [val(t) for t in body(open(os.path.join(pg, "phone/extra_ethan_lyra_data.c")).read(),
                                  "ov101_021F8760[] = {").split(",") if t.strip()]
    assert len(maps) == 73 and len(kinds) == 73, (len(maps), len(kinds))
    w("\n/* The maps the friend has a line about (bank 662 row 13 + i), and which\n"
      " * of them the story has opened (0 always, 1 after the Lake, 2 with the Kanto\n"
      " * map card, 3 with the whole map). */\n"
      "const u16 gPokegearFriendMaps[73] = {\n"
      + "\n".join("    " + ", ".join(str(x) for x in maps[i:i + 12]) + "," for i in range(0, 73, 12)) + "\n};\n"
      "const u8 gPokegearFriendMapKinds[73] = {\n"
      + "\n".join("    " + ", ".join(str(x) for x in kinds[i:i + 24]) + "," for i in range(0, 73, 24)) + "\n};\n")

    open(OUT, "w").write("".join(out))
    print("pokegear_tables: wrote %s from %s" % (os.path.relpath(OUT, ROOT), hg))


if __name__ == "__main__":
    main()
