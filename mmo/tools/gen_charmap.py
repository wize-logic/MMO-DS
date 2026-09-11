#!/usr/bin/env python3
"""Regenerate src/charcode_glyphs.gen.h from the engine's own character map."""

import argparse
import os
import re
import sys

BAND_LO = 0x0100
BAND_HI = 0x0208

# Covered by the contiguous ranges in charcode.c: '0'-'9', 'A'-'Z', 'a'-'z' at
# 0x0121, 0x012B and 0x0145, and the half-width space.
COVERED = set(range(0x0121, 0x015F)) | {0x01DE}

HEADER = """\
/* Generated; do not edit. */
"""


def read_charmap(path):
    rows = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.split("//", 1)[0].rstrip("\n")
            if "=" not in line:
                continue
            key, value = line.split("=", 1)
            key = key.strip()
            if not re.fullmatch(r"[0-9A-Fa-f]{4}", key):
                continue
            rows[int(key, 16)] = value
    return rows


def emit(rows):
    out = [HEADER]
    for code in sorted(rows):
        if not (BAND_LO <= code <= BAND_HI) or code in COVERED:
            continue
        value = rows[code]
        # {command} rows are control codes; escape rows are more than one
        # character. Neither is a glyph a name can carry.
        if len(value) != 1:
            continue
        cp = ord(value)
        out.append("    { 0x%04X, 0x%04X }, /* %s */\n" % (cp, code, value))
    return "".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", required=True)
    ap.add_argument("--out")
    args = ap.parse_args()

    path = os.path.join(args.engine, "tools", "msgenc", "charmap.txt")
    if not os.path.isfile(path):
        sys.exit("gen_charmap.py: no character map at %s" % path)

    text = emit(read_charmap(path))
    if args.out:
        with open(args.out, "w", encoding="utf-8") as f:
            f.write(text)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
