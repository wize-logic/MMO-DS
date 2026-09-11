#!/usr/bin/env python3
"""Generate mmo/TRAINER_CLASS: a HeartGold trainer class -> this game's."""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

MMO = Path(__file__).resolve().parent.parent

HG_CONSTANTS = "include/constants/trainer_class.h"
HG_NAMES = "files/msgdata/msg/msg_0730.gmm"
HG_GENDERS = "src/trainer_data.c"
HG_ENCOUNTER_BGM = "src/field_bgm.c"
# The two tables a class byte still indexes in HeartGold's assembly: what a
# class pays per level of its last monster (the battle overlay's
# sPrizeMoneyTbl, one row per class) and which theme its fight opens with
# (unk_020517A4.s: a class -> parameter list, then parameter -> sequence).
HG_PRIZES = "asm/overlay_12_battle_command.s"
HG_BATTLE_BGM = "asm/unk_020517A4.s"

# Appended, not stood in for. A class this game has no drawing of used to
# fight as one of two plain classes, nameless and prizeless.
HG_PICTURE_CLASSES = 129
PL_CLASS_COUNT = 105

# What a trainer of a class is heard as when their eyes meet yours, which is a
# different question from what they are drawn as and has to be asked
# separately.
HG_ENCOUNTER_DEFAULT = "SEQ_GS_EYE_J_SHOUNEN"

PL_CONSTANTS = ("build/rom/generated/trainer_classes.h",
                "build/pc/geninclude/generated/trainer_classes.h",
                "generated/trainer_classes.h")
PL_NAMES = "res/text/trainer_class_names.json"

# The two classes of this game's that no trainer of this game's uses. Measured
# 2026-08-30 over all 928 trdata members: 1, 103 and 104 are unused, and 1 is
# the female player. Both print the generic "Pokemon Trainer".
FALLBACK = {"MALE": 103, "FEMALE": 104}


def die(msg: str) -> None:
    print("gen_trainer_class: " + msg, file=sys.stderr)
    raise SystemExit(2)


