#!/usr/bin/env python3
"""Port the Gen 5 cries out of a Black/White cartridge, as host PCM."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import portmusic as pm          # noqa: E402
import soundtrack as st         # noqa: E402

MMO = Path(__file__).resolve().parent.parent

FIRST = 494                     # the egg ids are not species; Victini is 494
LAST = 649

# The NDS/IMA step walk, the same arithmetic the DS decodes with.
STEPS = [
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41,
    45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190,
    209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724,
    796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272,
    2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132,
    7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
    20350, 22385, 24623, 27086, 29794, 32767,
]
INDEX_STEP = [-1, -1, -1, -1, 2, 4, 6, 8]


def adpcm(data: bytes) -> bytes:
    sample, index = struct.unpack_from("<hH", data, 0)
    index = min(index, 88)
    out = bytearray()
    for byte in data[4:]:
        for nib in (byte & 0xF, byte >> 4):
            step = STEPS[index]
            diff = step >> 3
            if nib & 1:
                diff += step >> 2
            if nib & 2:
                diff += step >> 1
            if nib & 4:
                diff += step
            sample = max(-32768, min(32767, sample - diff if nib & 8 else sample + diff))
            index = max(0, min(88, index + INDEX_STEP[nib & 7]))
            out += struct.pack("<h", sample)
    return bytes(out)


def decode(swav: bytes) -> tuple[int, bytes]:
    kind, _loop, rate, _time, _loopofs, _length = struct.unpack_from("<BBHHHI", swav, 0)
    data = swav[12:]
    if kind == 0:
        # PCM8 on the DS is signed.
        pcm = b"".join(struct.pack("<h", struct.unpack_from("<b", data, i)[0] << 8)
                       for i in range(len(data)))
        return rate, pcm
    if kind == 1:
        return rate, data
    if kind == 2:
        return rate, adpcm(data)
    raise SystemExit("portcries: sample kind %d has no decoder" % kind)


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    image = Path(argv[1])
    out = Path(argv[2]) if len(argv) > 2 else MMO / "mods" / "imports" / "cries.bin"
    engine = os.path.join(REPO, "engine", "pokeplatinum")
    import os
    engine = os.environ.get("ENGINE_DIR", engine)
    sd = st.load_sdat(image, engine, ("IRBO", "IRAO"))

    entries = []
    blobs = []
    for species in range(FIRST, LAST + 1):
        arc = sd.fat[sd.wavearc_file(species)]
        samples = pm.swar_samples(arc)
        if not samples:
            print("portcries: species %d has an empty wave arc; skipped" % species)
            continue
        rate, pcm = decode(samples[0])
        entries.append((species, rate, len(pcm) // 2))
        blobs.append(pcm)

    header = struct.pack("<4sHH", b"OCRY", len(entries), 0)
    table = b""
    offset = len(header) + len(entries) * 16
    for (species, rate, nsamples), pcm in zip(entries, blobs):
        table += struct.pack("<HHIII", species, rate, nsamples, offset, 0)
        offset += len(pcm)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(header + table + b"".join(blobs))
    print("portcries: %d cries, %d bytes -> %s" % (len(entries), out.stat().st_size, out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
