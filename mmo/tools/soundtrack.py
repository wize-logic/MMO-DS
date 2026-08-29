#!/usr/bin/env python3
"""Compose a whole soundtrack out of another cartridge, one track at a time."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import portmusic as pm

# The official sound heap took 70,972 bytes of closure and refused 89,800,
# which is no room at all for Black's re-
# recorded samples, its wild battle theme alone is 190,316 bytes.
CLOSURE_CAP = 240000

# The archive path inside each cartridge, and the codes a slot accepts. A twin
# is accepted because the compose is gated afterwards by the port's audio lab,
# which measures the result rather than believing the label.
SDAT_IN_ROM = {
    "CPUE": "data/sound/pl_sound_data.sdat",
    "IPKE": "data/sound/gs_sound_data.sdat",
    "IPGE": "data/sound/gs_sound_data.sdat",
    "IRBO": "wb_sound_data.sdat",
    "IRAO": "wb_sound_data.sdat",
}
SLOT_CODES = {
    "heartgold": ("IPKE", "IPGE"),
    "blackwhite": ("IRBO", "IRAO"),
}
BASE_CODES = ("CPUE",)

PLAYER_FIELD = 1
PLAYER_BGM = 7


def die(msg: str) -> None:
    print("soundtrack: " + msg, file=sys.stderr)
    raise SystemExit(2)


def load_sdat(spec: Path, engine: str, codes: tuple[str, ...]):
    """A .sdat as itself; an image or an extracted image through NitroRom."""
    if spec.is_dir():
        hits = sorted(spec.glob("**/*sound_data.sdat"))
        if not hits:
            die("%s holds no *sound_data.sdat" % spec)
        return pm.Sdat(hits[0])
    blob = spec.read_bytes()[:4]
    if blob == b"SDAT":
        return pm.Sdat(spec)
    if not engine:
        die("%s is an image and no --engine (or $ENGINE) names the porter "
            "that reads one" % spec)
    sys.path.insert(0, str(Path(engine).expanduser() / "pc"))
    import modport
    rom = modport.NitroRom(spec)
    if rom.code not in codes:
        die("%s is %s (%r); this slot takes %s" %
            (spec, rom.code, rom.title, " or ".join(codes)))
    if rom.code not in SDAT_IN_ROM:
        die("no known archive path for %s" % rom.code)
    import tempfile
    tmp = Path(tempfile.mkstemp(suffix=".sdat")[1])
    tmp.write_bytes(rom.file_bytes(SDAT_IN_ROM[rom.code]))
    return pm.Sdat(tmp)


def read_map(path: Path, slot: str) -> list[tuple[str, str]]:
    rows, seen = [], set()
    for ln, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        pt = parts[0]
        if pt in seen:
            die("%s:%d: %s is mapped twice" % (path, ln, pt))
        seen.add(pt)
        cell = None
        for p in parts[1:]:
            if "=" not in p:
                die("%s:%d: '%s' is not slot=SEQ_NAME" % (path, ln, p))
            k, v = p.split("=", 1)
            if k not in SLOT_CODES:
                die("%s:%d: '%s' is not a slot this tool knows" % (path, ln, k))
            if k == slot:
                cell = v
        if cell is not None:
            rows.append((pt, cell))
    return rows


def refont_appended(base, foreign, ported, track_slot, fonts_path):
    """Re-voice the carried track banks with the destination's own font."""
    import soundfont as sf

    # Black's banks name their instruments per song, not by the ancestral
    # numbering, the identity chain here re-voiced carried Black tracks
    # into the wrong instruments, the same dead premise the font column was
    # rebuilt off. Until an acoustic chain earns its place, carried Black
    # tracks keep their own voices under a pinned font; Heart Gold's chain
    # is the fonts table read backwards and stands.
    if track_slot == "blackwhite":
        print("soundtrack: refont skipped, Black banks are per-song and "
              "carry their own voices")
        return
    ident = False
    chain = {}
    if not ident:
        if fonts_path is None or not fonts_path.exists():
            die("--refont on a heartgold track list needs --fonts=SOUNDFONTS")
        for ln in fonts_path.read_text().splitlines():
            s = ln.split("#", 1)[0].strip()
            if not s:
                continue
            parts = s.split()
            for c in parts[1:]:
                k, v = c.split("=", 1)
                if k == "heartgold" and v != "keep":
                    chain.setdefault(int(v.split("@", 1)[0]), int(parts[0]))

    pt_banks = [b for b in sf.PT_BANKS
                if b < len(base.info["BANK"]) and base.info["BANK"][b] is not None]
    dst_master = {}
    for b in pt_banks:
        for (i, kind, meta, regs) in sf.typed_instruments(base, b):
            dst_master.setdefault(i, (b, kind, meta, regs))
    dfont = sf.Font(base, pt_banks)

    arc700 = pm.swar_samples(base.fat[base.wavearc_file(700)])
    a700_ix = {}
    for i, sm in enumerate(arc700):
        a700_ix.setdefault(bytes(sm), i)

    notes_by_bank = {}
    banks = set()
    for fr_i, (fid, bnk) in ported.items():
        if bnk < 0:
            continue
        banks.add(bnk)
        progs, pairs, _ = pm.sseq_scan(foreign.fat[foreign.seq_file(fr_i)])
        acc = notes_by_bank.setdefault(bnk, set())
        for pr, nt in pairs:
            acc.add((pr, nt))

    voiced = kept = 0
    for bnk in sorted(banks):
        progs = {i: (k, m, regs)
                 for (i, k, m, regs) in sf.typed_instruments(base, bnk)}
        arcs = list(base.bank_swar(bnk))
        own_slot = max(k for k, a in enumerate(arcs) if a != pm.NO_ARCHIVE)
        own_arc = arcs[own_slot]
        own_samples = [bytes(x)
                       for x in pm.swar_samples(base.fat[base.wavearc_file(own_arc)])]
        own_ix = {}
        for i, sm in enumerate(own_samples):
            own_ix.setdefault(sm, i)
        slot700 = next((k for k, a in enumerate(arcs) if a == 700), None)
        if slot700 is None:
            free = next((k for k, a in enumerate(arcs) if a == pm.NO_ARCHIVE), None)
            if free is not None:
                arcs[free] = 700
                rec = bytearray(base.info["BANK"][bnk])
                struct.pack_into("<4H", rec, 4, *arcs)
                base.info["BANK"][bnk] = bytes(rec)
                slot700 = free

        rebuilt = {}
        changed = False
        for q, (k, m, regs) in progs.items():
            p = q if ident else chain.get(q)
            donor = dst_master.get(p) if p is not None else None
            newprog = None
            if donor is not None:
                db, dk, dm, dregs = donor
                covered = True
                if dk == sf.REC_DRUMS:
                    lo, hi = dm
                    for (pr, nt) in notes_by_bank.get(bnk, ()):
                        if pr == q and not lo <= nt <= hi:
                            covered = False
                            break
                if covered:
                    nregs = []
                    for kk, (rk, d) in enumerate(dregs):
                        d2 = bytearray(d)
                        if rk in sf.REC_PCM and regs:
                            # the carried track's own performance stays and
                            # the font lends only the voice and its root
                            # note, the same split the font pass makes: the
                            # sequence was written for these envelopes
                            src = bytes(regs[min(kk, len(regs) - 1)][1])
                            d2[5:10] = src[5:10]
                        if rk in sf.REC_PCM:
                            w = dfont.wave(db, d)
                            if w is None:
                                nregs = None
                                break
                            if w in a700_ix and slot700 is not None:
                                struct.pack_into("<HH", d2, 0,
                                                 a700_ix[w], slot700)
                            else:
                                if w not in own_ix:
                                    own_ix[w] = len(own_samples)
                                    own_samples.append(w)
                                struct.pack_into("<HH", d2, 0,
                                                 own_ix[w], own_slot)
                        nregs.append((rk, bytes(d2)))
                    if nregs is not None:
                        newprog = (dk, dm, nregs)
            if newprog is not None:
                rebuilt[q] = newprog
                voiced += 1
                changed = True
            else:
                rebuilt[q] = (k, m, [(rk, bytes(d)) for (rk, d) in regs])
                kept += 1
        if changed:
            base.fat[base.bank_file(bnk)] = sf.build_sbnk(rebuilt)
            base.fat[base.wavearc_file(own_arc)] = pm.build_swar(own_samples)
    print("soundtrack: refont voiced %d program(s), kept %d, "
          "across %d carried bank(s)" % (voiced, kept, len(banks)))


