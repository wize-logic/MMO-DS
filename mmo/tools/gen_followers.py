#!/usr/bin/env python3
"""Generate the table that turns a species into the Pokemon walking behind you."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

MMO = Path(__file__).resolve().parent.parent

# `FollowMon_GetSpriteID`'s base, out of HeartGold's own sprites.h. Read rather
# than assumed, so a renumbered sprite table is a refusal and not a shift.
BASE_SPRITE_NAME = "FOLLOWER_MON_BULBASAUR"

# What a follower member looks like, measured across all 566 of them.
FOLLOWER_FRAMES = 8
FOLLOWER_SIZES = (32, 64)
FOLLOWER_FORMAT = 3  # 4bpp; nsbtx.py's FORMAT_BITS calls it 4
FOLLOWER_PALETTES = ("tsure_poke0", "tsure_poke1")

# The archives `--verify` reads, by the path HeartGold and SoulSilver both keep
# them at. The names are stripped in the official image; these are the ids.
SRC_MMODEL = "a/0/8/1"
SRC_TP_PARAM = "a/1/4/1"

# Where a filled package puts the first follower. Defined in tools/portfollow.py
# and repeated into the generated header so the client and the porter cannot
# disagree; tests/follower_test.c compares the two.
FOLLOWER_GFX_BASE = 1024


def die(msg: str) -> None:
    print("gen_followers: " + msg, file=sys.stderr)
    raise SystemExit(2)


def decomp_dir(name: str) -> Path | None:
    try:
        out = subprocess.run([str(MMO / "tools" / "decomp_dir.sh"), name],
                             capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    got = out.stdout.strip()
    return Path(got) if got else None


def defines(path: Path, prefix: str) -> dict[str, int]:
    """`#define <prefix><NAME> <number>` -> {NAME: number}, first wins."""
    out: dict[str, int] = {}
    for m in re.finditer(r"#define\s+%s(\w+)\s+(\d+)" % prefix,
                         path.read_text()):
        out.setdefault(m.group(1), int(m.group(2)))
    return out


def numbered(path: Path, prefix: str) -> dict[int, str]:
    """The same file the other way round: {number: first name for it}."""
    out: dict[int, str] = {}
    for m in re.finditer(r"#define\s+%s(\w+)\s+(\d+)" % prefix,
                         path.read_text()):
        out.setdefault(int(m.group(2)), m.group(1))
    return out


def array(src: str, name: str) -> list[str]:
    """The initialiser tokens of `static const u16 <name>[] = { ... };`."""
    head = "static const u16 %s[] = {" % name
    try:
        start = src.index(head) + len(head)
        stop = src.index("};", start)
    except ValueError:
        die("no %s[] in follow_mon.c; the follower tables moved" % name)
    toks = []
    for line in src[start:stop].splitlines():
        line = line.split("//")[0].strip()
        for tok in line.split(","):
            tok = tok.strip()
            if tok:
                toks.append(tok)
    return toks


def resolve(toks: list[str], idx: dict[str, int], what: str) -> list[int]:
    out = []
    for tok in toks:
        if tok.startswith("FOLLOWER_MON_"):
            key = tok[len("FOLLOWER_MON_"):]
            if key not in idx:
                die("%s names FOLLOWER_MON_%s and follow_mon_idx.h does not"
                    % (what, key))
            out.append(idx[key])
        elif tok in ("TRUE", "FALSE"):
            out.append(1 if tok == "TRUE" else 0)
        else:
            try:
                out.append(int(tok, 0))
            except ValueError:
                die("%s holds %r, which is neither a number nor a "
                    "FOLLOWER_MON_ id" % (what, tok))
    return out


def read_tables(hg: Path):
    """Everything this table is made of, straight out of the checkout."""
    idx = defines(hg / "include/constants/follow_mon_idx.h", "FOLLOWER_MON_")
    sprites = defines(hg / "include/constants/sprites.h", "SPRITE_")
    models = defines(hg / "include/constants/mmodel.h", "MMODEL_")
    species = numbered(hg / "include/constants/species.h", "SPECIES_")
    if not (idx and sprites and models and species):
        die("a constants header in %s/include/constants is empty or missing"
            % hg)
    if BASE_SPRITE_NAME not in sprites:
        die("sprites.h has no SPRITE_%s to count from" % BASE_SPRITE_NAME)

    src = (hg / "src/follow_mon.c").read_text()
    model_lut = resolve(array(src, "sModelIndexLUT"), idx, "sModelIndexLUT")
    form_lut = resolve(array(src, "sFormMaxLUT"), idx, "sFormMaxLUT")
    female_lut = resolve(array(src, "sFemaleFlagLUT"), idx, "sFemaleFlagLUT")

    # sModelIndexLUT is indexed by species and the other two by species-1, which
    # is what FollowMon_GetSpriteID and OverworldModelLookupFormCount do.
    if len(model_lut) != len(form_lut) + 1 or len(form_lut) != len(female_lut):
        die("the three follower tables are %d/%d/%d long; the first is indexed "
            "by species and the other two by species-1, so they should be "
            "n+1/n/n" % (len(model_lut), len(form_lut), len(female_lut)))
    return sprites, models, species, model_lut, form_lut, female_lut


def build(hg: Path):
    """A row per species, plus the sprite expansion the oracles check."""
    sprites, models, species, model_lut, form_lut, female_lut = read_tables(hg)
    base = sprites["SPRITE_" + BASE_SPRITE_NAME] if \
        "SPRITE_" + BASE_SPRITE_NAME in sprites else sprites[BASE_SPRITE_NAME]

    # sprite id -> mmodel member, by the name both headers give it. The same
    # pairing gen_sprites.py writes into mmo/SPRITES, done again from the same
    # two headers rather than read back out of that table: two tables generated
    # from one source can be compared, one generated from the other cannot.
    member_of = {sid: models[name] for name, sid in sprites.items()
                 if name in models}

    rows = []
    expansion = {}   # sprite id -> (species, which sprite of that species)
    for sp in range(1, len(model_lut)):
        offset = model_lut[sp]
        female = 1 if female_lut[sp - 1] else 0
        count = 2 if female else form_lut[sp - 1] + 1
        first = base + offset
        member = member_of.get(first)
        if member is None:
            die("species %d (%s) draws sprite %d and no mmodel member carries "
                "that sprite's name" % (sp, species.get(sp, "?"), first))
        for k in range(count):
            if first + k in expansion:
                other = expansion[first + k][0]
                die("species %d and %d both claim sprite %d; the follower "
                    "tables overlap" % (other, sp, first + k))
            expansion[first + k] = (sp, k)
        rows.append((sp, offset, count, female, member,
                     species.get(sp, "SPECIES_%d" % sp)))

    # Oracle 1. The expansion has to be exactly the sprites the two headers
    # agree on a member for, over a contiguous range with nothing left over.
    named = {sid for sid, mid in member_of.items()
             if sid >= base and _is_follower(sprites, sid)}
    if set(expansion) != named:
        extra = sorted(set(expansion) - named)[:8]
        missing = sorted(named - set(expansion))[:8]
        die("the follower tables and the sprite names disagree: %d emitted "
            "with no member (%s), %d with a member nobody emits (%s)"
            % (len(set(expansion) - named), extra,
               len(named - set(expansion)), missing))
    lo, hi = min(expansion), max(expansion)
    if hi - lo + 1 != len(expansion):
        die("the emitted sprite ids run %d..%d but there are only %d of them; "
            "the range has a hole" % (lo, hi, len(expansion)))

    # HeartGold's six spare follower slots: FOLLOWER_MON-named sprites with a
    # member that no species draws. A script places one by hand; nothing here
    # carries them, and `--verify` needs to know they exist so the shape check
    # can be exact about what it accepts.
    spare = {member_of[sid] for name, sid in sprites.items()
             if name.startswith("FOLLOWER_MON")
             and not name.startswith("FOLLOWER_MON_STATIC")
             and sid in member_of and sid not in expansion}

    return rows, expansion, base, lo, hi, spare


def _is_follower(sprites: dict[str, int], sid: int) -> bool:
    """A sprite id whose name is a FOLLOWER_MON_ one, and not a STATIC_."""
    for name, v in sprites.items():
        if v == sid:
            return (name.startswith("FOLLOWER_MON_")
                    and not name.startswith("FOLLOWER_MON_STATIC"))
    return False


def write(out: Path, rows, expansion, base, lo, hi) -> None:
    two = sum(1 for r in rows if r[3])
    many = sum(1 for r in rows if not r[3] and r[2] > 1)
    lines = [
        "# FOLLOWERS, GENERATED by tools/gen_followers.py; DO NOT EDIT.",
        "#",
        "# The species in your first party slot -> the run of HeartGold",
        "# overworld sprite ids that draw it walking behind you.",
        "#",
        "# HeartGold picks a follower's picture with arithmetic, not a lookup",
        "# (FollowMon_GetSpriteID), so a species owns CONSECUTIVE sprite ids:",
        "# one usually, one per alternate form for the few that have them, and",
        "# two when the female wears a different coat. `sprites` is how many",
        "# and `female` says which of the two rules the second one follows.",
        "#",
        "#   sprite = %d + offset" % base,
        "#   female:      + 1 if the mon is female",
        "#   otherwise:   + min(form, sprites - 1)",
        "#",
        "# `member` is the mmodel.narc member of the FIRST sprite of the run,",
        "# and the rest follow it consecutively. tools/portmap.py appends those",
        "# members to this game's own archive and gives each an appended",
        "# OBJ_EVENT_GFX id, exactly as it already does for a ported person.",
        "#",
        "# There is no size column on purpose. Whether a follower is 32x32 or",
        "# 64x64 needs the cartridge, and this table is generated from a",
        "# checkout so its regeneration gate runs without one; the size reaches",
        "# the client as the billboard model cooked into billboard_gfx.txt.",
        "# `gen_followers.py --verify <rom>` is what checks it against the art.",
        "#",
        "# %d species, %d sprite ids, %d..%d contiguous."
        % (len(rows), len(expansion), lo, hi),
        "# %d species draw a second sprite for the female, %d have alternate"
        % (two, many),
        "# forms, and the arithmetic covers the range with no id twice.",
        "#",
        "# Rows: <species> <offset> <sprites> <female> <member> <name>",
        "",
    ]
    for sp, offset, count, female, member, name in rows:
        lines.append("%4d  %4d  %3d  %d  %5d  %s"
                     % (sp, offset, count, female, member, name))
    lines.append("")
    out.write_text("\n".join(lines))


# What `--break` does to the image in memory, and which refusal each one is
# meant to reach. Every one uses bytes already in the same cartridge, so a mode
# that stops firing is the check drifting rather than the fixture rotting.
BREAKS = {
    "person": "a follower member replaced with a person's (16 frames, one "
              "palette): the shape check",
    "notbtx": "a follower member replaced with a sequence blob: the "
              "is-it-an-NSBTX check",
    "sizeflag": "one tp_param size byte flipped: the art-against-the-"
                "cartridge check",
    "nosequence": "every gfx sequence blanked: the walk-sequence check",
    "stray": "a person's member overwritten with a follower's: the "
             "shape-is-an-identity check",
}


def write_header(out: Path, rows, expansion, base) -> None:
    """The same table again, as the three arrays the client does arithmetic on."""
    def arr(kind, name, vals, per):
        body = []
        for i in range(0, len(vals), per):
            body.append("    " + " ".join("%d," % v for v in vals[i:i + per]))
        return ["static const %s %s[MMO_FOLLOWER_SPECIES] = {" % (kind, name),
                *body, "};", ""]

    lines = [
        "/* follower_index.gen.h, GENERATED by tools/gen_followers.py "
        "--header; DO NOT EDIT.",
        " *",
        " * Which overworld graphics id draws a species walking behind a",
        " * player. HeartGold picks it with arithmetic rather than a lookup",
        " * (FollowMon_GetSpriteID), so what is tabulated is the arithmetic's",
        " * three inputs and a species owns a RUN of consecutive ids: one",
        " * usually, one per alternate form for the few that have them, and two",
        " * when the female wears a different coat.",
        " *",
        " * The base is not HeartGold's sprite id. A follower reaches this game",
        " * as an APPENDED graphics id out of a filled content package, and",
        " * tools/portfollow.py allocates that band, so MMO_FOLLOWER_GFX_BASE",
        " * is portfollow.py's FOLLOWER_GFX_BASE and the two have to agree.",
        " * tests/import_test.sh compares them against a real fill; the",
        " * arithmetic itself is tests/follower_test.c.",
        " */",
        "",
        "#define MMO_FOLLOWER_SPECIES     %d" % len(rows),
        "#define MMO_FOLLOWER_SPRITES     %d" % len(expansion),
        "#define MMO_FOLLOWER_SPRITE_BASE %d"
        "  /* HeartGold's own SPRITE_FOLLOWER_MON_BULBASAUR */" % base,
        "#define MMO_FOLLOWER_GFX_BASE    %d"
        "  /* portfollow.py's FOLLOWER_GFX_BASE */" % FOLLOWER_GFX_BASE,
        "#define MMO_FOLLOWER_GENDER_FEMALE 1"
        "  /* GENDER_FEMALE here, MON_FEMALE there: the same number */",
        "",
    ]
    lines += arr("unsigned short", "kFollowerOffset",
                 [r[1] for r in rows], 12)
    lines += arr("unsigned char", "kFollowerRun", [r[2] for r in rows], 20)
    lines += arr("unsigned char", "kFollowerFemale", [r[3] for r in rows], 20)
    out.write_text("\n".join(lines))


def verify(rom: Path, hg: Path, broken: str | None = None) -> int:
    """Oracles 2 and 3, against a cartridge the player owns."""
    sys.path.insert(0, str(MMO / "tools"))
    import nsbtx                                            # noqa: E402
    pc = _engine_pc()
    if pc is None:
        die("--verify reads the image with the engine port's own NitroRom and "
            "there is no pokeplatinum checkout to find it in")
    sys.path.insert(0, str(pc))
    try:
        from modport import NitroRom                        # noqa: E402
    except ImportError:
        die("no NitroRom in %s; --verify does not carry a NARC reader of its "
            "own on purpose" % pc)

    rows, expansion, base, lo, hi, spare = build(hg)
    image = NitroRom(rom)
    mmodel = image.narc_members(SRC_MMODEL)
    tp = image.narc_members(SRC_TP_PARAM)

    if len(tp) != len(expansion):
        die("%s holds %d records and the tables emit %d follower sprites; one "
            "record per sprite is what makes the size flag addressable"
            % (SRC_TP_PARAM, len(tp), len(expansion)))

    if broken is not None:
        first = _member_for(rows, expansion, min(expansion))
        if broken == "person":
            mmodel[first] = mmodel[0]
        elif broken == "notbtx":
            model = next((b for b in mmodel if b[:4] == b"BMD0"), b"junk")
            mmodel[first] = model
        elif broken == "sizeflag":
            rec = bytearray(tp[0])
            rec[1] ^= 1
            tp[0] = bytes(rec)
        elif broken == "stray":
            mmodel[0] = mmodel[first]
        elif broken == "nosequence":
            for i, blob in enumerate(mmodel):
                if blob[:4] not in (b"BTX0", b"BMD0") and len(blob) >= 4:
                    mmodel[i] = b"\0\0\0\0" + blob[4:]
        else:
            die("no --break called %r; try %s"
                % (broken, ", ".join(sorted(BREAKS))))
        print("gen_followers: broken on purpose (%s)" % BREAKS[broken])

    sizes = {}
    bad = 0
    for sid in sorted(expansion):
        member = _member_for(rows, expansion, sid)
        if member >= len(mmodel):
            print("  sprite %d wants mmodel member %d and the image has %d"
                  % (sid, member, len(mmodel)))
            bad += 1
            continue
        why = _shape(nsbtx, mmodel[member])
        if isinstance(why, str):
            print("  sprite %d (member %d): %s" % (sid, member, why))
            bad += 1
            continue
        sizes[sid] = why
        flag = tp[sid - lo][1]
        big = why == 64
        if bool(flag) != big:
            print("  sprite %d (member %d): the art is %dx%d and %s byte 1 "
                  "says %d" % (sid, member, why, why, SRC_TP_PARAM, flag))
            bad += 1

    small = sum(1 for v in sizes.values() if v == 32)
    large = sum(1 for v in sizes.values() if v == 64)

    # The shape has to be an identity, not a sanity check: everything in the
    # archive that looks like a follower has to be one. The tables reach 566 of
    # them and HeartGold keeps six spare slots no species maps to, so 572 is the
    # whole answer and a 573rd would mean the shape has stopped being specific.
    want = {_member_for(rows, expansion, s) for s in expansion} | spare
    shaped = {i for i, blob in enumerate(mmodel)
              if not isinstance(_shape(nsbtx, blob), str)}
    if shaped != want:
        loose = sorted(shaped - want)[:8]
        gone = sorted(want - shaped)[:8]
        print("  the follower shape accepts %d members and the tables plus the "
              "%d spare slots account for %d: %d unaccounted for (%s), %d "
              "expected and not accepted (%s)"
              % (len(shaped), len(spare), len(want), len(shaped - want), loose,
                 len(want - shaped), gone))
        bad += 1

    seq, seq_why = _walk_sequence(mmodel)
    if seq is None:
        print("  " + seq_why)
        bad += 1

    print("gen_followers: %d species, %d sprites, %d small and %d large, "
          "%d disagreement(s)" % (len(rows), len(expansion), small, large, bad))
    if seq is not None:
        print("gen_followers: the follower walk sequence is mmodel member %d "
              "(%s)" % (seq, seq_why))

    if broken is not None:
        # The run was supposed to fail. Saying so is the whole point of it.
        if bad:
            print("gen_followers: refused, which is what --break %s asks for"
                  % broken)
            return 0
        print("gen_followers: --break %s changed the image and nothing "
              "objected; the check above is not doing its job" % broken,
              file=sys.stderr)
        return 1
    return 1 if bad else 0


def _member_for(rows, expansion, sid: int) -> int:
    sp, k = expansion[sid]
    for row in rows:
        if row[0] == sp:
            return row[4] + k
    die("species %d has no row" % sp)
    return -1


def _shape(nsbtx, blob: bytes):
    """The member's size in pixels, or a sentence saying why it is not one."""
    if blob[:4] != b"BTX0":
        return "not an NSBTX (%r)" % blob[:4]
    try:
        tex = nsbtx.read(blob)
    except SystemExit:
        return "nsbtx.py refused to read it"
    except Exception as exc:                      # a member that is not one
        return "unreadable as an NSBTX (%s)" % exc.__class__.__name__
    textures, palettes = tex["textures"], tex["palettes"]
    if len(textures) != FOLLOWER_FRAMES:
        return "%d textures, not %d" % (len(textures), FOLLOWER_FRAMES)
    seen = set()
    for param, _extra, _data in textures.values():
        w = 8 << ((param >> 20) & 7)
        h = 8 << ((param >> 23) & 7)
        fmt = (param >> 26) & 7
        if fmt != FOLLOWER_FORMAT:
            return "texture format %d, not %d" % (fmt, FOLLOWER_FORMAT)
        if w != h:
            return "a %dx%d texture, and a follower's frames are square" % (w, h)
        seen.add(w)
    if len(seen) != 1:
        return "frames of %s, and a member's frames are all one size" % (
            sorted(seen),)
    size = seen.pop()
    if size not in FOLLOWER_SIZES:
        return "%dx%d frames, and a follower is 32 or 64" % (size, size)
    if tuple(sorted(palettes)) != FOLLOWER_PALETTES:
        return "palettes %s, not the normal/shiny pair %s" % (
            tuple(sorted(palettes)), FOLLOWER_PALETTES)
    return size


