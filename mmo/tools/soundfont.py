#!/usr/bin/env python3
"""Port another cartridge's instruments under Platinum's own music."""

from __future__ import annotations

import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import portmusic as pm
import soundtrack as st

REC_PCM = (1, 4, 5)
REC_DRUMS = 16
REC_SPLIT = 17
PT_BANKS = tuple(range(700, 706))

# Shared-archive budget: the shared archive is resident for the whole session
# the way the official client's 458,564-byte one is, and this compose only ever appends to
# it (the SE banks index it too).
SHARED_ARC_BUDGET = 1_450_000


def die(msg: str) -> None:
    print("soundfont: " + msg, file=sys.stderr)
    raise SystemExit(2)


# --------------------------------------------------------------- extraction
def typed_instruments(sdat, b):
    """[(prog, kind, meta, [(region_kind, def10), ...])] for one bank."""
    rec = sdat.info["BANK"][b]
    if rec is None:
        return []
    blob = sdat.fat[sdat.bank_file(b)]
    n = struct.unpack_from("<I", blob, pm.SBNK_COUNT)[0]
    out = []
    for i in range(n):
        kind = blob[pm.SBNK_ENTRY + i * 4]
        off = struct.unpack_from("<H", blob, pm.SBNK_ENTRY + i * 4 + 1)[0]
        if kind == 0 or off == 0:
            continue
        if kind in REC_PCM + (2, 3):
            out.append((i, kind, None, [(kind, bytes(blob[off:off + 10]))]))
        elif kind == REC_DRUMS:
            lo, hi = blob[off], blob[off + 1]
            regs = []
            for k in range(hi - lo + 1):
                at = off + 2 + k * 12
                rk = struct.unpack_from("<H", blob, at)[0]
                regs.append((rk, bytes(blob[at + 2:at + 12])))
            out.append((i, kind, (lo, hi), regs))
        elif kind == REC_SPLIT:
            bounds = bytes(blob[off:off + 8])
            nr = sum(1 for x in bounds if x)
            regs = []
            for k in range(nr):
                at = off + 8 + k * 12
                rk = struct.unpack_from("<H", blob, at)[0]
                regs.append((rk, bytes(blob[at + 2:at + 12])))
            out.append((i, kind, bounds, regs))
    return out


class Font:
    """One game's music banks as a master program list with variants."""

    def __init__(self, sdat, banks):
        self.sdat = sdat
        self.banks = list(banks)
        self.arc_cache = {}
        # p -> Counter(sig) and (p, sig) -> representative program
        self.variants = defaultdict(Counter)
        self.rep = {}
        for b in self.banks:
            for (i, kind, meta, regs) in typed_instruments(sdat, b):
                sg = self.prog_sig(b, kind, meta, regs)
                self.variants[i][sg] += 1
                self.rep.setdefault((i, sg), (b, kind, meta, regs))

    def samples(self, arc):
        if arc not in self.arc_cache:
            self.arc_cache[arc] = pm.swar_samples(
                self.sdat.fat[self.sdat.wavearc_file(arc)])
        return self.arc_cache[arc]

    def wave(self, b, def10):
        idx, slot = struct.unpack_from("<HH", def10, 0)
        arcs = self.sdat.bank_swar(b)
        if slot >= len(arcs) or arcs[slot] == pm.NO_ARCHIVE:
            return None
        pool = self.samples(arcs[slot])
        return bytes(pool[idx]) if idx < len(pool) else None

    def prog_sig(self, b, kind, meta, regs):
        out = [kind, meta]
        for (rk, d) in regs:
            if rk in REC_PCM:
                out.append((rk, self.wave(b, d), bytes(d)[4:10]))
            else:
                out.append((rk, struct.unpack_from("<H", d, 0)[0],
                            bytes(d)[4:10]))
        return tuple(out)

    def canonical(self, p):
        """The most bank-supported variant of program p, or None."""
        if p not in self.variants:
            return None
        sg, _ = self.variants[p].most_common(1)[0]
        return self.rep[(p, sg)]

    def variant_of(self, p, bank):
        for (pp, sg), (b, kind, meta, regs) in self.rep.items():
            if pp == p and b == bank:
                return (b, kind, meta, regs)
        return None

    def waves_of(self, prog):
        b, kind, meta, regs = prog
        return [self.wave(b, d) if rk in REC_PCM else None
                for (rk, d) in regs]


