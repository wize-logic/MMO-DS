#!/usr/bin/env python3
"""Generate mmo/STORYEND: the state HeartGold's story leaves its save in."""
from __future__ import annotations

import collections
import re
import sys
from pathlib import Path

MMO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(MMO / "tools"))
import gen_mapscenes as gm  # noqa: E402

NOT_STORY_VAR = ("VAR_TEMP", "VAR_SPECIAL", "VAR_OBJ_", "VAR_BATTLE_RESULT")
PER_CHARACTER_FLAG = ("FLAG_GOT_", "FLAG_DAILY_",
                      "FLAG_HIDDENITEM_", "FLAG_ITEM_", "FLAG_TEMP",
                      "FLAG_MAPTEMP", "FLAG_DEFEATED_")
STD_INIT_STEM = "scr_seq_0149"


def die(msg: str) -> None:
    print("gen_storyend: " + msg, file=sys.stderr)
    raise SystemExit(2)


def value_of(tok: str) -> int | None:
    if tok == "TRUE":
        return 1
    if tok == "FALSE":
        return 0
    try:
        return int(tok, 0)
    except ValueError:
        return None


def reachable(ops: list, labels: dict[str, int], start: int) -> set[int]:
    """Every instruction index an entry can run: both arms of every branch,
    every call, until End or a Return with nothing to return to."""
    seen: set[int] = set()
    todo = [start]
    while todo:
        pc = todo.pop()
        while 0 <= pc < len(ops) and pc not in seen:
            seen.add(pc)
            name, args = ops[pc]
            if name in gm.STOPS or name == "Return":
                break
            if name == "GoTo":
                pc = labels.get(args[0], -1)
                continue
            m = re.match(r"^(GoTo|Call)(?:If(Set|Unset|Defeated|NotDefeated|"
                         r"Eq|Ne|Gt|Ge|Lt|Le))?$", name)
            if m and args:
                dest = args[-1] if m.group(2) in ("Set", "Unset", "Defeated",
                                                  "NotDefeated") else args[0]
                if dest in labels:
                    todo.append(labels[dest])
            pc += 1
    return seen


def daily_range(hg: Path) -> range:
    """The flags the game clears at the turn of every day, by number."""
    text = (hg / "include/constants/flags.h").read_text()
    base = re.search(r"#define\s+DAILY_FLAG_BASE\s+(0x[0-9A-Fa-f]+|\d+)", text)
    count = re.search(r"#define\s+NUM_DAILY_FLAGS\s+(\d+)", text)
    if not base or not count:
        raise SystemExit("gen_storyend: flags.h names no DAILY_FLAG_BASE / NUM_DAILY_FLAGS")
    b = int(base.group(1), 0)
    return range(b, b + int(count.group(1)))


