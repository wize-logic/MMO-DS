#!/usr/bin/env python3
"""Re-scramble a Gen 4 sprite sheet from one game's direction into this one's."""

from __future__ import annotations

import sys
from pathlib import Path

A, C = 1103515245, 24691

# RGCN header, then the RAHC section header. Character data starts here, and
# only the character data is scrambled, the headers are plain.
DATA_OFFSET = 0x30
SEED = 0x1234


def die(msg: str) -> None:
    print("rescramble: " + msg, file=sys.stderr)
    raise SystemExit(2)


def _words(b: bytes) -> list[int]:
    return [b[i] | (b[i + 1] << 8) for i in range(0, len(b) & ~1, 2)]


def _bytes(w: list[int]) -> bytes:
    out = bytearray()
    for v in w:
        out.append(v & 0xFF)
        out.append((v >> 8) & 0xFF)
    return bytes(out)


def decode(buf: bytes, mode: int) -> bytes:
    """Undo one direction's scramble. Mode 0 is a copy."""
    if mode == 0:
        return buf
    w = _words(buf)
    if not w:
        return buf
    if mode == 2:
        v = w[0]
        for i in range(len(w)):
            w[i] ^= v & 0xFFFF
            v = (v * A + C) & 0xFFFFFFFF
    elif mode == 1:
        v = w[-1]
        for i in range(len(w) - 1, -1, -1):
            w[i] ^= v & 0xFFFF
            v = (v * A + C) & 0xFFFFFFFF
    else:
        die("mode %d is not one the engine's tool defines (1 or 2)" % mode)
    return _bytes(w) + buf[len(w) * 2:]


def encode(plain: bytes, mode: int, seed: int = SEED) -> bytes:
    """Apply one direction's scramble to plain character data."""
    if mode == 0:
        return plain
    w = _words(plain)
    if not w:
        return plain
    out = list(w)
    if mode == 2:
        if w[0] != 0:
            die("mode 2 needs the first word to be background (got 0x%04x); "
                "this sheet is not one this can re-encode" % w[0])
        s = seed & 0xFFFF
        out[0] = s
        for i in range(1, len(w)):
            s = (s * A + C) & 0xFFFFFFFF
            out[i] = w[i] ^ (s & 0xFFFF)
    elif mode == 1:
        if w[-1] != 0:
            die("mode 1 needs the last word to be background (got 0x%04x)"
                % w[-1])
        s = seed & 0xFFFF
        out[-1] = s
        for i in range(len(w) - 2, -1, -1):
            s = (s * A + C) & 0xFFFFFFFF
            out[i] = w[i] ^ (s & 0xFFFF)
    else:
        die("mode %d is not one the engine's tool defines (1 or 2)" % mode)
    return _bytes(out) + plain[len(w) * 2:]


def background_share(buf: bytes) -> float:
    """How much of the sheet is palette index 0. Noise sits near 1/16."""
    if not buf:
        return 0.0
    zero = sum((b & 0xF) == 0 for b in buf) + sum((b >> 4) == 0 for b in buf)
    return zero / (len(buf) * 2)


def convert(member: bytes, src_mode: int, dst_mode: int) -> bytes:
    if member[:4] not in (b"RGCN", b"NCGR"):
        die("not an NCGR member (magic %r)" % member[:4])
    if len(member) <= DATA_OFFSET:
        die("member is %d bytes, shorter than its own header" % len(member))
    head, body = member[:DATA_OFFSET], member[DATA_OFFSET:]
    plain = decode(body, src_mode)
    out = head + encode(plain, dst_mode)
    # The round trip is the check: what we wrote has to read back as what we
    # decoded, or the sheet in the package is not the picture on the cartridge.
    if decode(out[DATA_OFFSET:], dst_mode) != plain:
        die("re-encode did not round trip; nothing written")
    return out


def main(argv: list[str]) -> int:
    if len(argv) >= 3 and argv[1] == "--check":
        path = Path(argv[2])
        mode = int(argv[3]) if len(argv) > 3 else 2
        data = path.read_bytes()
        if data[:4] not in (b"RGCN", b"NCGR"):
            die("%s is not an NCGR" % path)
        share = background_share(decode(data[DATA_OFFSET:], mode))
        print("rescramble: %s decoded with mode %d is %.1f%% background"
              % (path.name, mode, share * 100))
        return 0 if share > 0.4 else 1

    if len(argv) != 4:
        die("usage: rescramble.py <file> <from-mode> <to-mode>  "
            "(or --check <file> <mode>)")
    path = Path(argv[1])
    src, dst = int(argv[2]), int(argv[3])
    if src == dst:
        return 0
    data = path.read_bytes()
    path.write_bytes(convert(data, src, dst))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