def load(spec, engine, codes):
    return st.load_sdat(Path(spec), engine, codes)


def pt_font(sdat):
    return Font(sdat, [b for b in PT_BANKS if b < len(sdat.info["BANK"])
                       and sdat.info["BANK"][b] is not None])


def foreign_font(sdat, slot):
    names = sdat.names["BANK"]
    banks = []
    for b in range(len(sdat.info["BANK"])):
        n = (names[b] if b < len(names) else "") or ""
        if slot == "heartgold":
            if 700 <= b < 750 and n.startswith("BANK_") \
                    and "_SE" not in n and "GAMEBOY" not in n:
                banks.append(b)
        else:
            if n.startswith("BANK_MUS_"):
                banks.append(b)
    if not banks:
        die("no %s music banks found; is this the right cartridge?" % slot)
    return Font(sdat, banks)


def played_programs(sdat):
    """Master programs Platinum's music actually selects, and per-program
    note sets (for drum-kit coverage checks)."""
    used = set()
    notes = defaultdict(set)
    for i, rec in enumerate(sdat.info["SEQ"]):
        if rec is None:
            continue
        bank = struct.unpack_from("<H", rec, 4)[0]
        if bank not in PT_BANKS:
            continue
        name = sdat.names["SEQ"][i] or ""
        if "DUMMY" in name:
            continue
        progs, pairs, _ = pm.sseq_scan(sdat.fat[sdat.seq_file(i)])
        used |= progs
        for (p, note) in pairs:
            notes[p].add(note)
    return used, notes


# --------------------------------------------------------------- matching
def wave_fp(blob):
    """Fingerprint + spectral profile, or None for the undecodable."""
    import numpy as np
    if blob is None or len(blob) < 28:
        return None
    typ, loopflag, rate = struct.unpack_from("<BBH", blob, 0)
    data = blob[12:]
    if typ == 0:
        pcm = np.frombuffer(data, dtype=np.int8).astype(np.float32)
    elif typ == 1:
        pcm = np.frombuffer(data[:len(data) & ~1], dtype="<i2").astype(np.float32)
    elif typ == 2:
        if len(data) < 8:
            return None
        val = struct.unpack_from("<h", data, 0)[0]
        idx = min(88, max(0, struct.unpack_from("<H", data, 2)[0]))
        step_t = wave_fp.STEP
        index_t = wave_fp.INDEX
        nyb = np.frombuffer(data[4:], dtype=np.uint8)
        out = np.empty(len(nyb) * 2, dtype=np.float32)
        k = 0
        for byte in nyb:
            for shift in (0, 4):
                dd = (byte >> shift) & 0xF
                step = step_t[idx]
                diff = step >> 3
                if dd & 1:
                    diff += step >> 2
                if dd & 2:
                    diff += step >> 1
                if dd & 4:
                    diff += step
                val = val - diff if dd & 8 else val + diff
                val = max(-32768, min(32767, val))
                idx = max(0, min(88, idx + index_t[dd & 7]))
                out[k] = val
                k += 1
        pcm = out
    else:
        return None
    if rate < 1000 or len(pcm) < 64:
        return None
    x = pcm / (np.max(np.abs(pcm)) + 1e-6)
    n = int(min(len(x), 0.5 * rate))
    t = np.arange(0, n, rate / 16000.0)
    fp = np.interp(t, np.arange(n), x[:n])[:8000]
    fp = fp - fp.mean()
    fp /= (np.linalg.norm(fp) + 1e-9)
    return dict(F=np.fft.rfft(fp, 16384), loop=loopflag, dur=len(x) / rate,
                pcm16=np.clip(pcm * (32767 / (np.abs(pcm).max() + 1e-6)),
                              -32768, 32767).astype("<i2"), rate=rate)


wave_fp.STEP = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
    209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724,
    796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132,
    7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767]
wave_fp.INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]