def main(argv: list[str]) -> int:
    hg = Path(argv[1]) if len(argv) > 1 else gm.decomp_dir("pokeheartgold")
    out = Path(argv[2]) if len(argv) > 2 else MMO / "STORYEND"
    if hg is None:
        die("need a heartgold checkout; pass one or set DECOMP_DIR")
    flagno = {}
    for m in re.finditer(r"#define\s+(FLAG_\w+)\s+(0x[0-9A-Fa-f]+|\d+)",
                         (hg / "include/constants/flags.h").read_text()):
        flagno.setdefault(m.group(1), int(m.group(2), 0))
    varno = {}
    for m in re.finditer(r"#define\s+(VAR_\w+)\s+(0x[0-9A-Fa-f]+|\d+)",
                         (hg / "include/constants/vars.h").read_text()):
        varno.setdefault(m.group(1), int(m.group(2), 0))
    seq = hg / "files/fielddata/script/scr_seq"
    by_stem = {p.stem: p for p in seq.glob("scr_seq_*.s")}
    banks: dict[str, gm.Bank] = {}

    def load_bank(stem: str) -> gm.Bank | None:
        if stem in banks:
            return banks[stem]
        p = by_stem.get(stem)
        if p is None:
            return None
        banks[stem] = gm.Bank(p, {})
        return banks[stem]

    std = gm.StdBanks(hg, load_bank)

    # The new-game state: std_init, the way gen_mapscenes runs it.
    fresh = gm.World()
    try:
        ibank, istart = std.resolve("std_init")
        gm.run(ibank, istart, fresh, std)
    except gm.Unsupported as exc:
        die("std_init did not evaluate: %s" % exc)
    init_ops: set[tuple[str, int]] = {(ibank.name, i) for i in reachable(ibank.ops, ibank.labels, istart)}

    # The arrival entries: every ON_TRANSITION / ON_LOAD body, in the map's
    # own bank or the std bank, marked so their writes are left to
    # gen_mapscenes.
    arrival_ops: set[tuple[str, int]] = set()
    # A map's header bank and its script bank carry different numbers
    # (scr_seq_0521_T02R0302_hdr beside scr_seq_0748_T02R0302), and the
    # pairing is the map header's: src/data/map_headers.h names both.
    pairs: list[tuple[Path, str]] = []
    headers = (hg / "src/data/map_headers.h").read_text()
    for m in re.finditer(r"\[MAP_(\w+)\]\s*=\s*\{(.*?)\n\s*\}", headers, re.S):
        hdr_c = re.search(r"\.scriptHeaderBank\s*=\s*NARC_scr_seq_(scr_seq_\d+_\w+?)_bin", m.group(2))
        scr_c = re.search(r"\.scriptsBank\s*=\s*NARC_scr_seq_(scr_seq_\d+_\w+?)_bin", m.group(2))
        if hdr_c and scr_c and hdr_c.group(1) in by_stem:
            pairs.append((by_stem[hdr_c.group(1)], scr_c.group(1)))
    if not pairs:
        die("no map names its script banks in src/data/map_headers.h")
    for hdr, stem in pairs:
        bank = load_bank(stem)
        for ent in gm.arrival_scripts(hdr):
            sm = re.match(r"_EV_(\w+?)(?:\s*\+\s*\d+)?$", ent)
            if sm:
                if bank is None or sm.group(1) not in bank.labels:
                    continue
                arrival_ops |= {(bank.name, i) for i in reachable(bank.ops, bank.labels, bank.labels[sm.group(1)])}
            else:
                try:
                    sbank, start = std.resolve(ent)
                except gm.Unsupported:
                    continue
                arrival_ops |= {(sbank.name, i) for i in reachable(sbank.ops, sbank.labels, start)}

    setters: dict[str, set[tuple[str, int]]] = collections.defaultdict(set)
    clearers: dict[str, set[tuple[str, int]]] = collections.defaultdict(set)
    arr_setters: dict[str, set[tuple[str, int]]] = collections.defaultdict(set)
    arr_clearers: dict[str, set[tuple[str, int]]] = collections.defaultdict(set)
    writes: dict[str, list[int]] = collections.defaultdict(list)
    touched: set[str] = set()
    for stem in sorted(by_stem):
        if stem.endswith("_hdr"):
            continue
        bank = load_bank(stem)
        if bank is None:
            continue
        for i, (name, args) in enumerate(bank.ops):
            key = (bank.name, i)
            if key in init_ops:
                continue
            if key in arrival_ops:
                if name == "SetFlag" and args:
                    arr_setters[args[0]].add(key)
                elif name == "ClearFlag" and args:
                    arr_clearers[args[0]].add(key)
                continue
            if name == "SetFlag" and args:
                setters[args[0]].add(key)
            elif name == "ClearFlag" and args:
                clearers[args[0]].add(key)
            elif name == "SetVar" and len(args) == 2 and not args[0].startswith(NOT_STORY_VAR):
                v = value_of(args[1])
                touched.add(args[0])
                if v is not None:
                    writes[args[0]].append(v)
            elif name in ("CopyVar", "SetOrCopyVar", "AddVar", "SubVar") and args and not args[0].startswith(NOT_STORY_VAR):
                touched.add(args[0])

    var_rows = []
    resets = 0
    for var in sorted(touched):
        vals = writes.get(var, [])
        if var not in varno:
            continue
        if not vals:
            continue
        if 0 in vals and any(v > 0 for v in vals):
            resets += 1
            continue
        end = max(vals)
        if end == 0:
            continue
        var_rows.append((var, varno[var], end, len(vals)))

    # The daily flags are a range, not a name: DAILY_FLAG_BASE and
    # NUM_DAILY_FLAGS in flags.h, memset by ClearDailyFlags at the day's turn
    # (script_manager.c). Only some carry FLAG_DAILY_ in their name; the
    # lottery's two (FLAG_UNK_AA5, FLAG_UNK_AA6) do not, and resting them set
    # told every visitor they had drawn a ticket already (owner, 2026-09-02).
    daily = daily_range(hg)
    flag_rows = []
    review_both_hide = []
    per_character = 0
    for flag in sorted(set(setters) | set(clearers) | set(arr_setters) | set(arr_clearers)):
        if flag not in flagno:
            continue
        if flag.startswith(PER_CHARACTER_FLAG) or flagno[flag] in daily:
            per_character += 1
            continue
        s, c = setters.get(flag, set()), clearers.get(flag, set())
        by = "script(s)"
        if not s and not c:
            s, c = arr_setters.get(flag, set()), clearers.get(flag, set()) | arr_clearers.get(flag, set())
            by = "arrival script(s), which no scene touches"
        hide = flag.startswith("FLAG_HIDE_")
        if s and c:
            end = 1 if hide else 0
            why = "set in %d and cleared in %d %s: %s" % (
                len(s), len(c), by, "the actor leaves" if hide else "the scene ends")
            if hide:
                review_both_hide.append(flag)
        elif s:
            end, why = 1, "set in %d %s, never cleared" % (len(s), by)
        else:
            end, why = 0, "cleared in %d %s, never set" % (len(c), by)
        initial = 1 if flag in fresh.flags else 0
        if end == initial:
            continue
        flag_rows.append((flag, flagno[flag], end, why))

    lines = [
        "# STORYEND, GENERATED by tools/gen_storyend.py; DO NOT EDIT.",
        "#",
        "# The state HeartGold's story leaves its save in, read out of its own",
        "# scripts: what a visitor's Johto and Kanto exist as, by the owner's",
        "# word of 2026-09-02. tools/gen_mapscenes.py starts every arrival",
        "# script from std_init plus this; tools/portscript.py answers every",
        "# variable read a talk script makes from this; the server's people",
        "# follow the scenes those two agree on (mmo/MAPSCENES, mmo/MAPWALLS).",
        "#",
        "# A variable that only climbs rests at its highest write; one the",
        "# scripts ever reset to zero rests at zero (%d such, not rows). A flag" % resets,
        "# a story scene set and never cleared rests set, one cleared and never",
        "# set rests clear, one written both ways rests set if it hides a",
        "# person and clear otherwise; a flag no scene writes is decided by",
        "# the arrival scripts under the same rules. A row is written only",
        "# where the end differs from the new game's std_init.",
        "# Per-character records (%d flags by name: gifts, dailies, items," % per_character,
        "# trainers, map-temporaries) are not rows.",
        "#",
        "# %d variable rows, %d flag rows." % (len(var_rows), len(flag_rows)),
        "#",
        "# Rows: var  <NAME=number> <value>   # writes",
        "#       flag <NAME=number> <0|1>     # why",
        "",
    ]
    for var, no, end, n in var_rows:
        lines.append("var   %-44s %-5d # %d write(s), highest %d" % ("%s=0x%04X" % (var, no), end, n, end))
    lines.append("")
    for flag, no, end, why in flag_rows:
        lines.append("flag  %-44s %-5d # %s" % ("%s=0x%03X" % (flag, no), end, why))
    lines.append("")
    out.write_text("\n".join(lines))
    print("gen_storyend: %d variable rows, %d flag rows (%d reset vars at zero, "
          "%d per-character flags left to the server, %d hide flags written "
          "both ways and rested set) -> %s"
          % (len(var_rows), len(flag_rows), resets, per_character,
             len(review_both_hide), out))
    return 0


def load(path: Path) -> tuple[dict[int, int], dict[int, int]]:
    """(var number -> value, flag number -> 0|1) out of mmo/STORYEND."""
    vars_: dict[int, int] = {}
    flags: dict[int, int] = {}
    if not path.is_file():
        return vars_, flags
    for line in path.read_text().splitlines():
        m = re.match(r"^(var|flag)\s+\w+=(0x[0-9A-Fa-f]+)\s+(\d+)", line)
        if not m:
            continue
        (vars_ if m.group(1) == "var" else flags)[int(m.group(2), 0)] = int(m.group(3))
    return vars_, flags


def load_named(path: Path) -> tuple[dict[str, int], dict[str, int]]:
    """(var name -> value, flag name -> 0|1) out of mmo/STORYEND."""
    vars_: dict[str, int] = {}
    flags: dict[str, int] = {}
    if not path.is_file():
        return vars_, flags
    for line in path.read_text().splitlines():
        m = re.match(r"^(var|flag)\s+(\w+)=0x[0-9A-Fa-f]+\s+(\d+)", line)
        if not m:
            continue
        (vars_ if m.group(1) == "var" else flags)[m.group(2)] = int(m.group(3))
    return vars_, flags


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