def main(argv):
    args = []
    engine = ""
    refont = False
    fonts_path = None
    for a in argv[1:]:
        if a.startswith("--engine="):
            engine = a.split("=", 1)[1]
        elif a == "--refont":
            refont = True
        elif a.startswith("--fonts="):
            fonts_path = Path(a.split("=", 1)[1])
        else:
            args.append(a)
    if not engine:
        import os
        engine = os.environ.get("ENGINE", "")
    if len(args) != 5:
        die("usage: soundtrack.py [--engine=DIR] [--refont --fonts=TABLE] "
            "<base> <foreign> <map> <platinum|heartgold|blackwhite> <out.sdat>")
    slot = args[3]
    if slot not in SLOT_CODES and slot != "platinum":
        die("'%s' is not a slot; platinum, heartgold or blackwhite" % slot)

    base = load_sdat(Path(args[0]), engine, BASE_CODES)
    out_path = Path(args[4])
    if slot == "platinum":
        # Keep every track: the archive passes through whole (a font pass may
        # already have rewritten its banks), and the manifests still say what
        # the lab should play.
        foreign, rows = None, []
    else:
        foreign = load_sdat(Path(args[1]), engine, SLOT_CODES[slot])
        rows = read_map(Path(args[2]), slot)
        if not rows:
            die("the map has no %s column at all" % slot)

    ported: dict[int, tuple[int, int]] = {}   # foreign seq -> (file, bank)
    replaced, skipped, total_carried = 0, [], 0
    replaced_ids: list[int] = []
    player_need: dict[int, int] = {}          # player -> biggest foreign SSEQ

    # Every musical sequence of the base archive, for the lab gate: the ones
    # this compose replaces play through their own carried banks, the rest
    # play through the music banks a font pass may have rewritten. AIF_* and
    # BGM_END are excluded because they are silent on the stock archive too.
    music_ids = []
    for i, rec in enumerate(base.info["SEQ"]):
        if rec is None:
            continue
        bank = struct.unpack_from("<H", rec, 4)[0]
        name = base.names["SEQ"][i] or ""
        if not (700 <= bank <= 705) or not name:
            continue
        if name.startswith(("SEQ_SE", "SEQ_AIF", "SEQ_BGM_END")) \
                or "DUMMY" in name or "SILENCE" in name:
            continue
        music_ids.append(i)
    for pt_name, fr_name in rows:
        if pt_name not in base.names["SEQ"]:
            die("%s is not a sequence the base archive names" % pt_name)
        if fr_name not in foreign.names["SEQ"]:
            near = [n for n in foreign.names["SEQ"]
                    if n and fr_name.split("_")[-1] in n][:5]
            die("%s is not a sequence the %s archive names%s"
                % (fr_name, slot,
                   ("; near: " + ", ".join(near)) if near else ""))
        pt_i = base.names["SEQ"].index(pt_name)
        fr_i = foreign.names["SEQ"].index(fr_name)
        srec = bytearray(base.info["SEQ"][pt_i])
        if srec[9] not in (PLAYER_FIELD, PLAYER_BGM):
            die("%s plays on player %d; only field (1) and BGM (7) tracks "
                "load their own bank per start, so only those switch"
                % (pt_name, srec[9]))

        if fr_i not in ported:
            before = len(base.fat)
            new_bank, st = pm.append_closure(foreign, fr_i, base)
            size = st["bank_bytes"] + st["carried_bytes"]
            if size > CLOSURE_CAP:
                # Unwind nothing: the appended records stay (unreferenced,
                # harmless) only if we pointed at them, so drop them instead.
                del base.fat[before:]
                del base.info["BANK"][-1:]
                del base.names["BANK"][-1:]
                del base.info["WAVEARC"][-1:]
                del base.names["WAVEARC"][-1:]
                ported[fr_i] = (-1, -1)
                skipped.append((pt_name, fr_name, size))
                continue
            base.fat.append(foreign.fat[foreign.seq_file(fr_i)])
            ported[fr_i] = (len(base.fat) - 1, new_bank)
            total_carried += size
            print("soundtrack: %-22s <- %-28s %2d/%3d instruments, "
                  "%d bytes" % (pt_name, fr_name, st["progs_n"],
                                st["inst_n"], size))
        else:
            if ported[fr_i] == (-1, -1):
                skipped.append((pt_name, fr_name, 0))
                continue
            print("soundtrack: %-22s <- %-28s (shared)" % (pt_name, fr_name))

        seq_file, bank_no = ported[fr_i]
        struct.pack_into("<H", srec, 0, seq_file)
        struct.pack_into("<H", srec, 4, bank_no)
        srec[6] = foreign.info["SEQ"][fr_i][6]
        base.info["SEQ"][pt_i] = bytes(srec)
        need = len(foreign.fat[foreign.seq_file(fr_i)])
        if need > player_need.get(srec[9], 0):
            player_need[srec[9]] = need
        replaced_ids.append(pt_i)
        replaced += 1

    for pt_name, fr_name, size in skipped:
        print("soundtrack: %-22s stays Platinum, %s is %d bytes of closure "
              "and the sound heap has refused less" % (pt_name, fr_name, size))

    # A player with a heap of its own copies each sequence into it before it
    # starts (heap 0 means the main heap serves it), and Platinum sized those
    # buffers for its own longest tracks. A foreign sequence past the buffer
    # Does not start, measured through the audio lab: Black's Driftveil is
    # 17,420 bytes of SSEQ against the field player's 15,500. The archive owns
    # the PLAYER records, so the buffer grows to this soundtrack's own longest
    # sequence, plus slack for the player's bookkeeping.
    for ply, need in sorted(player_need.items()):
        prec = bytearray(base.info["PLAYER"][ply])
        cur = struct.unpack_from("<I", prec, 4)[0]
        if cur == 0 or need + 256 <= cur:
            continue
        grown = need + 1024
        if grown > 0x10000:
            die("player %d would need a %d-byte sequence buffer; that is not "
                "a track, that is a mistake" % (ply, grown))
        struct.pack_into("<I", prec, 4, grown)
        base.info["PLAYER"][ply] = bytes(prec)
        print("soundtrack: player %d sequence buffer %d -> %d for the longest "
              "carried track" % (ply, cur, grown))

    if refont and ported:
        refont_appended(base, foreign, ported, slot, fonts_path)

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(base.build())
    # The manifest: which sequence ids answer with foreign music now.
    # tests/soundtrack_test.sh plays exactly these through the port's audio
    # lab, so the list has to come from what was actually composed rather
    # than from re-reading the map. It lives at the package root, not under
    # replace/, because everything under replace/ becomes a claim.
    manifest = out_path
    for part in out_path.parents:
        if part.name == "replace":
            manifest = part.parent / "replaced.txt"
            break
    else:
        manifest = out_path.with_suffix(".replaced")
    manifest.write_text("".join("%d\n" % i for i in sorted(replaced_ids)))
    (manifest.parent / "music.txt").write_text(
        "".join("%d\n" % i for i in music_ids))
    print("soundtrack: %d of %d rows replaced, %d closures carried "
          "(%d bytes), %d skipped; %s is %d bytes"
          % (replaced, len(rows), len([v for v in ported.values()
                                       if v != (-1, -1)]),
             total_carried, len(skipped), out_path.name,
             out_path.stat().st_size))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