def wave_rms(blob):
    """The wave's own loudness, unnormalised."""
    import numpy as np

    if blob is None or len(blob) < 16:
        return None
    typ = blob[0]
    data = blob[12:]
    if typ == 0:
        pcm = np.frombuffer(data, dtype=np.int8).astype(np.float64) * 256.0
    elif typ == 1:
        pcm = np.frombuffer(data[:len(data) & ~1], dtype="<i2").astype(np.float64)
    elif typ == 2:
        f = wave_fp(blob)          # decodes; its pcm16 is peak-normalised
        if f is None:
            return None
        # re-derive the true level: decode again through the same table
        val = struct.unpack_from("<h", data, 0)[0]
        idx = min(88, max(0, struct.unpack_from("<H", data, 2)[0]))
        out = []
        for byte in data[4:]:
            for shift in (0, 4):
                d = (byte >> shift) & 0xF
                step = wave_fp.STEP[idx]
                diff = step >> 3
                if d & 1:
                    diff += step >> 2
                if d & 2:
                    diff += step >> 1
                if d & 4:
                    diff += step
                val = val - diff if d & 8 else val + diff
                val = max(-32768, min(32767, val))
                idx = max(0, min(88, idx + wave_fp.INDEX[d & 7]))
                out.append(val)
        pcm = np.asarray(out, dtype=np.float64)
    else:
        return None
    if len(pcm) < 16:
        return None
    return float(np.sqrt((pcm ** 2).mean()))


# A sustain level scales amplitude by (level/127)^2, the driver's own
# SNDi_DecibelSquareTable is exactly 40*log10(level/127) in tenths of a dB,
# checked at four points. So an instrument's place in the mix is its sample
# RMS times that square, and matching it is one multiply on the level.
SUSTAIN_FULL = 127


def sustain_scale(pt_rms, do_rms):
    """Percent to scale the (Platinum) sustain level by, so a donor whose sample is mastered
    hotter sits where the instrument it replaces sat.
    """
    if not pt_rms or not do_rms or do_rms <= pt_rms:
        return 100
    import math
    return max(1, min(100, int(round(100.0 * math.sqrt(pt_rms / do_rms)))))


def wave_art(blob):
    """How a wave STARTS and how bright it is, the two things a lag-searched correlation
    cannot see.
    """
    import numpy as np

    f = wave_fp(blob)
    if f is None:
        return None
    pcm = f["pcm16"].astype(np.float32)
    rate = f["rate"]
    x = pcm / (np.abs(pcm).max() + 1e-6)
    env = np.abs(x)
    a = int(min(len(x) - 16, np.argmax(env >= 0.8 * env.max()) + 16))
    w = x[a:a + 4096] if len(x) - a >= 512 else x
    spec = np.abs(np.fft.rfft(w * np.hanning(len(w)), 8192))
    fr = np.fft.rfftfreq(8192, 1.0 / rate)
    return {"atk": (a / rate) * 1000.0,
            "hi": float(spec[fr > 2000].sum() / (spec.sum() + 1e-9))}


# A donor has to be the same kind of sound, not merely a correlated one:
# within about three times the attack and under twice the brightness. The
# owner heard what these let through, brass answering for a soft lead in
# the Pokemon Center, and both bounds are needed: p18's donor cleared the
# attack bound and failed on brightness, p1's the reverse.
ART_ATTACK_RATIO = 3.0
ART_BRIGHT_RATIO = 1.8


def art_ok(aa, ab):
    if aa is None or ab is None:
        return False
    ra = (ab["atk"] + 5.0) / (aa["atk"] + 5.0)
    rh = (ab["hi"] + 0.03) / (aa["hi"] + 0.03)
    return (1.0 / ART_ATTACK_RATIO <= ra <= ART_ATTACK_RATIO
            and 1.0 / ART_BRIGHT_RATIO <= rh <= ART_BRIGHT_RATIO)


def rec_corr(fa, fb):
    import numpy as np
    x = np.fft.irfft(fa["F"] * np.conj(fb["F"]), 16384)
    lag = int(0.1 * 16000)
    return float(np.max(np.abs(np.concatenate([x[-lag:], x[:lag]]))))


def art(d):
    return bytes(d)[4:9]


