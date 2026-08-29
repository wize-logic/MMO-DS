#!/usr/bin/env python3
"""Generate the terrain-behaviour translation both sides of a map import need."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

MMO = Path(__file__).resolve().parent.parent

PL_HEADER = "include/constants/field/map_tile_behaviors.h"
HG_HEADER = "include/constants/metatile_behavior.h"

PREFIX = "TILE_BEHAVIOR_"

# A value neither game named. Platinum spells it two ways and HeartGold spells
# it as the bare number, so "unnamed" is a shape rather than a single string.
PL_UNNAMED = re.compile(r"^(UNUSED|UNKNOWN)_x[0-9A-Fa-f]{2}$")
HG_UNNAMED = re.compile(r"^\d+(_UNUSED)?$")

# Spellings the two trees genuinely differ on, hg -> (pl, why). Each is one
# pair read off the enum bodies, NOT a fuzzy rule: a guessed match is worse
# than a refusal, because a refusal says so.
ALIASES: dict[str, tuple[str, str]] = {
    "SLIDE_EAST": ("SLIDE_EASTWARD", "spelling"),
    "SLIDE_WEST": ("SLIDE_WESTWARD", "spelling"),
    "SLIDE_NORTH": ("SLIDE_NORTHWARD", "spelling"),
    "SLIDE_SOUTH": ("SLIDE_SOUTHWARD", "spelling"),
    "ROCK_CLIMB_NORTH_SOUTH": ("ROCK_CLIMB_N_S", "spelling"),
    "ROCK_CLIMB_EAST_WEST": ("ROCK_CLIMB_E_W", "spelling"),
    # The two that carry a reading rather than a spelling. Both still land on
    # the same number, but say out loud what the reading is.
    "SNOW": ("SNOW_SHALLOW",
             "heartgold names no other snow depth, and platinum's deep three "
             "sit at 0xA1-0xA3 which heartgold leaves unnamed"),
    "EMPTY_TRASH_CAN": ("TRASH_CAN",
                        "heartgold's is the searched-already variant of the "
                        "one tile platinum names"),
}


def die(msg: str) -> None:
    print("gen_terrain_map: " + msg, file=sys.stderr)
    raise SystemExit(2)


def decomp_dir(name: str) -> Path | None:
    try:
        out = subprocess.run([str(MMO / "tools" / "decomp_dir.sh"), name],
                             capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    return Path(out.stdout.strip())


def read_enum(path: Path, enum_name: str) -> dict[int, str]:
    """Every member of one C enum, by value. Implicit values are counted."""
    if not path.is_file():
        die("no %s, this table's whole point is that it has an oracle at "
            "both ends, so a missing one is a refusal, not a default" % path)
    text = path.read_text()
    m = re.search(r"enum\s+" + enum_name + r"\s*\{(.*?)\}\s*;", text, re.S)
    if not m:
        die("%s has no `enum %s`" % (path, enum_name))
    body = m.group(1)
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)

    out: dict[int, str] = {}
    nxt = 0
    for part in body.split(","):
        part = part.strip()
        if not part:
            continue
        mm = re.match(r"^(\w+)\s*(?:=\s*(0[xX][0-9A-Fa-f]+|\d+))?$", part)
        if not mm:
            die("%s: cannot read enum member %r" % (path, part))
        name, val = mm.group(1), mm.group(2)
        n = int(val, 0) if val is not None else nxt
        if n in out:
            die("%s: value %d is both %s and %s" % (path, n, out[n], name))
        if not name.startswith(PREFIX):
            die("%s: %s does not start with %s" % (path, name, PREFIX))
        out[n] = name[len(PREFIX):]
        nxt = n + 1
    return out


def named(name: str, unnamed: re.Pattern) -> bool:
    return not unnamed.match(name)


def check_aliases(pl: dict[int, str], hg: dict[int, str]) -> None:
    """Hold every alias to the evidence that justified it: the same number on
    both sides. An alias that has stopped being same-number is refused."""
    pl_at = {n: v for v, n in pl.items()}
    hg_at = {n: v for v, n in hg.items()}
    for src, (dst, why) in sorted(ALIASES.items()):
        if src not in hg_at:
            die("alias %s -> %s: heartgold no longer names %s, so the pair "
                "has nothing behind it" % (src, dst, src))
        if dst not in pl_at:
            die("alias %s -> %s: platinum no longer names %s" % (src, dst, dst))
        if hg_at[src] != pl_at[dst]:
            die("alias %s -> %s was justified by both trees naming 0x%02X, "
                "and they now sit at 0x%02X and 0x%02X, re-read them rather "
                "than keeping the pair (%s)"
                % (src, dst, hg_at[src], hg_at[src], pl_at[dst], why))


def classify(pl: dict[int, str], hg: dict[int, str]) -> list[tuple]:
    """One row per HeartGold value: (value, hg name, pl name, verdict, note)."""
    pl_by_name = {}
    for v, n in pl.items():
        if named(n, PL_UNNAMED):
            pl_by_name.setdefault(n, v)

    rows = []
    for v in sorted(hg):
        hgn = hg[v]
        pln = pl.get(v, "")
        if not named(hgn, HG_UNNAMED):
            verdict = "neither" if not named(pln, PL_UNNAMED) else "plonly"
            rows.append((v, v, hgn, pln, verdict, ""))
            continue

        want, why = ALIASES.get(hgn, (hgn, ""))
        target = pl_by_name.get(want)
        if target is None:
            # Platinum has no name for this behaviour anywhere. Whether that is
            # merely lossy or actively wrong depends on what Platinum does with
            # the number, so say which.
            if named(pln, PL_UNNAMED):
                # Platinum owns this number and means something else by it.
                # Copying would not lose the behaviour, it would invent one, so
                # the byte is cleared and bit 15 is left to carry the tile.
                rows.append((v, 0, hgn, pln, "hgonly-unsafe",
                             "platinum's 0x%02X is %s, so copying would claim "
                             "that; cleared to NONE and the tile keeps its "
                             "collision" % (v, pln)))
            else:
                rows.append((v, v, hgn, pln, "hgonly-inert",
                             "platinum leaves 0x%02X unnamed, so the byte "
                             "carries, the tile keeps its collision and only "
                             "this behaviour is lost" % v))
            continue
        kind = "alias-" if want != hgn else ""
        if target == v:
            verdict = kind + "same"
            note = "platinum spells it %s: %s" % (want, why) if kind else ""
        else:
            verdict = kind + "move"
            note = ""
            if kind:
                note = "platinum spells it %s: %s" % (want, why)
            if named(pln, PL_UNNAMED) and pln != want:
                note += ("; " if note else "") + \
                    "platinum's own 0x%02X is %s" % (v, pln)
        rows.append((v, target, hgn, pln, verdict, note))
    return rows


def near_misses(pl: dict[int, str], hg: dict[int, str]) -> None:
    """Names HeartGold has that Platinum does not, beside Platinum's whole
    named set -- so an alias is chosen off the evidence, not invented."""
    pl_named = sorted(n for n in pl.values() if named(n, PL_UNNAMED))
    missing = [(v, n) for v, n in sorted(hg.items())
               if named(n, HG_UNNAMED) and n not in set(pl_named)]
    print("heartgold names %d values platinum does not:" % len(missing))
    for v, n in missing:
        toks = set(re.split(r"_", n))
        best = sorted(((len(toks & set(re.split(r"_", p))), p)
                       for p in pl_named), reverse=True)[:3]
        print("  0x%02X %-34s  closest pl: %s"
              % (v, n, ", ".join("%s(%d)" % (p, s) for s, p in best if s)))


def emit(rows: list[tuple], pl: dict[int, str], hg: dict[int, str]) -> str:
    counts: dict[str, int] = {}
    for r in rows:
        counts[r[4]] = counts.get(r[4], 0) + 1
    order = ["same", "move", "alias-same", "alias-move", "hgonly-inert",
             "hgonly-unsafe", "plonly", "neither"]
    tally = ", ".join("%s %d" % (k, counts[k]) for k in order if k in counts)

    out = [
        "# TERRAIN_MAP, GENERATED by tools/gen_terrain_map.py; DO NOT EDIT.",
        "#",
        "# What a map's terrain behaviour byte means on each side of an import.",
        "# Collision is bit 15 of the same u16 and means the same thing in both",
        "# games, so it is not in this table; only the low byte is.",
        "#",
        "# The two columns of names are DIFFERENT SPACES and are tagged so.",
        "# `pl` names come from pokeplatinum's `enum TileBehavior`, `hg` names",
        "# from pokeheartgold's `enum TILE_BEHAVIOR`. Platinum names %d of %d"
        % (sum(1 for n in pl.values() if named(n, PL_UNNAMED)), len(pl)),
        "# values and HeartGold %d of %d. Upstream reformatted HeartGold's"
        % (sum(1 for n in hg.values() if named(n, HG_UNNAMED)), len(hg)),
        "# naming to match Platinum's, which is why most rows match at all.",
        "#",
        "# VERDICT is what an importer may do with the row:",
        "#   same        both name it at the same number, copy the byte",
        "#   move        both name it at different numbers, rewrite it",
        "#   alias-same  ) as above, once a spelling this tool records is",
        "#   alias-move  ) applied; every alias is same-number on both sides",
        "#   hgonly-inert   heartgold names it and platinum leaves the number",
        "#                  unused, so the byte carries and only heartgold's",
        "#                  extra behaviour is lost",
        "#   hgonly-unsafe  heartgold names it and platinum means something",
        "#                  ELSE by that number, copying would not lose a",
        "#                  behaviour, it would invent one, so WRITE says 0x00",
        "#   plonly      platinum names it, heartgold does not, so the number",
        "#               acquires platinum's meaning; weaker than `same`,",
        "#               because only one end is an oracle",
        "#   neither     neither names it; nothing is known, nothing claimed",
        "#",
        "# %s" % tally,
        "#",
        "# WRITE is the byte to put in the platinum map, so a converter needs",
        "# no rule beyond this column.",
        "#",
        "# Rows: <hg value> <write> <verdict> <hg name> <pl name> [note]",
        "",
    ]
    for v, tgt, hgn, pln, verdict, note in rows:
        line = "0x%02X  0x%02X  %-13s %-34s %-34s" % (v, tgt, verdict, hgn,
                                                      pln or "-")
        if note:
            line = line.rstrip() + "  # " + note
        out.append(line.rstrip())
    out.append("")
    return "\n".join(out)


def main(argv: list[str]) -> int:
    args = [a for a in argv[1:] if not a.startswith("--")]
    flags = {a for a in argv[1:] if a.startswith("--")}

    pldir = Path(args[0]) if len(args) > 0 else None
    hgdir = Path(args[1]) if len(args) > 1 else None
    out = Path(args[2]) if len(args) > 2 else MMO / "TERRAIN_MAP"

    if pldir is None:
        env = subprocess.run(["sh", "-c", 'echo "${ENGINE_DIR:-}"'],
                             capture_output=True, text=True).stdout.strip()
        pldir = Path(env) if env else decomp_dir("pokeplatinum")
    if hgdir is None:
        hgdir = decomp_dir("pokeheartgold")
    if pldir is None or hgdir is None:
        die("need a platinum checkout and a heartgold one; pass them, or set "
            "ENGINE_DIR / DECOMP_DIR")

    pl = read_enum(pldir / PL_HEADER, "TileBehavior")
    hg = read_enum(hgdir / HG_HEADER, "TILE_BEHAVIOR")
    check_aliases(pl, hg)

    if "--near" in flags:
        near_misses(pl, hg)
        return 0

    rows = classify(pl, hg)
    out.write_text(emit(rows, pl, hg))
    counts: dict[str, int] = {}
    for r in rows:
        counts[r[4]] = counts.get(r[4], 0) + 1
    print("gen_terrain_map: %d rows -> %s  (%s)"
          % (len(rows), out,
             ", ".join("%s %d" % kv for kv in sorted(counts.items()))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
