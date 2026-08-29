#!/usr/bin/env python3
"""Port one named track out of another cartridge's SDAT into this game's."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

REC = ["SEQ", "SEQARC", "BANK", "WAVEARC", "PLAYER", "GROUP", "PLAYER2", "STRM"]
BLOCK_HEADER = 0x40      # magic, size, eight record offsets, six reserved
FILE_ALIGN = 32
NO_ARCHIVE = 0xFFFF

# ---------------------------------------------------------------- the closure
SBNK_COUNT = 0x38        # u32, then four bytes per instrument
SBNK_ENTRY = 0x3C
NOTE_DEF = 10
SWAR_COUNT = 0x38        # u32, then a u32 offset per sample
SWAR_TABLE = 0x3C
SWAR_ALIGN = 4

# Instrument record kinds, from the two the bank actually uses beyond a plain
# note: a drum set is a low and high note and then one entry per note between
# them, a key split is eight boundaries and one entry per region. Both entries
# are a u16 kind and then a note def.
REC_NOTE = (1, 2, 3, 4, 5)
REC_DRUMS = 16
REC_SPLIT = 17
SUB_ENTRY = 12

# A bank names four wave archives and a note def says which of the four it
# reads. This marks a def whose archive has been decided but not yet numbered,
# because the carried archive's slot is not known until every wave is placed.
SLOT_PENDING = 0xFFFF


def die(msg: str) -> None:
    print("portmusic: " + msg, file=sys.stderr)
    raise SystemExit(2)


def varlen(b: bytes, i: int) -> tuple[int, int]:
    v = 0
    while True:
        c = b[i]
        i += 1
        v = (v << 7) | (c & 0x7F)
        if not c & 0x80:
            return v, i


def sseq_scan(blob: bytes) -> tuple[set[int], set[tuple[int, int]], bool]:
    """What a sequence selects and what it plays: programs, (program, note), whole."""
    base = struct.unpack_from("<I", blob, 0x18)[0]
    progs: set[int] = set()
    pairs: set[tuple[int, int]] = set()
    complete = True
    starts, seen = [(base, 0, 0)], set()
    k = 0
    while k < len(starts):
        i, prog, transpose = starts[k]
        guard = 0
        k += 1
        while 0 <= i < len(blob):
            guard += 1
            if guard > 0x20000:
                complete = False
                break
            if (i, prog, transpose) in seen:
                break
            seen.add((i, prog, transpose))
            c = blob[i]
            i += 1
            if c < 0x80:                              # note: velocity, length
                pairs.add((prog, max(0, min(127, c + transpose))))
                i += 1
                _, i = varlen(blob, i)
            elif c == 0x80:                           # rest
                _, i = varlen(blob, i)
            elif c == 0x81:                           # program change
                v, i = varlen(blob, i)
                prog = v & 0x7F
                progs.add(prog)
            elif c == 0x93:                           # open track
                i += 1
                off = blob[i] | (blob[i + 1] << 8) | (blob[i + 2] << 16)
                i += 3
                starts.append((base + off, 0, 0))
            elif c in (0x94, 0x95):                   # jump, call
                off = blob[i] | (blob[i + 1] << 8) | (blob[i + 2] << 16)
                i += 3
                starts.append((base + off, prog, transpose))
                if c == 0x94:
                    break
            elif c in (0xA0, 0xA1, 0xA2):             # random / var / if prefix
                pass
            elif 0xB0 <= c <= 0xBD:                   # var op: u8 var, s16
                i += 3
            elif c == 0xC3:                           # transpose
                transpose = struct.unpack_from("<b", blob, i)[0]
                i += 1
            elif 0xC0 <= c <= 0xD6:                   # one-byte parameters
                i += 1
            elif c in (0xE0, 0xE1, 0xE3):             # two-byte parameters
                i += 2
            elif c == 0xFC:                           # loop end
                pass
            elif c in (0xFD, 0xFF):                   # return, end of track
                break
            elif c == 0xFE:                           # allocate tracks
                i += 2
            else:
                complete = False                      # an opcode with no length
                break
    # A track that plays before its first program change is on instrument 0.
    progs |= {p for p, _ in pairs}
    return progs, pairs, complete


def sbnk_note_defs(blob: bytes, want: set[int],
                   pairs: set[tuple[int, int]] | None = None) -> list[int]:
    """Where the note defs of the wanted instruments sit in an SBNK."""
    n = struct.unpack_from("<I", blob, SBNK_COUNT)[0]
    out = []
    for i in range(n):
        kind = blob[SBNK_ENTRY + i * 4]
        off = struct.unpack_from("<H", blob, SBNK_ENTRY + i * 4 + 1)[0]
        if kind == 0 or off == 0 or i not in want:
            continue
        notes = None if pairs is None else {nt for p, nt in pairs if p == i}
        if kind in REC_NOTE:
            here = [off]
        elif kind == REC_DRUMS:
            lo, hi = blob[off], blob[off + 1]
            here = [off + 2 + k * SUB_ENTRY + 2 for k in range(hi - lo + 1)
                    if notes is None or lo + k in notes]
        elif kind == REC_SPLIT:
            bounds = blob[off:off + 8]
            here = []
            for k in range(sum(1 for b in bounds if b)):
                low = 0 if k == 0 else bounds[k - 1] + 1
                if notes is None or any(low <= nt <= bounds[k] for nt in notes):
                    here.append(off + 8 + k * SUB_ENTRY + 2)
        else:
            die("instrument %d is record kind %d, which this does not read"
                % (i, kind))
        for p in here:
            if p + NOTE_DEF > len(blob):
                die("instrument %d has a note def past the end of the bank" % i)
        out += here
    return out


def build_swar(samples: list[bytes]) -> bytes:
    """A wave archive holding exactly the samples handed to it."""
    head = SWAR_TABLE + 4 * len(samples)
    offs, body = [], bytearray()
    for s in samples:
        body += b"\0" * ((-len(body)) % SWAR_ALIGN)
        offs.append(head + len(body))
        body += s
    size = head + len(body)
    out = bytearray(b"SWAR")
    out += struct.pack("<IIHH", 0x0100FEFF, size, 0x10, 1)
    out += b"DATA" + struct.pack("<I", size - 0x10) + bytes(32)
    out += struct.pack("<I", len(samples))
    out += struct.pack("<%dI" % len(samples), *offs)
    out += body
    return bytes(out)


def swar_samples(blob: bytes) -> list[bytes]:
    n = struct.unpack_from("<I", blob, SWAR_COUNT)[0]
    offs = [struct.unpack_from("<I", blob, SWAR_TABLE + 4 * i)[0]
            for i in range(n)]
    return [blob[o:(offs[i + 1] if i + 1 < n else len(blob))]
            for i, o in enumerate(offs)]


class Sdat:
    """Enough of the container to take it apart and put it back together."""

    def __init__(self, path: Path):
        d = path.read_bytes()
        self.raw = d
        if d[:4] != b"SDAT":
            die("%s is not an SDAT (magic %r)" % (path, d[:4]))
        nblk, = struct.unpack_from("<H", d, 14)
        self.block = {}
        for i in range(nblk):
            off, size = struct.unpack_from("<II", d, 0x10 + i * 8)
            if size:
                self.block[d[off:off + 4].decode("latin1").rstrip()] = (off, size)
        for need in ("INFO", "FAT", "FILE"):
            if need not in self.block:
                die("%s has no %s block" % (path, need))
        self.names = self._lists("SYMB", self._name_at)
        self.info = self._lists("INFO", None)
        self.fat = self._fat()

    # reading -------------------------------------------------------------
    def _lists(self, tag, conv):
        """The eight per-kind lists in a SYMB or INFO block, plus which of
        them the original left as a null offset (preserved on write)."""
        out, present = {}, {}
        if tag not in self.block:
            return {k: [] for k in REC}
        b, size = self.block[tag]
        for i, kind in enumerate(REC):
            ro, = struct.unpack_from("<I", self.raw, b + 8 + i * 4)
            present[kind] = ro != 0
            if ro == 0:
                out[kind] = []
                continue
            n, = struct.unpack_from("<I", self.raw, b + ro)
            offs = [struct.unpack_from("<I", self.raw, b + ro + 4 + j * 4)[0]
                    for j in range(n)]
            if conv is None:
                out[kind] = self._records(b, size, offs, kind)
            else:
                out[kind] = [conv(b, o) for o in offs]
        setattr(self, tag.lower() + "_present", present)
        return out

    def _name_at(self, b, o):
        if o == 0:
            return ""
        e = self.raw.index(b"\0", b + o)
        return self.raw[b + o:e].decode("latin1")

    def _records(self, b, size, offs, kind):
        """Each INFO record as raw bytes."""
        fixed = {"SEQ": 12, "SEQARC": 4, "BANK": 12, "WAVEARC": 4,
                 "PLAYER": 8, "STRM": 12}
        out = []
        for o in offs:
            if o == 0:
                out.append(None)
                continue
            if kind == "GROUP":
                n, = struct.unpack_from("<I", self.raw, b + o)
                ln = 4 + n * 8
            elif kind == "PLAYER2":
                ln = 1 + self.raw[b + o]
            else:
                ln = fixed[kind]
            out.append(self.raw[b + o:b + o + ln])
        return out

    def _fat(self):
        b, _ = self.block["FAT"]
        n, = struct.unpack_from("<I", self.raw, b + 8)
        out = []
        for i in range(n):
            off, size = struct.unpack_from("<II", self.raw, b + 12 + i * 16)
            out.append(self.raw[off:off + size])
        return out

    # the graph -----------------------------------------------------------
    def seq_bank(self, i):
        r = self.info["SEQ"][i]
        return struct.unpack_from("<H", r, 4)[0]

    def seq_file(self, i):
        return struct.unpack_from("<H", self.info["SEQ"][i], 0)[0]

    def bank_file(self, i):
        return struct.unpack_from("<H", self.info["BANK"][i], 0)[0]

    def bank_swar(self, i):
        return list(struct.unpack_from("<4H", self.info["BANK"][i], 4))

    def wavearc_file(self, i):
        return struct.unpack_from("<H", self.info["WAVEARC"][i], 0)[0]

    # writing -------------------------------------------------------------
    def build(self) -> bytes:
        symb = self._build_list_block(b"SYMB", self.names, symbols=True)
        info = self._build_list_block(b"INFO", self.info, symbols=False)
        nfat = len(self.fat)
        fat_size = 12 + nfat * 16
        fat_size += (-fat_size) % 4
        head = BLOCK_HEADER
        symb_off = head
        info_off = symb_off + len(symb)
        fat_off = info_off + len(info)
        file_off = fat_off + fat_size

        # Files first, so the FAT can carry their absolute offsets.
        body = bytearray()
        cursor = file_off + 16
        cursor += (-cursor) % FILE_ALIGN
        pad = cursor - (file_off + 16)
        body += b"\0" * pad
        fat_rows = []
        for f in self.fat:
            fat_rows.append((file_off + 16 + len(body), len(f)))
            body += f
            body += b"\0" * ((-len(f)) % FILE_ALIGN)

        fat = bytearray(b"FAT " + struct.pack("<II", fat_size, nfat))
        for off, size in fat_rows:
            fat += struct.pack("<IIII", off, size, 0, 0)
        fat += b"\0" * (fat_size - len(fat))

        fileblk = bytearray(b"FILE")
        fileblk += struct.pack("<III", 16 + len(body), nfat, 0)
        fileblk += body

        total = head + len(symb) + len(info) + len(fat) + len(fileblk)
        out = bytearray(b"SDAT")
        out += struct.pack("<IIHH", 0x0100FEFF, total, head, 4)
        out += struct.pack("<II", symb_off, len(symb))
        out += struct.pack("<II", info_off, len(info))
        out += struct.pack("<II", fat_off, fat_size)
        out += struct.pack("<II", file_off, len(fileblk))
        out += b"\0" * (head - len(out))
        return bytes(out + symb + info + fat + fileblk)

    def _build_list_block(self, tag, lists, symbols):
        present = getattr(self, tag.decode("latin1").lower() + "_present",
                          {k: True for k in REC})
        heads, pool = bytearray(), bytearray()
        rec_off = {}
        cur = BLOCK_HEADER
        for kind in REC:
            if not present.get(kind, False):
                rec_off[kind] = 0
                continue
            rec_off[kind] = cur
            cur += 4 + 4 * len(lists[kind])
        pool_base = cur
        entry_off = {}
        for kind in REC:
            if not present.get(kind, False):
                continue
            offs = []
            for item in lists[kind]:
                if item is None or (symbols and item == ""):
                    offs.append(0)
                    continue
                offs.append(pool_base + len(pool))
                pool += (item.encode("latin1") + b"\0") if symbols else item
            entry_off[kind] = offs
        for kind in REC:
            if not present.get(kind, False):
                continue
            heads += struct.pack("<I", len(lists[kind]))
            for o in entry_off[kind]:
                heads += struct.pack("<I", o)
        size = BLOCK_HEADER + len(heads) + len(pool)
        size += (-size) % 4
        out = bytearray(tag + struct.pack("<I", size))
        for kind in REC:
            out += struct.pack("<I", rec_off[kind])
        out += b"\0" * (BLOCK_HEADER - len(out))
        out += heads + pool
        out += b"\0" * (size - len(out))
        return bytes(out)


def append_closure(src, s: int, dst) -> tuple[int, dict]:
    """Append one source sequence's playing closure to dst: its bank, narrowed to what the walk
    says it plays, and one wave archive of the track's own.
    """
    want = src.names["SEQ"][s]
    bank = src.seq_bank(s)
    new_bank = len(dst.info["BANK"])

    def add_file(data: bytes) -> int:
        dst.fat.append(data)
        return len(dst.fat) - 1

    # The track brings its own instruments, except the ones already here.
    #
    # Both games call their shared instrument set WAVE_ARC_BASIC and both field
    # banks are [BASIC, the track's own small archive, -, -]. The first version
    # of this carried the source's whole BASIC beside the destination's and got
    # silence, 431 KB is a load no player heap takes, and the second reused
    # the destination's by name, which plays but is a substitution: a bank's
    # note def is an INDEX into that archive, and HeartGold's BASIC and
    # Platinum's are byte-identical at only 4 of 158 indices. Goldenrod's bank
    # points at all 158 of them, so every instrument in the track was whatever
    # Sinnoh happens to keep at that number. It played. It was not the track.
    #
    # What it is instead is a closure with an identity test in it:
    #
    #   * walk the sequence for the instruments it selects and the notes it
    #     plays on each, a drum kit is one wave per note and a track that hits
    #     two of its twelve needs two;
    #   * a wave that is byte-identical to one in an archive the destination
    #     ALSO NAMES is not carried at all. It is already resident and it is the
    #     same bytes, so pointing at it is exact rather than approximate;
    #   * everything else is carried into one archive of this track's own, and
    #     the note defs are renumbered onto it.
    #
    # Goldenrod's theme selects 9 of its bank's 105 instruments and plays 20
    # waves; 12 of them are already here and 8 are carried, 48 KB against 431,
    # and every note sounds the wave the source game wrote for it. The size is
    # the point as well as the fidelity: the sound heap refused 134 KB and 90 KB
    # and took 71 KB, and this game's own largest BGM bank loads 34 KB.
    seq_blob = src.fat[src.seq_file(s)]
    bank_blob = bytearray(src.fat[src.bank_file(bank)])
    progs, pairs, whole_walk = sseq_scan(seq_blob)
    n_inst = struct.unpack_from("<I", bank_blob, SBNK_COUNT)[0]
    if not progs:
        die("%s selects no instrument at all; the walk of it found nothing to "
            "carry" % want)
    beyond = sorted(p for p in progs if p >= n_inst)
    progs = {p for p in progs if p < n_inst}

    # Which of the destination's archives a carried wave might already be in.
    # Only an archive both games name is a candidate: it is the one the
    # destination keeps resident, and identity is what makes pointing at it a
    # port rather than a substitution.
    resident: dict[bytes, tuple[int, int]] = {}
    shared = []
    for k, w in enumerate(src.bank_swar(bank)):
        if w == NO_ARCHIVE:
            continue
        name = src.names["WAVEARC"][w] if src.names["WAVEARC"] else ""
        if not name or name not in dst.names["WAVEARC"]:
            continue
        here = dst.names["WAVEARC"].index(name)
        shared.append((name, here))
        for j, blob in enumerate(swar_samples(dst.fat[dst.wavearc_file(here)])):
            resident.setdefault(bytes(blob), (here, j))

    src_arcs = {k: swar_samples(src.fat[src.wavearc_file(w)])
                for k, w in enumerate(src.bank_swar(bank)) if w != NO_ARCHIVE}
    defs = sbnk_note_defs(bank_blob, progs, pairs if whole_walk else None)
    slots: list[int] = []          # destination wave-archive id per bank slot
    carried_waves: dict[tuple[int, int], int] = {}
    samples: list[bytes] = []
    reused = 0
    for p in defs:
        idx, slot = struct.unpack_from("<HH", bank_blob, p)
        if slot not in src_arcs:
            die("an instrument reads wave archive slot %d and the bank names "
                "none there" % slot)
        pool = src_arcs[slot]
        if idx >= len(pool):
            die("an instrument asks for wave %d of an archive with %d"
                % (idx, len(pool)))
        wave = bytes(pool[idx])
        here = resident.get(wave)
        if here is not None:
            if here[0] not in slots:
                slots.append(here[0])
            struct.pack_into("<HH", bank_blob, p, here[1], slots.index(here[0]))
            reused += 1
            continue
        key = (slot, idx)
        if key not in carried_waves:
            carried_waves[key] = len(samples)
            samples.append(wave)
        struct.pack_into("<HH", bank_blob, p, carried_waves[key], SLOT_PENDING)

    new_wave = len(dst.info["WAVEARC"])
    dst.info["WAVEARC"].append(
        struct.pack("<HH", add_file(build_swar(samples)), 0))
    dst.names["WAVEARC"].append("WAVE_ARC_%s_PORTED" % want.replace("SEQ_", ""))
    slots.append(new_wave)
    if len(slots) > 4:
        die("this track's waves come from %d archives and a bank names four"
            % len(slots))
    # The carried archive's slot was written as -1 above because its number was
    # not known until every wave had been placed.
    for p in defs:
        idx, slot = struct.unpack_from("<HH", bank_blob, p)
        if slot == SLOT_PENDING:
            struct.pack_into("<HH", bank_blob, p, idx, len(slots) - 1)

    # Every instrument the sequence does not select is cleared. Its note defs
    # would point into an archive that no longer holds their samples, and a
    # bank whose unused rows are empty is the shape this game's own banks have.
    for i in range(n_inst):
        if i not in progs:
            bank_blob[SBNK_ENTRY + i * 4] = 0

    brec = bytearray(src.info["BANK"][bank])
    struct.pack_into("<H", brec, 0, add_file(bytes(bank_blob)))
    struct.pack_into("<4H", brec, 4,
                     *(slots + [NO_ARCHIVE] * (4 - len(slots))))
    dst.info["BANK"].append(bytes(brec))
    dst.names["BANK"].append(src.names["BANK"][bank] + "_PORTED")

    return new_bank, {
        "wave": new_wave,
        "progs_n": len(progs),
        "inst_n": n_inst,
        "defs_n": len(defs),
        "pairs_n": len(pairs),
        "whole_walk": whole_walk,
        "reused": reused,
        "shared_names": [n for n, _ in shared],
        "carried_n": len(samples),
        "carried_bytes": sum(len(x) for x in samples),
        "bank_bytes": len(bank_blob),
        "beyond": beyond,
    }


def main(argv):
    if len(argv) != 5:
        die("usage: portmusic.py <src.sdat> <dst.sdat> <SEQ_NAME> <out.sdat>")
    src = Sdat(Path(argv[1]))
    dst = Sdat(Path(argv[2]))
    want, out_path = argv[3], Path(argv[4])

    if want not in src.names["SEQ"]:
        near = [n for n in src.names["SEQ"] if want.split("_")[-1] in n][:5]
        die("no sequence called %s in the source%s"
            % (want, ("; near: " + ", ".join(near)) if near else ""))
    s = src.names["SEQ"].index(want)

    # Where the appended records land. Nothing already in dst moves.
    new_seq = len(dst.info["SEQ"])
    new_bank, st = append_closure(src, s, dst)

    srec = bytearray(src.info["SEQ"][s])
    dst.fat.append(src.fat[src.seq_file(s)])
    struct.pack_into("<H", srec, 0, len(dst.fat) - 1)
    struct.pack_into("<H", srec, 4, new_bank)
    dst.info["SEQ"].append(bytes(srec))
    dst.names["SEQ"].append(want + "_PORTED")

    out_path.write_bytes(dst.build())
    print("portmusic: %s -> sequence %d, bank %d, wave archive %d"
          % (want, new_seq, new_bank, st["wave"]))
    print("portmusic:   %d of the bank's %d instruments, %d note def(s)%s"
          % (st["progs_n"], st["inst_n"], st["defs_n"],
             (", narrowed to the %d note(s) it plays" % st["pairs_n"])
             if st["whole_walk"] else
             ", the walk of it did not finish, so every note def of every "
             "instrument it selects is carried rather than guessed at"))
    print("portmusic:   %d wave(s) already here, byte-identical, in %s"
          % (st["reused"], ", ".join(st["shared_names"]) or "nothing shared"))
    print("portmusic:   %d wave(s) carried, %d bytes"
          % (st["carried_n"], st["carried_bytes"]))
    if st["beyond"]:
        print("portmusic:   %d program change(s) past the bank's last "
              "instrument, ignored: %s" % (len(st["beyond"]), st["beyond"]))
    print("portmusic: %s is %d bytes (%d files); nothing already in it moved"
          % (out_path.name, out_path.stat().st_size, len(dst.fat)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