def _walk_sequence(mmodel: list[bytes]):
    """Find the billboard gfx sequence a follower walks on."""
    import struct
    found = []
    for i, blob in enumerate(mmodel):
        if blob[:4] in (b"BTX0", b"BMD0") or len(blob) < 8:
            continue
        n = struct.unpack_from("<I", blob, 0)[0]
        if not 1 <= n <= 64 or len(blob) < 4 + 4 * n:
            continue
        tex = list(blob[4 + 2 * n:4 + 3 * n])
        if n != 16 or len(set(tex)) != FOLLOWER_FRAMES:
            continue
        groups = [tex[g:g + 4] for g in range(0, 16, 4)]
        if all(g[0] == g[2] and g[1] == g[3] and g[0] != g[1] for g in groups):
            found.append(i)
    if len(found) == 1:
        return found[0], "16 steps, four facings of a two-frame cycle"
    if not found:
        return None, ("no mmodel member is the follower's walk sequence "
                      "(four facings of a two-frame cycle over 8 textures)")
    return None, ("%d mmodel members look like the follower's walk sequence "
                  "(%s); it is meant to be unique" % (len(found), found))


def _engine_pc() -> Path | None:
    root = decomp_dir("pokeplatinum")
    if root is None or not (root / "pc").is_dir():
        return None
    return root / "pc"


