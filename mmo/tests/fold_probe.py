#!/usr/bin/env python3
"""Fold one script built here, so the claim under a ported one is checked."""

import struct
import sys

sys.path.insert(0, sys.argv[1] + "/tools")
import portscript
from pathlib import Path
tab = portscript.load_table(Path(sys.argv[1]) / "SCRCMD")
by = {c.hg_name: c for c in tab.values()}
# One entry: compare a story variable with 1, jump when equal, and two
# messages, the shape every talking person in Johto has.
tail_a = struct.pack("<HB", by["NPCMsg"].hg, 4) + struct.pack("<H", by["End"].hg)
tail_b = struct.pack("<HB", by["NPCMsg"].hg, 9) + struct.pack("<H", by["End"].hg)
code = (struct.pack("<HHH", by["CompareVarToValue"].hg, 0x4000, 1)
        + struct.pack("<HBi", by["GoToIf"].hg, 1, len(tail_a))
        + tail_a + tail_b)
member = struct.pack("<i", 2) + struct.pack("<H", 0xFD13) + code
out, kept, _open = portscript.convert(member, tab, {})
p = 4 + 2
seen = []
while p < len(out):
    op = struct.unpack_from("<H", out, p)[0]; p += 2
    if op == by["End"].pl: break
    if op == by["NPCMsg"].pl: seen.append(out[p]); p += 1
verdict = "%s %s" % ("folded" if kept[0] else "refused", seen)

am = by["ApplyMovement"]
end_rec = struct.pack("<HH", portscript.MOVEMENT_END, 0)


def fold_block(block: bytes):
    # ApplyMovement's data pointer is relative to the word after the command;
    # the block sits right behind the entry's End.
    code = (struct.pack("<HHi", am.hg, 1, 2) + struct.pack("<H", by["End"].hg)
            + block)
    member = struct.pack("<i", 2) + struct.pack("<H", 0xFD13) + code
    out, kept, _open = portscript.convert(member, tab, {})
    return out, kept[0]


faults = []
same_blk = struct.pack("<HH", 12, 2) + end_rec
out, ok = fold_block(same_blk)
if not (ok and out.find(same_blk) >= 0):
    faults.append("identity")
leap = struct.pack("<HH", 105, 1) + end_rec
want = b"".join(struct.pack("<HH", a, c)
                for a, c in portscript.MOVE_LOWER[105]) + end_rec
out, ok = fold_block(leap)
if not (ok and out.find(want) >= 0 and out.find(leap) < 0):
    faults.append("lowered")
out, ok = fold_block(struct.pack("<HH", portscript.MOVE_MAX_HG, 1) + end_rec)
if ok:
    faults.append("refused")
covered = set(portscript._MOVES) | set(portscript.MOVE_LOWER)
gap = sorted(set(range(portscript.MOVE_MAX_HG)) - covered)
if gap or any(a >= portscript.MOVE_MAX_HG for a in portscript._MOVES) \
        or any(b >= portscript.MOVE_MAX_PL for b in portscript._MOVES.values()) \
        or any(a >= portscript.MOVE_MAX_PL
               for rows in portscript.MOVE_LOWER.values() for a, _ in rows):
    faults.append("coverage %s" % gap)
print("%s moves %s" % (verdict, "ok" if not faults else "BAD " + ",".join(faults)))