def prog_score(ptf, pgm_a, fof, pgm_b, fps):
    """How much program B looks like A's instrument: per-PCM-region best
    recording correlation, articulation equality, kind agreement."""
    _, ka, ma, ra = pgm_a
    _, kb, mb, rb = pgm_b
    a_pcm = [(rk, d) for (rk, d) in ra if rk in REC_PCM]
    b_pcm = [(rk, d) for (rk, d) in rb if rk in REC_PCM]
    if not a_pcm or not b_pcm:
        return 0.0, 0.0
    def fp_of(w):
        if w is None:
            return None
        if w not in fps:
            fps[w] = wave_fp(w)
        return fps[w]

    tot = rec_best = 0.0
    arts = 0
    for (rk, d) in a_pcm:
        wa = ptf.wave(pgm_a[0], d)
        fa = fp_of(wa)
        best = 0.0
        for (crk, cd) in b_pcm:
            wb = fof.wave(pgm_b[0], cd)
            if wa is not None and wa == wb:
                s = 1.0
            else:
                fb = fp_of(wb)
                s = rec_corr(fa, fb) if (fa and fb) else 0.0
            if art(d) == art(cd):
                arts += 1
                s += 0.10
            best = max(best, s)
        tot += min(best, 1.0)
        rec_best = max(rec_best, best)
    score = tot / len(a_pcm)
    if ka == kb:
        score += 0.05
    return score, rec_best


def match_heartgold(ptf, fof, used, fps):
    return match_by_evidence(ptf, fof, used, fps)


def match_by_evidence(ptf, fof, used, fps):
    """{p: ((q, bank) or None, tier)} by evidence, never by position."""
    def fp_of(w):
        if w is None:
            return None
        if w not in fps:
            fps[w] = wave_fp(w)
        return fps[w]

    def art_of(w):
        key = ("art", w)
        if key not in fps:
            fps[key] = wave_art(w)
        return fps[key]

    # every (program, variant) of every donor music bank is a candidate,
    # with its PCM waves resolved once
    cands = []
    for q in fof.variants:
        for sg in fof.variants[q]:
            b, kind, meta, regs = fof.rep[(q, sg)]
            ws = [fof.wave(b, d) for (rk, d) in regs if rk in REC_PCM]
            ws = [w for w in ws if w is not None]
            if ws:
                cands.append((q, b, kind, ws))

    # wave-level best matches, memoised: platinum wave -> per-candidate-wave
    # correlations are what every program score is made of
    pair_cache = {}

    def wcorr(w, w2):
        if w == w2:
            return 1.0
        key = (w, w2)
        if key in pair_cache:
            return pair_cache[key]
        # articulation first: a wave that starts differently or sits in a
        # different brightness band is not this instrument however well its
        # sustain correlates, so it is not a candidate at all
        v = 0.0
        if art_ok(art_of(w), art_of(w2)):
            fa, fb = fp_of(w), fp_of(w2)
            v = rec_corr(fa, fb) if (fa is not None and fb is not None) else 0.0
        pair_cache[key] = v
        return v

    out = {}
    for p in sorted(used):
        prog = ptf.canonical(p)
        if prog is None:
            continue
        waves = [w for w in ptf.waves_of(prog) if w is not None]
        if not waves:
            out[p] = (None, "psg")
            continue
        pt_kit = prog[1] == 16
        best = (0.0, False, None)
        for (q, b, kind, ws) in cands:
            if (kind == 16) != pt_kit:
                continue
            # and the primary voice has to agree, not merely some region of
            # it: a split instrument can win on a secondary region while the
            # voice a melody actually sits in is a different kind of sound,
            # which is how a plucked lead kept drawing a slow swell after
            # the per-pair guard went in.
            if waves[0] != ws[0] \
                    and not art_ok(art_of(waves[0]), art_of(ws[0])):
                continue
            tot = 0.0
            allbytes = True
            for w in waves:
                r = max(wcorr(w, w2) for w2 in ws)
                if r < 1.0:
                    allbytes = False
                tot += r
            score = tot / len(waves)
            if score > best[0]:
                best = (score, allbytes, (q, b))
        if best[2] is not None and best[1]:
            out[p] = (best[2], "bytes")
        elif best[2] is not None and best[0] >= 0.45:
            out[p] = (best[2], "recording")
        else:
            out[p] = (None, "keep")
    return out


