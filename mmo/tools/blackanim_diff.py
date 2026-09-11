#!/usr/bin/env python3
"""Hold the plugin's live compositor to the tool's own compose()."""
import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import porticons  # noqa: E402
import portsprites as ps  # noqa: E402


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--rom", required=True)
    ap.add_argument("--harness", required=True)
    ap.add_argument("--engine")
    ap.add_argument("--package", help="the fill (default mmo/mods/imports)")
    ap.add_argument("faces", nargs="+", help="SPECIES:front or SPECIES:back")
    args = ap.parse_args(argv)
    engine = porticons.engine_dir(args.engine)
    modport = porticons.load_modport(engine)
    black = modport.NitroRom(Path(args.rom)).narc_members(ps.SPRITE_NARC_BLACK)
    pkg = args.package or str(ps.MMO / "mods" / "imports")
    bad = 0
    for spec in args.faces:
        sp, face = spec.split(":")
        sp = int(sp)
        half = ps.FRONT if face == "front" else ps.BACK
        character = ps.ENGINE_ID.get(sp, sp) * ps.POKEGRA_STRIDE + (3 if face == "front" else 1)
        blk = black[ps.BLOCK * sp:ps.BLOCK * sp + ps.BLOCK]
        charmap = ps.read_charmap(blk[half + ps.CHARMAP])
        cells = ps.read_cells(blk[half + ps.CELLS])
        ca = ps.read_animations(blk[half + ps.CELL_ANIM])
        mc = ps.read_multicells(blk[half + ps.MULTICELLS])
        mca = ps.read_animations(blk[half + ps.MULTICELL_ANIM])
        ticks, period = ps.keyframe_ticks(mc, ca, mca)
        probe = sorted(set([0] + ticks[::max(1, len(ticks) // 12)] + [period - 1]))
        run = subprocess.run([args.harness, pkg, str(character)] + [str(t) for t in probe],
                             capture_output=True, text=True)
        lines = run.stdout.splitlines()
        if run.returncode != 0 or not lines or not lines[0].startswith("period "):
            print("%s %s: the harness answered nothing (%s)" % (sp, face, (run.stdout + run.stderr).strip()[:80]))
            bad += 1
            continue
        head = lines[0].split()
        cperiod, cmarks = int(head[1]), int(head[3])
        got, cur = {}, None
        for line in lines[1:]:
            if line.startswith("tick "):
                cur = int(line.split()[1])
                got[cur] = {}
            else:
                x, y, v = map(int, line.split())
                got[cur][(x, y)] = v
        differ = pixels = 0
        for t in probe:
            want = ps.compose(charmap, cells, mc, ca, mca, t).px
            have = got.get(t, {})
            differ += sum(1 for k in set(want) | set(have) if want.get(k) != have.get(k))
            pixels += len(want)
        print("%d %s: period %d/%d keyframes %d/%d, %d of %d pixels differ over %d ticks"
              % (sp, face, period, cperiod, len(ticks), cmarks, differ, pixels, len(probe)))
        if differ or period != cperiod or len(ticks) != cmarks:
            bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