def decomp_dir(name: str) -> Path:
    try:
        out = subprocess.run([str(MMO / "tools" / "decomp_dir.sh"), name],
                             capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        die("no checkout of %s; tools/decomp_dir.sh says where it looks" % name)
    return Path(out.stdout.strip())


def normalise(name: str) -> str:
    """One spelling of a class, out of the two trees' habits."""
    for prefix in ("PKMN_", "POKEMON_"):
        if name.startswith(prefix):
            name = name[len(prefix):]
    if name.endswith("_GS"):
        name = name[:-3]
    name = re.sub(r"_M$", "_MALE", name)
    name = re.sub(r"_F$", "_FEMALE", name)
    return name


def hg_classes(tree: Path) -> dict[int, str]:
    text = (tree / HG_CONSTANTS).read_text()
    out = {int(m.group(2)): m.group(1) for m in
           re.finditer(r"#define\s+TRAINERCLASS_(\w+)\s+(\d+)", text)}
    if not out:
        die("no TRAINERCLASS_ constants in %s" % (tree / HG_CONSTANTS))
    return out


def hg_names(tree: Path) -> dict[int, str]:
    text = (tree / HG_NAMES).read_text(encoding="utf-8")
    out = {}
    for m in re.finditer(r'<row id="[^"]*" index="(\d+)">.*?'
                         r'<language name="English">(.*?)</language>',
                         text, re.S):
        out[int(m.group(1))] = m.group(2).strip()
    if not out:
        die("no rows in %s" % (tree / HG_NAMES))
    return out


def hg_encounter_bgm(tree: Path) -> dict[str, tuple[str, str]]:
    """Each class's eyes-meet track, by class constant, Johto then Kanto."""
    text = (tree / HG_ENCOUNTER_BGM).read_text()
    body = re.search(r"sTrainerEncounterMusicParam\[\]\[3\]\s*=\s*\{(.*?)\n\};",
                     text, re.S)
    if body is None:
        die("no sTrainerEncounterMusicParam[][3] in %s"
            % (tree / HG_ENCOUNTER_BGM))
    out = {}
    for cls, johto, kanto in re.findall(
            r"\{\s*(TRAINERCLASS_\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\}",
            body.group(1)):
        out[cls] = (johto, kanto)
    if not out:
        die("no rows in sTrainerEncounterMusicParam[][3]")
    return out


def hgb_of(hgb: dict[str, tuple[str, str]], name: str) -> tuple[str, str]:
    """A class with no row of its own is heard as the table's own default."""
    return hgb.get("TRAINERCLASS_" + name,
                   (HG_ENCOUNTER_DEFAULT, HG_ENCOUNTER_DEFAULT))


def hg_genders(tree: Path) -> dict[str, str]:
    """The class -> gender table HeartGold's own party generator reads."""
    text = (tree / HG_GENDERS).read_text()
    m = re.search(r"static const u8 sTrainerGenders\[\] = \{(.*?)\n\};",
                  text, re.S)
    if m is None:
        die("no sTrainerGenders[] in %s" % (tree / HG_GENDERS))
    out = {}
    for row in re.finditer(r"TRAINER_(MALE|FEMALE|DOUBLE),\s*//\s*"
                           r"TRAINERCLASS_(\w+)", m.group(1)):
        out[row.group(2)] = row.group(1)
    if not out:
        die("sTrainerGenders[] in %s names no class in its comments"
            % (tree / HG_GENDERS))
    return out


def hg_prizes(tree: Path) -> dict[str, int]:
    """What a class pays per level of its last monster, by class constant."""
    text = (tree / HG_PRIZES).read_text()
    body = text.split("sPrizeMoneyTbl:", 1)
    if len(body) < 2:
        die("no sPrizeMoneyTbl in %s" % (tree / HG_PRIZES))
    out = {}
    for m in re.finditer(r"\.short\s+TRAINERCLASS_(\w+),\s*(\d+)", body[1]):
        out[m.group(1)] = int(m.group(2))
        if len(out) > 400:
            break
    if not out:
        die("sPrizeMoneyTbl in %s names no class" % (tree / HG_PRIZES))
    return out


def hg_battle_bgm(tree: Path) -> dict[str, str]:
    """The theme a class's fight opens with, by class constant."""
    text = (tree / HG_BATTLE_BGM).read_text()
    m1 = re.search(r"_020FC3CA:\n(.*?)\n_020FC40A:", text, re.S)
    m2 = re.search(r"_020FC40A:\n(.*?)\n\s*\.text", text, re.S)
    if m1 is None or m2 is None:
        die("the class and parameter tables moved in %s" % (tree / HG_BATTLE_BGM))
    param_of = {c: int(n) for c, n in
                re.findall(r"TRAINERCLASS_(\w+)\s*\|\s*\((\d+)\s*<<\s*10\)",
                           m1.group(1))}
    seqs = re.findall(r"\.short\s+(?:0x[0-9A-Fa-f]+|\d+),\s*(SEQ_\w+)",
                      m2.group(1))
    out = {}
    for cls, n in param_of.items():
        if n < len(seqs):
            out[cls] = seqs[n]
    if not out:
        die("the class table in %s names no class" % (tree / HG_BATTLE_BGM))
    return out


def pl_classes(tree: Path) -> dict[int, str]:
    for rel in PL_CONSTANTS:
        if (tree / rel).is_file():
            text = (tree / rel).read_text()
            break
    else:
        die("no generated/trainer_classes.h under %s (an engine build writes "
            "it; this table is committed so a fill never needs one)" % tree)
    out: dict[int, str] = {}
    for m in re.finditer(r"TRAINER_CLASS_(\w+)\s*=\s*(\d+),", text):
        out.setdefault(int(m.group(2)), m.group(1))
    if not out:
        die("no TRAINER_CLASS_ constants in %s" % tree)
    return out


def pl_names(tree: Path) -> list[str]:
    d = json.loads((tree / PL_NAMES).read_text(encoding="utf-8"))
    return [m["en_US"].strip() for m in d["messages"]]


def main(argv: list[str]) -> int:
    out_path = Path(argv[0]) if argv else MMO / "TRAINER_CLASS"
    hg = decomp_dir("pokeheartgold")
    pl = decomp_dir("pokeplatinum")
    print("gen_trainer_class: heartgold %s" % hg)
    print("gen_trainer_class: platinum  %s" % pl)

    hgc, hgn, hgg = hg_classes(hg), hg_names(hg), hg_genders(hg)
    plc, pln = pl_classes(pl), pl_names(pl)
    if len(pln) != len(plc):
        die("this game has %d classes and %d printed names; the two have to be "
            "the same list" % (len(plc), len(pln)))

    by_norm: dict[str, int] = {}
    for i, name in sorted(plc.items()):
        by_norm.setdefault(normalise(name), i)

    hgb = hg_encounter_bgm(hg)
    hgp = hg_prizes(hg)
    hgv = hg_battle_bgm(hg)
    if len(plc) != PL_CLASS_COUNT:
        die("this game has %d classes and the appended ids start at %d; "
            "one of the two moved" % (len(plc), PL_CLASS_COUNT))

    def extras(name: str) -> tuple[str, int, str]:
        gender = hgg.get(name, "MALE")
        if gender == "DOUBLE":
            gender = "MALE"
        return (gender[0], hgp.get(name, 0), hgv.get(name, "-"))

    rows, refused = [], {}
    appended = 0
    for i in sorted(hgc):
        name = hgc[i]
        j = by_norm.get(normalise(name))
        if j is None:
            refused[i] = "no class of this game's is spelt like it"
        elif i not in hgn:
            refused[i] = "HeartGold prints no name for it"
        elif hgn[i] != pln[j]:
            refused[i] = ("the constants agree and the printed names do not "
                          "(%r against %r)" % (hgn[i], pln[j]))
        else:
            rows.append((i, j, "paired", hgn[i], name, plc[j])
                        + hgb_of(hgb, name) + extras(name))
            continue
        if i < HG_PICTURE_CLASSES and i in hgn:
            j = PL_CLASS_COUNT + appended
            appended += 1
            rows.append((i, j, "appended", hgn[i], name, "-")
                        + hgb_of(hgb, name) + extras(name))
            continue
        gender = hgg.get(name, "MALE")
        if gender == "DOUBLE":
            gender = "MALE"
        j = FALLBACK[gender]
        rows.append((i, j, "fallback", hgn.get(i, "-"), name, plc[j])
                    + hgb_of(hgb, name) + extras(name))

    paired = sum(1 for r in rows if r[2] == "paired")
    text = [
        "# TRAINER_CLASS, GENERATED by tools/gen_trainer_class.py;"
        " DO NOT EDIT.",
        "#",
        "# A HeartGold trainer class -> this game's. tools/porttrainers.py",
        "# reads it, and a ported trainer's `trdata` carries the number in",
        "# the second column rather than its own.",
        "#",
        "# WHY A TABLE AT ALL. This game indexes three 105-entry tables by the",
        "# class byte, the gender its party generator uses, the prize",
        "# multiplier, and the name a battle prints. HeartGold's classes run",
        "# to %d, so a carried byte reads past the end of all three."
        % max(hgc),
        "#",
        "# TWO ORACLES. A `paired` row is one both agree on: the two trees'",
        "# own constants, normalised for the spelling each happens to use,",
        "# AND the name each game actually prints (HeartGold's msg_0730 by",
        "# class, this game's bank 619). Neither is derived from the other.",
        "# The constants alone pair Johto's Rival with Sinnoh's, whose text",
        "# reads %r; the text alone cannot tell apart the eleven classes of"
        % pln[63],
        "# this game's that print the same generic line.",
        "#",
        "# An `appended` row is a class this game has no drawing of, carried",
        "# whole: tools/porttrainers.py appends its five picture files, its",
        "# printed name, its gender and its prize rate at the id in the second",
        "# column, %d and up, in the source's own order, and the patched"
        % PL_CLASS_COUNT,
        "# engine reads a class past its tables out of the package. Sage,",
        "# Firebreather, Team Rocket, every gym leader: a Johto class is",
        "# drawn and named as its own.",
        "#",
        "# A `fallback` row is a class the cartridge draws no picture of (the",
        "# constants past %d are phone-book ids sharing the namespace); its"
        % HG_PICTURE_CLASSES,
        "# trainers fight as %d or %d by the HeartGold class's own gender,"
        % (FALLBACK["MALE"], FALLBACK["FEMALE"]),
        "# the two classes no trainer of this game's uses.",
        "#",
        "# %d classes: %d paired, %d appended, %d fallback."
        % (len(rows), paired, appended, len(rows) - paired - appended),
        "#",
        "# The two tracks are what HeartGold plays when a trainer of that",
        "# class sees you, in Johto and in Kanto. This game chooses that off",
        "# the class byte as well, so without them a carried trainer is heard",
        "# as whatever stand-in they are drawn as. Then the class's gender",
        "# (M/F), what it pays per level of its last monster, and the theme",
        "# its fight opens with (`-` is the plain trainer theme; the gym",
        "# leader one is region-specific and the porter takes the header's).",
        "#",
        "# Rows: hg <class> <this game's> <how> <heartgold> <this game's>"
        " <johto track> <kanto track> <gender> <prize> <battle>  # printed name",
        "",
    ]
    for i, j, how, shown, hn, pn, jseq, kseq, gender, prize, battle in rows:
        text.append("hg %4d %4d  %-8s  %-26s %-26s %-22s %-22s %s %3d %-24s  # %s"
                    % (i, j, how, hn, pn, jseq, kseq, gender, prize, battle,
                       shown))
    out_path.write_text("\n".join(text) + "\n")
    print("gen_trainer_class: %d rows -> %s (%d paired, %d appended, %d "
          "fallback)" % (len(rows), out_path, paired, appended,
                         len(rows) - paired - appended))
    for i in sorted(refused):
        if any(r[0] == i and r[2] == "fallback" for r in rows):
            print("  %3d %-24s %s" % (i, hgc[i], refused[i]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