# --------------------------------------------------------------- the table
def read_table(path, slot):
    rows = {}
    for ln, raw in enumerate(path.read_text().splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        try:
            p = int(parts[0])
        except ValueError:
            die("%s:%d: '%s' is not a program number" % (path, ln, parts[0]))
        for cell in parts[1:]:
            if "=" not in cell:
                die("%s:%d: '%s' is not slot=value" % (path, ln, cell))
            k, v = cell.split("=", 1)
            if k != slot:
                continue
            rows[p] = v
    return rows


def write_table(path, slot, mapping, ptf, used, notes, force):
    existing = {}
    lines_head = [
        "# SOUNDFONTS, which instrument answers for which, when Platinum's",
        "# own music plays through another cartridge's font.",
        "#",
        "# One row per master program of Platinum's music banks (700..705,",
        "# each cell <program>@<bank> plus an optional /<percent> that scales",
        "# the donor's sustain levels so it sits where the instrument it",
        "# replaces sat, a donor mastered hotter, or one that holds where",
        "# Platinum's decayed away, is what too loud and harsh sounds like.",
        "# one layout, measured). Cells: heartgold=<program> is that slot of",
        "# HeartGold's master list; blackwhite=<program>@<bank> is that",
        "# bank's variant at the SAME number (gen 5 kept the ancestral",
        "# numbering; the bank picks which per-song trim donates). keep",
        "# means the Platinum instrument stays; PSG programs are synth",
        "# settings, identical hardware in every game, and always stay.",
        "#",
        "# Generated by tools/soundfont.py survey, then CURATED BY EAR --",
        "# regenerating needs --force on purpose. The tier comment is the",
        "# evidence: bytes (same recording, certain), recording (the two",
        "# performances line up), articulation (the designer's ADSR/keying",
        "# agrees), majority (Black's most-used trim), single/guess (weakest,",
        "# worth an ear), keep (nothing found).",
        "",
    ]
    old_notes = {}
    if path.exists():
        text = path.read_text()
        for ln in text.splitlines():
            s, _, comment = ln.partition("#")
            s = s.strip()
            if not s:
                continue
            parts = s.split()
            p = int(parts[0])
            existing[p] = {c.split("=", 1)[0]: c.split("=", 1)[1]
                           for c in parts[1:]}
            old_notes[p] = {t.split(":", 1)[0]: t.split(":", 1)[1]
                            for t in comment.split() if ":" in t}
        if any(slot in cells for cells in existing.values()) and not force:
            die("%s already has %s cells; --force regenerates them" %
                (path, slot))
    allp = sorted(set(existing) | set(mapping))
    outl = list(lines_head)
    for p in allp:
        cells = existing.get(p, {})
        tiers = old_notes.get(p, {})
        if p in mapping:
            val, tier = mapping[p]
            cells[slot] = val
            tiers[slot] = tier
        prog = ptf.canonical(p)
        kindname = {1: "note", 2: "square", 3: "noise", 4: "note", 5: "note",
                    16: "drums", 17: "split%d" % len(prog[3])
                    if prog else ""}.get(prog[1] if prog else 0, "?")
        uses = len(notes.get(p, ()))
        note = " ".join("%s:%s" % (k, v) for k, v in sorted(tiers.items()))
        cellstr = "  ".join("%s=%s" % (k, v) for k, v in sorted(cells.items()))
        outl.append("%-4d %-46s # %-7s %d note(s) %s"
                    % (p, cellstr, kindname, uses, note))
    path.write_text("\n".join(outl) + "\n")


# --------------------------------------------------------------- compose
def build_sbnk(programs):
    """An SBNK holding {prog: (kind, meta, [(rkind, def10), ...])}."""
    n = (max(programs) + 1) if programs else 0
    entries = bytearray(4 * n)
    body = bytearray()
    base = pm.SBNK_ENTRY + 4 * n

    def put(rec):
        off = base + len(body)
        if off > 0xFFFF:
            die("bank record offset past 64k; the bank is implausibly big")
        body.extend(rec)
        return off

    for p, (kind, meta, regs) in sorted(programs.items()):
        if kind in REC_PCM + (2, 3):
            off = put(regs[0][1])
        elif kind == REC_DRUMS:
            lo, hi = meta
            rec = bytearray([lo, hi])
            for (rk, d) in regs:
                rec += struct.pack("<H", rk) + d
            off = put(rec)
        elif kind == REC_SPLIT:
            rec = bytearray(meta)
            for (rk, d) in regs:
                rec += struct.pack("<H", rk) + d
            off = put(rec)
        else:
            die("program %d has record kind %d, which this does not write"
                % (p, kind))
        entries[p * 4] = kind
        struct.pack_into("<H", entries, p * 4 + 1, off)
    size = pm.SBNK_ENTRY + 4 * n + len(body)
    out = bytearray(b"SBNK")
    out += struct.pack("<IIHH", 0x0100FEFF, size, 0x10, 1)
    out += b"DATA" + struct.pack("<I", size - 0x10) + bytes(32)
    out += struct.pack("<I", n)
    out += entries
    out += body
    return bytes(out)


def compose(base, foreign, table_path, slot, out_path):
    ptf = pt_font(base)
    fof = foreign_font(foreign, slot)
    used, notes = played_programs(base)
    rows = read_table(table_path, slot)

    # Resolve each mapped program to a donor program object
    donors = {}
    for p, v in rows.items():
        if v == "keep":
            continue
        pct = 100
        if "/" in v:
            v, spct = v.split("/", 1)
            pct = int(spct)
        if "@" in v:
            q, bank = v.split("@", 1)
            prog = fof.variant_of(int(q), int(bank))
            if prog is None:
                die("%s bank %s defines no program %s" % (slot, bank, q))
        else:
            prog = fof.canonical(int(v))
            if prog is None:
                die("%s master has no program %s" % (slot, v))
        donors[p] = (prog, pct)

    # The voice is the donor'S, the performance is platinum'S.
    #
    # Every defect the owner heard was an envelope, not a timbre: brass
    # swelling into a plucked lead, a note holding where Sinnoh's decayed
    # away (+84 dB), a note dying where Sinnoh's held (sustain 1 against
    # 122). The sequences were written for Platinum's envelopes, note
    # lengths, phrasing and balance all assume them, so a donor lends
    # what it is, its sample and the root note that sample is cut at, and
    # Platinum keeps how a note behaves: attack, decay, sustain, release
    # and pan, region by region. What is left to correct after that is the
    # sample's own level, which is what the /percent on a cell carries.
    for p, (prog, pct) in list(donors.items()):
        pt_prog = ptf.canonical(p)
        if pt_prog is None:
            continue
        b2, kind2, meta2, regs2 = prog
        pt_regs = [d for (rk, d) in pt_prog[3]]
        blended = []
        for k, (rk, d) in enumerate(regs2):
            d = bytearray(d)
            if rk in REC_PCM and pt_regs:
                src = bytearray(pt_regs[min(k, len(pt_regs) - 1)])
                d[5:10] = src[5:10]          # A, D, S, R, pan
                if pct != 100 and d[7] > 0:
                    # never round an audible instrument to level 0: 0 is
                    # not quiet in this format, it is silence
                    d[7] = max(1, min(127, int(round(d[7] * pct / 100.0))))
            blended.append((rk, bytes(d)))
        donors[p] = (b2, kind2, meta2, blended)

    # Drum-kit coverage: the donor must define every note the music plays
    for p, prog in list(donors.items()):
        _, kind, meta, regs = prog
        pt_prog = ptf.canonical(p)
        if pt_prog and pt_prog[1] == REC_DRUMS and kind == REC_DRUMS:
            lo, hi = meta
            missing = [nt for nt in notes.get(p, ()) if not lo <= nt <= hi]
            if missing:
                print("soundfont: p%d stays Platinum, the donor kit "
                      "covers %d..%d and the music plays %s"
                      % (p, lo, hi, sorted(missing)))
                del donors[p]

    # New per-bank program sets
    new_banks = {}
    for b in [x for x in PT_BANKS if x < len(base.info["BANK"])
              and base.info["BANK"][x] is not None]:
        progs = {}
        for (i, kind, meta, regs) in typed_instruments(base, b):
            if i in donors:
                db, dkind, dmeta, dregs = donors[i]
                progs[i] = (dkind, dmeta,
                            [(rk, bytes(d), (fof.wave(db, d)
                                             if rk in REC_PCM else None))
                             for (rk, d) in dregs])
            else:
                progs[i] = (kind, meta,
                            [(rk, bytes(d), (ptf.wave(b, d)
                                             if rk in REC_PCM else None))
                             for (rk, d) in regs])
        new_banks[b] = progs

    # Wave placement. The shared archive (arc 700) is not rebuilt from
    # scratch: every SE bank in the game indexes it too, and those banks are
    # outside this compose on purpose, the first version reordered it and
    # every sound effect in Sinnoh came out as some other wave, caught by
    # the test's SE pin. So the original samples stay at their original
    # indices, byte for byte, and the font's waves are appended after them
    # (a donor wave that already exists in the original, HeartGold kept
    # many recordings, reuses the original index instead). Each music
    # bank's own archive has no other reader and is rebuilt freely.
    orig_shared = pm.swar_samples(base.fat[base.wavearc_file(700)])
    shared = [bytes(s) for s in orig_shared]
    shared_ix = {}
    for i, s in enumerate(shared):
        shared_ix.setdefault(s, i)

    wave_users = defaultdict(set)
    for b, progs in new_banks.items():
        for p, (kind, meta, regs) in progs.items():
            for (rk, d, w) in regs:
                if w is not None:
                    wave_users[w].add(b)
    own = {b: [] for b in new_banks}
    own_ix = {b: {} for b in new_banks}
    # Encounter order, not sorted: this loop decides the INDEX each carried
    # wave gets, and the launcher's C compose has no reason to agree with
    # a sort by wave bytes. Both walk banks 700..705, their programs and
    # their regions in the same order, so insertion order is the ordering
    # the two implementations can agree on by construction, and the
    # differential in tests/soundtrack_test.sh is what noticed they did
    # not.
    for w, bs in wave_users.items():
        if w in shared_ix:
            continue
        if len(bs) > 1 or 700 in bs:
            shared_ix[w] = len(shared)
            shared.append(w)
        else:
            (b,) = bs
            own_ix[b][w] = len(own[b])
            own[b].append(w)

    shared_blob = pm.build_swar(shared)
    if len(shared_blob) > SHARED_ARC_BUDGET:
        die("the rebuilt shared archive is %d bytes against a %d budget; "
            "raise the sound heap first" % (len(shared_blob),
                                            SHARED_ARC_BUDGET))

    # Rewrite defs onto the new archives and build the bank files
    for b, progs in new_banks.items():
        arcs = base.bank_swar(b)
        # slot of the shared archive in this bank's list (arc 700), and of
        # its own archive (arc == bank number), matching Platinum's shape
        slot_shared = arcs.index(700) if 700 in arcs else None
        slot_own = arcs.index(b) if b in arcs else None
        rebuilt = {}
        for p, (kind, meta, regs) in progs.items():
            nregs = []
            for (rk, d, w) in regs:
                d = bytearray(d)
                if rk in REC_PCM:
                    if w is None:
                        die("bank %d program %d region lost its wave" % (b, p))
                    if w in shared_ix:
                        if slot_shared is None:
                            die("bank %d has no shared-archive slot" % b)
                        struct.pack_into("<HH", d, 0, shared_ix[w], slot_shared)
                    else:
                        if slot_own is None:
                            die("bank %d has no own-archive slot" % b)
                        struct.pack_into("<HH", d, 0, own_ix[b][w], slot_own)
                nregs.append((rk, bytes(d)))
            rebuilt[p] = (kind, meta, nregs)
        base.fat[base.bank_file(b)] = build_sbnk(rebuilt)

    base.fat[base.wavearc_file(700)] = shared_blob
    for b in new_banks:
        # bank 700's own archive is the shared one written above
        if b == 700:
            continue
        if b in base.bank_swar(b):
            base.fat[base.wavearc_file(b)] = pm.build_swar(own[b])

    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(base.build())
    swapped = len(donors)
    kept = len([p for p in used if p in rows and rows[p] == "keep"])
    print("soundfont: %s font on %d of %d played programs (%d keep, "
          "%d psg/unlisted); shared archive %d bytes, own %s"
          % (slot, swapped, len(used), kept,
             len(used) - swapped - kept, len(shared_blob),
             {b: sum(len(w) for w in own[b]) for b in sorted(own)}))
    print("soundfont: %s is %d bytes" % (out_path.name,
                                         out_path.stat().st_size))
    return 0


# --------------------------------------------------------------- previews
def write_previews(outdir, ptf, fof, mapping, slot):
    import numpy as np
    import wave as wavemod
    outdir.mkdir(parents=True, exist_ok=True)

    def dump(name, blob):
        f = wave_fp(blob)
        if f is None:
            return
        with wavemod.open(str(outdir / name), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(f["rate"])
            w.writeframes(f["pcm16"][:f["rate"] * 2].tobytes())

    for p, (val, tier) in sorted(mapping.items()):
        if tier in ("psg", "bytes", "keep") or val is None:
            continue
        prog = ptf.canonical(p)
        pw = next((w for w in ptf.waves_of(prog) if w), None)
        if pw:
            dump("p%03d_platinum.wav" % p, pw)
        sval = str(val)
        if "@" in sval:
            q, bank = sval.split("@", 1)
            dprog = fof.variant_of(int(q), int(bank))
        else:
            dprog = fof.canonical(int(sval))
        if dprog:
            dw = next((w for w in fof.waves_of(dprog) if w), None)
            if dw:
                dump("p%03d_%s_%s.wav" % (p, slot, tier), dw)


# --------------------------------------------------------------- main
def main(argv):
    engine = ""
    args = []
    force = False
    previews = None
    out_table = None
    it = iter(argv[1:])
    for a in it:
        if a.startswith("--engine="):
            engine = a.split("=", 1)[1]
        elif a == "--force":
            force = True
        elif a.startswith("--previews="):
            previews = Path(a.split("=", 1)[1])
        elif a.startswith("--out="):
            out_table = Path(a.split("=", 1)[1])
        else:
            args.append(a)
    if not engine:
        import os
        engine = os.environ.get("ENGINE", "")

    if not args:
        die("usage: soundfont.py survey|compose ... (the docstring has both)")
    cmd = args[0]
    if cmd == "survey":
        if len(args) != 4:
            die("survey <base> <foreign> <heartgold|blackwhite>")
        slot = args[3]
        if slot not in st.SLOT_CODES:
            die("'%s' is not a slot" % slot)
        base = load(args[1], engine, st.BASE_CODES)
        foreign = load(args[2], engine, st.SLOT_CODES[slot])
        ptf = pt_font(base)
        fof = foreign_font(foreign, slot)
        used, notes = played_programs(base)
        fps = {}
        raw = match_by_evidence(ptf, fof, used, fps)

        def cell_for(p, qb):
            """The donor, plus what its levels have to be scaled by to sit
            where the instrument it replaces sat."""
            if qb is None:
                return "keep"
            prog = ptf.canonical(p)
            donor = fof.variant_of(qb[0], qb[1])
            pct = 100
            if prog is not None and donor is not None:
                pd = [d for (rk, d) in prog[3] if rk in REC_PCM]
                dd = [d for (rk, d) in donor[3] if rk in REC_PCM]
                pw = [w for w in ptf.waves_of(prog) if w is not None]
                dw = [w for w in fof.waves_of(donor) if w is not None]
                if pd and dd and pw and dw:
                    pct = sustain_scale(wave_rms(pw[0]), wave_rms(dw[0]))
            return "%d@%d%s" % (qb[0], qb[1],
                                "" if pct == 100 else "/%d" % pct)

        mapping = {p: (cell_for(p, qb), tier)
                   for p, (qb, tier) in raw.items() if tier != "psg"}
        pre = {p: (cell_for(p, qb) if qb is not None else None, t2)
               for p, (qb, t2) in raw.items()}
        table = out_table or (Path(__file__).resolve().parent.parent
                              / "SOUNDFONTS")
        write_table(table, slot, mapping, ptf, used, notes, force)
        tiers = Counter(t for (_, t) in mapping.values())
        print("soundfont: %s -> %s (%s)" % (slot, table, dict(tiers)))
        if previews:
            write_previews(previews, ptf, fof, pre, slot)
            print("soundfont: previews in %s" % previews)
        return 0
    if cmd == "compose":
        if len(args) != 6:
            die("compose <base> <foreign> <table> <slot> <out.sdat>")
        slot = args[4]
        if slot not in st.SLOT_CODES:
            die("'%s' is not a slot" % slot)
        base = load(args[1], engine, st.BASE_CODES)
        foreign = load(args[2], engine, st.SLOT_CODES[slot])
        return compose(base, foreign, Path(args[3]), slot, Path(args[5]))
    die("unknown command '%s'" % cmd)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