def main(argv: list[str]) -> int:
    args = argv[1:]
    broken = None
    if "--break" in args:
        at = args.index("--break")
        if at + 1 >= len(args):
            die("--break needs a kind; try %s" % ", ".join(sorted(BREAKS)))
        broken = args[at + 1]
        if broken not in BREAKS:
            die("no --break called %r; try %s"
                % (broken, ", ".join(sorted(BREAKS))))
        args = args[:at] + args[at + 2:]

    if args and args[0] == "--verify":
        if len(args) < 2:
            die("--verify needs a cartridge path")
        rom = Path(args[1])
        if not rom.is_file():
            die("no cartridge at %s" % rom)
        hg = Path(args[2]) if len(args) > 2 else decomp_dir("pokeheartgold")
        if hg is None:
            die("need a heartgold checkout; pass one or set DECOMP_DIR")
        return verify(rom, hg, broken)

    if broken is not None:
        die("--break only means something with --verify")

    header = False
    if args and args[0] == "--header":
        header = True
        args = args[1:]

    if header:
        out = Path(args[0]) if args else MMO / "src" / "follower_index.gen.h"
        hg = Path(args[1]) if len(args) > 1 else decomp_dir("pokeheartgold")
        if hg is None:
            die("need a heartgold checkout; pass one or set DECOMP_DIR")
        rows, expansion, base, lo, hi, _spare = build(hg)
        write_header(out, rows, expansion, base)
        print("gen_followers: %d species, gfx %d..%d -> %s"
              % (len(rows), FOLLOWER_GFX_BASE,
                 FOLLOWER_GFX_BASE + len(expansion) * 2 - 1, out))
        return 0

    hg = Path(args[0]) if args else decomp_dir("pokeheartgold")
    out = Path(args[1]) if len(args) > 1 else MMO / "FOLLOWERS"
    if hg is None:
        die("need a heartgold checkout; pass one or set DECOMP_DIR")

    rows, expansion, base, lo, hi, _spare = build(hg)
    write(out, rows, expansion, base, lo, hi)
    print("gen_followers: %d species, %d sprite ids %d..%d -> %s"
          % (len(rows), len(expansion), lo, hi, out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
