#!/usr/bin/env python3
"""Generate, per HeartGold map, which of its people the default scene hides."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

MMO = Path(__file__).resolve().parent.parent

# The whole vocabulary. A map whose arrival script issues anything else is
# recorded as unevaluated rather than half-evaluated.
BRANCHES = {
    "GoToIfEq": lambda c: c == 0,
    "GoToIfNe": lambda c: c != 0,
    "GoToIfGt": lambda c: c > 0,
    "GoToIfGe": lambda c: c >= 0,
    "GoToIfLt": lambda c: c < 0,
    "GoToIfLe": lambda c: c <= 0,
}
STOPS = ("End", "Return")
MAX_STEPS = 4096


class Unsupported(Exception):
    pass


def die(msg: str) -> None:
    print("gen_mapscenes: " + msg, file=sys.stderr)
    raise SystemExit(2)


def decomp_dir(name: str) -> Path | None:
    try:
        out = subprocess.run([str(MMO / "tools" / "decomp_dir.sh"), name],
                             capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    return Path(out.stdout.strip())


def parse_asm(text: str) -> tuple[list[tuple[str, list[str]]], dict[str, int]]:
    """A script source as a flat instruction list plus where each label sits."""
    ops: list[tuple[str, list[str]]] = []
    labels: dict[str, int] = {}
    for raw in text.splitlines():
        line = raw.split("//")[0].split("@")[0].strip()
        if not line or line.startswith("#") or line.startswith("."):
            continue
        m = re.match(r"^(\w+):$", line)
        if m:
            labels[m.group(1)] = len(ops)
            continue
        parts = line.split(None, 1)
        args = [a.strip() for a in parts[1].split(",")] if len(parts) > 1 else []
        ops.append((parts[0], args))
    return ops, labels


def run(ops, labels, start: int, flags: set[str]) -> None:
    """Interpret from `start` with every variable zero, mutating `flags`."""
    pc, cmp_result, steps = start, 0, 0
    while 0 <= pc < len(ops):
        steps += 1
        if steps > MAX_STEPS:
            raise Unsupported("did not stop")
        name, args = ops[pc]
        pc += 1
        if name == "SetFlag":
            flags.add(args[0])
        elif name == "ClearFlag":
            flags.discard(args[0])
        elif name == "Compare":
            # Every variable is zero, so the comparison is against the literal.
            if len(args) != 2:
                raise Unsupported(name)
            try:
                cmp_result = -int(args[1], 0)
            except ValueError:
                raise Unsupported("Compare against %s" % args[1])
        elif name in BRANCHES:
            if BRANCHES[name](cmp_result):
                if args[0] not in labels:
                    raise Unsupported("branch to %s" % args[0])
                pc = labels[args[0]]
        elif name == "GoTo":
            if args[0] not in labels:
                raise Unsupported("jump to %s" % args[0])
            pc = labels[args[0]]
        elif name in STOPS:
            return
        else:
            raise Unsupported(name)


def arrival_scripts(hdr: Path) -> list[str]:
    """The ON_TRANSITION and ON_LOAD entries, in the order the game runs them."""
    text = hdr.read_text()
    out = []
    for kind in ("OnTransition", "OnLoad"):
        m = re.search(r"InitScriptEntry_%s\s+(\S+)" % kind, text)
        if m:
            out.append(m.group(1))
    return out


def main(argv: list[str]) -> int:
    hg = Path(argv[1]) if len(argv) > 1 else decomp_dir("pokeheartgold")
    out = Path(argv[2]) if len(argv) > 2 else MMO / "MAPSCENES"
    if hg is None:
        die("need a heartgold checkout; pass one or set DECOMP_DIR")

    ids = {}
    for m in re.finditer(r"#define\s+MAP_(\w+)\s+(\d+)",
                         (hg / "include/constants/maps.h").read_text()):
        ids.setdefault(m.group(1), int(m.group(2)))
    # The flag number goes in the table beside its name. A porter reads the
    # cartridge and this file and nothing else, and the eventFlag field it has
    # to compare against is a number.
    flagno = {}
    for m in re.finditer(r"#define\s+(FLAG_\w+)\s+(0x[0-9A-Fa-f]+|\d+)",
                         (hg / "include/constants/flags.h").read_text()):
        flagno.setdefault(m.group(1), int(m.group(2), 0))
    seq = hg / "files/fielddata/script/scr_seq"
    by_stem = {p.stem: p for p in seq.glob("scr_seq_*.s")}

    def source(bank_const: str) -> Path | None:
        m = re.match(r"NARC_scr_seq_(scr_seq_\d+_\w+?)_bin$", bank_const)
        return by_stem.get(m.group(1)) if m else None

    rows, evaluated, refused = [], 0, 0
    text = (hg / "src/data/map_headers.h").read_text()
    for m in re.finditer(r"\[MAP_(\w+)\]\s*=\s*\{(.*?)\n\s*\}", text, re.S):
        name, body = m.group(1), m.group(2)
        if name not in ids:
            continue
        hdr_c = re.search(r"\.scriptHeaderBank\s*=\s*(\w+)", body)
        scr_c = re.search(r"\.scriptsBank\s*=\s*(\w+)", body)
        if not hdr_c or not scr_c:
            continue
        hdr, scr = source(hdr_c.group(1)), source(scr_c.group(1))
        if hdr is None or scr is None or not hdr.is_file() or not scr.is_file():
            rows.append((name.lower(), None, "no script source"))
            refused += 1
            continue
        entries = arrival_scripts(hdr)
        if not entries:
            rows.append((name.lower(), set(), ""))
            evaluated += 1
            continue
        ops, labels = parse_asm(scr.read_text())
        flags: set[str] = set()
        why = ""
        try:
            for ent in entries:
                # `_EV_scr_seq_T25_018 + 1` names script _018, whose body is
                # the label of the same number without the _EV_ prefix.
                sm = re.match(r"_EV_(\w+?)(?:\s*\+\s*\d+)?$", ent)
                if not sm:
                    raise Unsupported("entry %s" % ent)
                if sm.group(1) not in labels:
                    raise Unsupported("no body for %s" % sm.group(1))
                run(ops, labels, labels[sm.group(1)], flags)
        except Unsupported as exc:
            rows.append((name.lower(), None, str(exc)))
            refused += 1
            continue
        unknown = sorted(f for f in flags if f not in flagno)
        if unknown:
            rows.append((name.lower(), None, "no number for %s" % unknown[0]))
            refused += 1
            continue
        rows.append((name.lower(), flags, ""))
        evaluated += 1

    rows.sort(key=lambda r: r[0])
    lines = [
        "# MAPSCENES, GENERATED by tools/gen_mapscenes.py; DO NOT EDIT.",
        "#",
        "# Which flags a HeartGold map's own arrival script leaves SET when it",
        "# runs with every story variable at zero, and therefore which of the",
        "# map's people are HIDDEN in the scene a port carries.",
        "#",
        "# An object event is created when its eventFlag is CLEAR, in both",
        "# games. The numbers cannot cross (they are HeartGold's), so a port",
        "# drops the object instead: tools/portmap.py carries an object whose",
        "# flag is FLAG_NOTHING or is not named on this map's row, and leaves",
        "# behind the ones a flag here hides.",
        "#",
        "# `?` is a map whose arrival script reaches outside the eight commands",
        "# this evaluates. It is not a guess and not a translation, see the",
        "# refusal in mmo/MAPFORMATS.md, so a port of such a map carries only",
        "# the people no flag hides at all, and says so.",
        "#",
        "# %d maps evaluated, %d written down as unevaluated."
        % (evaluated, refused),
        "#",
        "# Rows: hg <name> <NAME=value ... of the flags left set, or ? and why>",
        "",
    ]
    for name, flags, why in rows:
        if flags is None:
            lines.append("hg  %-38s ?  # %s" % (name, why))
        elif not flags:
            lines.append("hg  %-38s -" % name)
        else:
            lines.append("hg  %-38s %s"
                         % (name, " ".join("%s=0x%X" % (f, flagno[f])
                                           for f in sorted(flags))))
    lines.append("")
    out.write_text("\n".join(lines))
    print("gen_mapscenes: %d maps evaluated, %d unevaluated -> %s"
          % (evaluated, refused, out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
