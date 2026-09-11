#!/usr/bin/env python3
"""Generate the table of HeartGold maps a recipe may ask for."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

MMO = Path(__file__).resolve().parent.parent

# The overworld matrix, by the name HeartGold's own header table gives it.
OVERWORLD = "map_matrix_0000"


def die(msg: str) -> None:
    print("gen_maps: " + msg, file=sys.stderr)
    raise SystemExit(2)


def decomp_dir(name: str) -> Path | None:
    try:
        out = subprocess.run([str(MMO / "tools" / "decomp_dir.sh"), name],
                             capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    return Path(out.stdout.strip())


# One row of the location-name bank, as the decompilation commits it. A
# `<row ... index="N">` and the English string inside it; the two are apart by
# an attribute the reader does not need.
LABEL_ROW = re.compile(
    r'<row\b[^>]*\bindex="(\d+)"[^>]*>(.*?)</row>', re.S)
LABEL_TEXT = re.compile(r'<language name="English">(.*?)</language>', re.S)
LABEL_BANK = "files/msgdata/msg/msg_0279.gmm"


def read_labels(hg: Path) -> dict[int, str]:
    """`map section -> the place name`, out of the committed message bank."""
    path = hg / LABEL_BANK
    if not path.is_file():
        die("no %s, the place names come from the decompilation, not from "
            "the cartridge" % path)
    out: dict[int, str] = {}
    for m in LABEL_ROW.finditer(path.read_text()):
        text = LABEL_TEXT.search(m.group(2))
        if text is None:
            continue
        name = text.group(1).strip()
        if not name or "{" in name or "\\" in name:
            continue
        out[int(m.group(1))] = name
    return out


def main(argv: list[str]) -> int:
    hg = Path(argv[1]) if len(argv) > 1 else decomp_dir("pokeheartgold")
    out = Path(argv[2]) if len(argv) > 2 else MMO / "MAPS"
    if hg is None:
        die("need a heartgold checkout; pass one or set DECOMP_DIR")

    ids: dict[str, int] = {}
    for m in re.finditer(r"#define\s+MAP_(\w+)\s+(\d+)",
                         (hg / "include/constants/maps.h").read_text()):
        ids.setdefault(m.group(1), int(m.group(2)))

    # The place names, by the section id a header names them with. The bank is
    # committed as text in the decompilation, so this needs no cartridge; the
    # porter is what turns a string into a message this game's font can draw.
    secs: dict[str, int] = {}
    for m in re.finditer(r"#define\s+MAPSEC_(\w+)\s+(\d+)",
                         (hg / "include/constants/map_sections.h").read_text()):
        secs.setdefault(m.group(1), int(m.group(2)))
    labels = read_labels(hg)
    if len(labels) < len(secs):
        die("the location-name bank has %d rows and there are %d map sections"
            % (len(labels), len(secs)))

    text = (hg / "src/data/map_headers.h").read_text()
    rows = []
    for m in re.finditer(r"\[MAP_(\w+)\]\s*=\s*\{(.*?)\n\s*\}", text, re.S):
        name, body = m.group(1), m.group(2)
        bank = re.search(r"\.areaDataBank\s*=\s*(\d+)", body)
        matrix = re.search(r"\.matrixId\s*=\s*NARC_map_matrix_map_matrix_(\d+)_", body)
        kind = re.search(r"\.mapType\s*=\s*MAP_TYPE_(\w+)", body)
        region = re.search(r"\.regionNo\s*=\s*MAP_REGION_(\w+)", body)
        mapsec = re.search(r"\.mapsec\s*=\s*MAPSEC_(\w+)", body)
        bgm = re.search(r"\.dayMusicId\s*=\s*(\w+)", body)
        bg = re.search(r"\.battleBg\s*=\s*BATTLE_BG_(\w+)", body)
        events = re.search(r"\.eventsBank\s*=\s*NARC_zone_event_(\d+)_", body)
        script = re.search(r"\.scriptsBank\s*=\s*NARC_scr_seq_scr_seq_(\d+)_", body)
        hdr = re.search(r"\.scriptHeaderBank\s*=\s*NARC_scr_seq_scr_seq_(\d+)_", body)
        icon = re.search(r"\.areaIcon\s*=\s*(\d+)", body)
        cam = re.search(r"\.cameraType\s*=\s*(\d+)", body)
        msg = re.search(r"\.msgBank\s*=\s*NARC_msg_msg_(\d+)_?", body)
        enc = re.search(r"\.wildEncounterBank\s*=\s*ENCDATA_(\w+)", body)
        if not bank or not matrix or not events or name not in ids:
            continue
        mid = int(matrix.group(1))
        # The overworld is matrix 0 and is the one a port cuts; every other
        # matrix belongs to one map and is carried whole.
        sec = secs.get(mapsec.group(1)) if mapsec else None
        rows.append((name.lower(), ids[name], int(bank.group(1)), mid,
                     int(events.group(1)),
                     int(script.group(1)) if script else -1,
                     int(msg.group(1)) if msg else -1,
                     (enc.group(1) if enc and enc.group(1) != "NA" else "-"),
                     (kind.group(1).lower() if kind else "unknown"),
                     (region.group(1).lower() if region else "-"),
                     bgm.group(1) if bgm else "-",
                     bg.group(1).lower() if bg else "-",
                     int(hdr.group(1)) if hdr else -1,
                     int(icon.group(1)) if icon else -1,
                     int(cam.group(1)) if cam else -1,
                     labels.get(sec, "-") if sec is not None else "-"))
    if not rows:
        die("no map rows found in %s/src/data/map_headers.h" % hg)
    rows.sort(key=lambda r: r[1])

    lines = [
        "# MAPS, GENERATED by tools/gen_maps.py; DO NOT EDIT.",
        "#",
        "# The HeartGold maps a `map` recipe line may name.",
        "#",
        "# MATRIX 0 is the shared 47x17 overworld: such a map is the set of",
        "# cells that name its header, and tools/portmap.py cuts the bounding",
        "# rectangle of them into a matrix of its own, so no import ever",
        "# approaches the engine's matrix bound. Any other matrix belongs to",
        "# one map, an interior, usually 1x1, and is carried whole.",
        "#",
        "# No destination member is named here: a package must append",
        "# CONTIGUOUSLY with the built image (pc_modfs refuses an append",
        "# hole), so a package starts at the vanilla count and portmap.py",
        "# allocates from there for every map it is asked for at once.",
        "#",
        "# BGM is the source cartridge's own sequence NAME, which is what",
        "# tools/portmusic.py takes; the number it lands on is the porter's.",
        "#",
        "# REGION is the header's own regionNo. `portmap.py --region johto`",
        "# ports every row with that word, which is what makes a region a",
        "# selection rather than a list of names somebody typed out.",
        "#",
        "# LABEL is the place name the banner draws, from the header's map",
        "# section. It runs to the end of the line and may contain spaces.",
        "#",
        "# SCRIPTS and MSG are the members a map's field scripts and its own",
        "# text live in. tools/portscript.py folds the first out of the",
        "# cartridge and the porter carries the second whole; -1 is a map",
        "# whose header names neither.",
        "#",
        "# ENC is the map's own entry in the source game's encounter data",
        "# (`files/fielddata/encountdata/gs_enc_data.json`, which is committed",
        "# there as text), or `-` for the 405 maps whose header says ENCDATA_NA",
        "# and where nothing is met. A ported map's encounters are the",
        "# SERVER's: the client's own header keeps 0xFFFF, because the engine",
        "# rolling its own would be a second opinion about what a step met.",
        "#",
        "# BG is the header's own battle background, by the name the",
        "# cartridge's decompilation gives it. The two games number the",
        "# shared set identically, plain, water, city, forest, mountain,",
        "# snow, three rooms, three caves, five Elite Four chambers, so",
        "# tools/portmap.py carries it by name and a fight on a ported route",
        "# opens on the field the source drew, not on one guessed from the",
        "# map's kind.",
        "#",
        "# HDR is the member of the map's own arrival-script table, the one",
        "# the header names as scriptHeaderBank: which of the map's scripts",
        "# runs on entry, and the frame table behind it. The porter reads the",
        "# entry off the cartridge and carries it only when the fold left",
        "# nothing in it but the hour and the flags it sets (Routes 34, 35",
        "# and 39 hide and show people by the clock); -1 is a map with none.",
        "#",
        "# ICON is the header's own areaIcon: which of the source game's nine",
        "# place-name signs (its gs_areawindow archive) the banner is drawn",
        "# on when the player arrives. tools/portmap.py appends those nine",
        "# after this game's own and gives the ported header the one it names,",
        "# so a Johto town is announced on Johto's sign and not on Sinnoh's.",
        "#",
        "# CAM is the header's own cameraType: which of the source game's",
        "# seventeen camera templates (its overlay 1 table, ov01_02206478)",
        "# the map is viewed through. tools/portmap.py appends those",
        "# seventeen after this game's own and names each ported header's,",
        "# so a Johto gym is looked at the way its own game looked at it.",
        "#",
        "# Rows: hg <name> <header id> <area bank> <matrix> <events> <scripts>",
        "#          <msg> <enc> <kind> <region> <bgm> <bg> <hdr> <icon> <cam>",
        "#          <label...>",
        "",
    ]
    over = sum(1 for r in rows if r[3] == 0)
    for name, hid, bank, mid, ev, scr, msg, enc, kind, region, bgm, bg, hdr, \
            icon, cam, label in rows:
        lines.append("hg  %-38s %4d %4d %4d %4d %4d %4d %-10s %-14s %-6s %-24s "
                     "%-10s %4d %2d %2d %s"
                     % (name, hid, bank, mid, ev, scr, msg, enc, kind, region,
                        bgm, bg, hdr, icon, cam, label))
    lines.append("")
    out.write_text("\n".join(lines))
    counts: dict[str, int] = {}
    for r in rows:
        counts[r[9]] = counts.get(r[9], 0) + 1
    print("gen_maps: %d maps (%d on the overworld matrix, %s) -> %s"
          % (len(rows), over,
             ", ".join("%d %s" % (v, k) for k, v in sorted(counts.items())),
             out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
