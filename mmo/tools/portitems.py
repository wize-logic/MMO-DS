"""portitems.py, fill a package with the items HeartGold has past this game's table."""
from __future__ import annotations

import re
import struct
from pathlib import Path

MSG_TABLE_KEY = 0x2FD
MSG_STRING_KEY = 0x91BD3
MSG_STRING_STEP = 0x493D
MSG_TERMINATOR = 0xFFFF

HG_ITEM_DATA = "a/0/1/7"        # itemtool/itemdata/item_data.narc, unnamed in the NitroFS
HG_ITEM_ICON = "a/0/1/8"        # itemtool/itemdata/item_icon.narc
HG_ITEM_BANKS = (221, 222, 223, 224)   # descriptions, names, and two more (see match_banks)

PL_ITEM_DATA = "itemtool/itemdata/pl_item_data.narc"
PL_ITEM_ICON = "itemtool/itemdata/item_icon.narc"
PL_MSG = "msgdata/pl_msg.narc"
PL_BANK_DESCRIPTIONS = 391
PL_BANK_NAMES = 392
PL_BANK_ARTICLES = 393
PL_BANK_PLURAL = 394
PL_LAST_ITEM = 467              # this game's last item; MAX_ITEMS is one past
ROW_BYTES = 34


class Refused(Exception):
    pass


def read_bank(blob: bytes) -> tuple[int, list[list[int]]]:
    count, seed = struct.unpack_from("<HH", blob, 0)
    key = (MSG_TABLE_KEY * seed) & 0xFFFF
    out = []
    for i in range(count):
        pair = (key * (i + 1)) & 0xFFFF
        mask = pair | (pair << 16)
        offset, length = struct.unpack_from("<II", blob, 4 + i * 8)
        offset ^= mask
        length ^= mask
        if offset + length * 2 > len(blob):
            raise Refused("message %d of a bank runs past its end" % i)
        text = struct.unpack_from("<%dH" % length, blob, offset)
        char_key = (MSG_STRING_KEY * (i + 1)) & 0xFFFF
        chars = []
        for char in text:
            chars.append(char ^ char_key)
            char_key = (char_key + MSG_STRING_STEP) & 0xFFFF
        out.append(chars)
    return seed, out


def write_bank(seed: int, messages: list[list[int]]) -> bytes:
    out = bytearray(4 + len(messages) * 8)
    struct.pack_into("<HH", out, 0, len(messages), seed)
    key = (MSG_TABLE_KEY * seed) & 0xFFFF
    for i, chars in enumerate(messages):
        pair = (key * (i + 1)) & 0xFFFF
        mask = pair | (pair << 16)
        struct.pack_into("<II", out, 4 + i * 8, len(out) ^ mask, len(chars) ^ mask)
        char_key = (MSG_STRING_KEY * (i + 1)) & 0xFFFF
        for char in chars:
            out += struct.pack("<H", char ^ char_key)
            char_key = (char_key + MSG_STRING_STEP) & 0xFFFF
    return bytes(out)


def hg_archive_table(hg: Path) -> dict[int, tuple[int, int, int]]:
    """HeartGold item id -> (data member, icon NCGR member, icon NCLR member),
    out of src/item.c's sItemNarcIds and include/constants/items.h."""
    ids = {}
    for line in (hg / "include/constants/items.h").read_text().splitlines():
        m = re.match(r"#define (ITEM_\w+)\s+(\d+)\b", line)
        if m:
            ids[m.group(1)] = int(m.group(2))
    out = {}
    row = re.compile(r"\[(ITEM_\w+)\]\s*=\s*\{\s*NARC_item_data_(\d+)_bin\s*,\s*"
                     r"NARC_item_icon_item_icon_(\d+)_NCGR\s*,\s*"
                     r"NARC_item_icon_item_icon_(\d+)_NCLR")
    for m in row.finditer((hg / "src/item.c").read_text()):
        if m.group(1) in ids:
            out[ids[m.group(1)]] = (int(m.group(2)), int(m.group(3)), int(m.group(4)))
    if len(out) < 500:
        raise Refused("HeartGold's sItemNarcIds parsed to %d rows" % len(out))
    return out


