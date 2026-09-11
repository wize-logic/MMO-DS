#!/usr/bin/env python3
"""How many places in a source file could one of our hunks have landed on?"""

import sys


def hunks(path):
    out, cur = [], None
    for line in open(path, encoding="utf-8", errors="replace").read().splitlines():
        if line.startswith("@@"):
            cur = {"old": [], "new": []}
            out.append(cur)
        elif cur is None or line.startswith("\\"):
            continue
        elif line[:1] == " ":
            cur["old"].append(line[1:])
            cur["new"].append(line[1:])
        elif line[:1] == "-":
            cur["old"].append(line[1:])
        elif line[:1] == "+":
            cur["new"].append(line[1:])
        elif line == "":
            # A context line whose single leading space an editor has eaten.
            cur["old"].append("")
            cur["new"].append("")
    return out


def main(argv):
    if len(argv) != 3:
        print("usage: patch_sites.py PATCH SOURCE", file=sys.stderr)
        return 2
    patch, source = argv[1], argv[2]
    lines = open(source, encoding="utf-8", errors="replace").read().splitlines()

    groups = {}
    for i, h in enumerate(hunks(patch), 1):
        groups.setdefault(("\n".join(h["old"]), "\n".join(h["new"])), []).append((i, h))

    bad = 0
    for (old_text, _), members in groups.items():
        old = members[0][1]["old"]
        n = len(old)
        if n == 0:
            continue
        sites = [k + 1 for k in range(len(lines) - n + 1) if lines[k:k + n] == old]
        which = ",".join(str(i) for i, _ in members)
        if not sites:
            print("    hunk %s: context matches nowhere, so it lands on an earlier hunk's result"
                  % which)
        elif len(sites) > len(members):
            bad += 1
            print("    hunk %s: %d lines of context, and %d sites in the file: %s"
                  % (which, n, len(sites), " ".join(str(s) for s in sites[:8])))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