def pl_archive_table(engine: Path) -> dict[int, tuple[int, int, int]]:
    """This game's item id -> (data member, NCGR, NCLR), out of the build's
    generated item_id_map.h and item_icon.naix."""
    naix = {}
    for line in (engine / "build/rom/res/items/item_icon.naix").read_text().splitlines():
        m = re.match(r"#define (\w+)\s+(\d+)\b", line)
        if m:
            naix[m.group(1)] = int(m.group(2))
    text = (engine / "build/rom/res/items/item_id_map.h").read_text()
    out = {}
    row = re.compile(r"\[(\d+)\]\s*=\s*\{\s*\.dataID\s*=\s*(\d+)\s*,\s*\.iconID\s*=\s*(\w+)\s*,"
                     r"\s*\.paletteID\s*=\s*(\w+)", re.S)
    for m in row.finditer(text):
        out[int(m.group(1))] = (int(m.group(2)), naix[m.group(3)], naix[m.group(4)])
    if len(out) != PL_LAST_ITEM + 1:
        raise Refused("this game's item_id_map.h parsed to %d rows, not %d"
                      % (len(out), PL_LAST_ITEM + 1))
    return out


def decode(chars: list[int], codes: dict[int, str]) -> str:
    return "".join(codes.get(c, "?") for c in chars if c != MSG_TERMINATOR)


def match_banks(hg_banks: dict[int, list], pl_banks: dict[int, list]) -> dict[int, int]:
    """Which HeartGold bank is which of this game's four: the pairing that
    agrees on every shared item's entry. Neither tree names them."""
    pairing = {}
    for pl_id, pl_entries in pl_banks.items():
        best = None
        for hg_id, hg_entries in hg_banks.items():
            same = sum(1 for i in range(1, PL_LAST_ITEM + 1)
                       if hg_entries[i] == pl_entries[i])
            if best is None or same > best[1]:
                best = (hg_id, same)
        pairing[pl_id] = best
    return pairing


def fill(rom_members_of, pl_members_of, hg: Path, engine: Path, codes: dict[int, str],
         take_data, take_icon, write_member, write_generated, mmo: Path) -> dict:
    """The fill, all of it or nothing. `take_*` hand out the next appended
    member of each archive; `write_member(narc, index, blob)` writes a cooked
    member; `write_generated(name, text)` a generated table."""
    hg_table = hg_archive_table(hg)
    pl_table = pl_archive_table(engine)
    hg_rows = rom_members_of(HG_ITEM_DATA)
    hg_icons = rom_members_of(HG_ITEM_ICON)
    pl_rows = pl_members_of(PL_ITEM_DATA)
    pl_icons = pl_members_of(PL_ITEM_ICON)
    pl_msg = pl_members_of(PL_MSG)
    hg_msg = rom_members_of("a/0/2/7")

    # The row oracle: every item both games ship, byte for byte.
    differ = []
    for item in range(1, PL_LAST_ITEM + 1):
        if item not in hg_table or item not in pl_table:
            continue
        a = hg_rows[hg_table[item][0]]
        b = pl_rows[pl_table[item][0]]
        if len(a) != ROW_BYTES or len(b) != ROW_BYTES:
            raise Refused("item %d's row is %d/%d bytes, not %d" % (item, len(a), len(b), ROW_BYTES))
        if a != b:
            differ.append(item)
    if len(differ) > 8:
        raise Refused("%d shared items have different rows in the two games (%s ...); "
                      "the row is not the same row" % (len(differ), differ[:6]))

    # The icon oracle: the same picture for the same item.
    icon_differ = []
    for item in range(1, PL_LAST_ITEM + 1):
        if item not in hg_table or item not in pl_table:
            continue
        if hg_icons[hg_table[item][1]] != pl_icons[pl_table[item][1]]:
            icon_differ.append(item)
    if len(icon_differ) > 40:
        raise Refused("%d shared items draw differently in the two games" % len(icon_differ))

    # The banks: matched by agreement, rewritten unchanged first.
    hg_banks = {}
    for b in HG_ITEM_BANKS:
        seed, entries = read_bank(hg_msg[b])
        hg_banks[b] = entries
    pl_banks = {}
    seeds = {}
    for b in (PL_BANK_DESCRIPTIONS, PL_BANK_NAMES, PL_BANK_ARTICLES, PL_BANK_PLURAL):
        seed, entries = read_bank(pl_msg[b])
        if write_bank(seed, entries) != pl_msg[b]:
            raise Refused("bank %d does not rewrite unchanged" % b)
        if len(entries) != PL_LAST_ITEM + 1:
            raise Refused("bank %d holds %d entries, not %d" % (b, len(entries), PL_LAST_ITEM + 1))
        pl_banks[b] = entries
        seeds[b] = seed
    # Measured 2026-09-03: names, articles and plurals agree on 466 of 467,
    # descriptions on 368, HeartGold reworded 99 of them, and every other
    # pairing agrees on 45 or fewer. So a bank has to agree on most of the
    # range and beat every other bank by a wide margin.
    pairing = match_banks(hg_banks, pl_banks)
    for pl_id, (hg_id, same) in pairing.items():
        others = max(sum(1 for i in range(1, PL_LAST_ITEM + 1)
                         if hg_banks[h][i] == pl_banks[pl_id][i])
                     for h in hg_banks if h != hg_id)
        if same < 300 or same - others < 200:
            raise Refused("this game's item bank %d agrees with HeartGold's %d on %d of %d "
                          "entries and with the next bank on %d; that is not a match"
                          % (pl_id, hg_id, same, PL_LAST_ITEM, others))
    if len({hg for hg, _ in pairing.values()}) != 4:
        raise Refused("two of this game's item banks matched the same HeartGold bank")

    # The fill itself.
    last = max(hg_table)
    rows = []
    for item in range(PL_LAST_ITEM + 1, last + 1):
        if item not in hg_table:
            continue
        name = decode(hg_banks[pairing[PL_BANK_NAMES][0]][item], codes)
        if not name.strip() or name.strip("?") == "":
            continue
        data_m, ncgr_m, nclr_m = hg_table[item]
        dst_data = take_data()
        write_member(PL_ITEM_DATA, dst_data, hg_rows[data_m])
        dst_ncgr = take_icon()
        write_member(PL_ITEM_ICON, dst_ncgr, hg_icons[ncgr_m])
        dst_nclr = take_icon()
        write_member(PL_ITEM_ICON, dst_nclr, hg_icons[nclr_m])
        rows.append((item, dst_data, dst_ncgr, dst_nclr, name))
    if not rows:
        raise Refused("HeartGold has no item past %d to fill" % PL_LAST_ITEM)
    grown_to = rows[-1][0] + 1
    for pl_id in (PL_BANK_DESCRIPTIONS, PL_BANK_NAMES, PL_BANK_ARTICLES, PL_BANK_PLURAL):
        hg_id = pairing[pl_id][0]
        grown = list(pl_banks[pl_id])
        for item in range(PL_LAST_ITEM + 1, grown_to):
            grown.append(list(hg_banks[hg_id][item]) if item < len(hg_banks[hg_id])
                         else [MSG_TERMINATOR])
        write_member(PL_MSG, pl_id, write_bank(seeds[pl_id], grown))
    write_generated("items_fill.txt",
                    "# <item id> <pl_item_data member> <icon NCGR member> <icon NCLR member>\n"
                    + "".join("%d %d %d %d\n" % r[:4] for r in rows))
    (mmo / "ITEM_FILL").write_text(
        "# ITEM_FILL, GENERATED by tools/portitems.py; DO NOT EDIT.\n"
        "#\n"
        "# The items a HeartGold fill appends past this game's own 467, by the\n"
        "# id both games give them and the name HeartGold's own bank prints.\n"
        "# tools/gen_idmap_items.py reads it so the client's item id map\n"
        "# carries them; the server's table is numbered the same way.\n"
        "#\n"
        "# Rows: <item id> <name>\n\n"
        + "".join("%d %s\n" % (r[0], r[4]) for r in rows))
    return dict(rows=len(rows), first=rows[0][0], last=rows[-1][0],
                row_diffs=differ, icon_diffs=len(icon_differ),
                banks={pl: hg for pl, (hg, _s) in pairing.items()},
                names=[r[4] for r in rows])
