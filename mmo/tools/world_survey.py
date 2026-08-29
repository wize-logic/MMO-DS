#!/usr/bin/env python3
"""world_survey.py, measure the two field-data worlds this project straddles."""

import argparse
import glob
import json
import os
import re
import struct
import sys

# The four little-endian ints every land-data chunk opens with, in file order.
LAND_FIELDS = ("terrain", "unknown1", "model", "props")

# The defines the platinum field code reads a terrain attribute through.  Absent
# any one of them we do not know how to decode the plane, and guessing at it is
# the expensive kind of wrong.
NEEDED_DEFINES = (
    "TERRAIN_ATTRIBUTES_OFFSET",
    "TERRAIN_ATTRIBUTES_SIZE",
    "TERRAIN_ATTRIBUTES_COLLISION_SHIFT",
    "TERRAIN_ATTRIBUTES_COLLISION_MASK",
    "TERRAIN_ATTRIBUTES_TILE_BEHAVIOR_MASK",
)

# Sentinels in the map-header enum that are not rows of the header table.
# MAP_HEADER_DYNAMIC is a real warp destination: it means "wherever the running
# script last set", which the server's map model already carries as a flag.
DYNAMIC_HEADER = "MAP_HEADER_DYNAMIC"

EVENT_KINDS = ("warp_events", "object_events", "bg_events", "coord_events")


class Failure(Exception):
    pass


def read_defines(path):
    """The engine's own numbers for the terrain plane, or a hard stop."""
    text = open(path, encoding="utf-8").read()
    out = {}
    for name in NEEDED_DEFINES:
        m = re.search(r"^#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)\s*$" % name, text, re.M)
        if not m:
            raise Failure(
                "%s no longer defines %s, the terrain plane cannot be decoded "
                "without it and a guess here is not worth the bytes it saves" % (path, name)
            )
        out[name] = int(m.group(1), 0)
    return out


def read_headers(path):
    """The map-header table as {name: {field: value}}, from its own initializer."""
    text = open(path, encoding="utf-8").read()
    rows = re.findall(r"\[(MAP_HEADER_\w+)\]\s*=\s*\{(.*?)\n    \}", text, re.S)
    if not rows:
        raise Failure("%s parsed to no map headers, its shape has changed" % path)
    return {
        name: dict(re.findall(r"\.(\w+)\s*=\s*([\w\-]+)", body)) for name, body in rows
    }


def matrix_dims(matrix):
    """A matrix is a grid of land-data chunks; `maps` is the one always filled."""
    grid = matrix["maps"]
    return len(grid[0]), len(grid)


def survey_platinum(engine, problems):
    root = os.path.join(engine, "res", "field")
    defines = read_defines(os.path.join(engine, "include", "constants", "field", "map.h"))
    headers = read_headers(os.path.join(engine, "include", "data", "map_headers.h"))

    terrain_off = defines["TERRAIN_ATTRIBUTES_OFFSET"]
    terrain_size = defines["TERRAIN_ATTRIBUTES_SIZE"]
    tiles_per_chunk = terrain_size // 2
    side = int(round(tiles_per_chunk ** 0.5))
    if side * side != tiles_per_chunk:
        raise Failure(
            "a terrain plane of %d bytes is %d tiles, which is not square, this "
            "tool assumes a square chunk everywhere" % (terrain_size, tiles_per_chunk)
        )

    # --- land data: the collision and behaviour planes, and what else is in there.
    chunks = sorted(glob.glob(os.path.join(root, "maps", "data", "map_data_*.bin")))
    if not chunks:
        raise Failure("no land data under %s/maps/data" % root)
    totals = dict.fromkeys(LAND_FIELDS, 0)
    models = 0
    blocked = walkable = 0
    behaviours = set()
    for path in chunks:
        blob = open(path, "rb").read()
        sizes = struct.unpack("<4i", blob[:16])
        if 16 + sum(sizes) != len(blob):
            problems.append(
                "%s: header sizes %s do not account for its %d bytes"
                % (os.path.basename(path), sizes, len(blob))
            )
        if sizes[0] != terrain_size:
            problems.append(
                "%s: terrain plane is %d bytes, the engine says %d"
                % (os.path.basename(path), sizes[0], terrain_size)
            )
            continue
        at = 16
        for name, size in zip(LAND_FIELDS, sizes):
            totals[name] += size
            if name == "model" and size and blob[at:at + 4] == b"BMD0":
                models += 1
            at += size
        plane = struct.unpack("<%dH" % tiles_per_chunk, blob[terrain_off:terrain_off + terrain_size])
        for attr in plane:
            if (attr >> defines["TERRAIN_ATTRIBUTES_COLLISION_SHIFT"]) & (
                defines["TERRAIN_ATTRIBUTES_COLLISION_MASK"]
                >> defines["TERRAIN_ATTRIBUTES_COLLISION_SHIFT"]
            ):
                blocked += 1
            else:
                walkable += 1
            behaviours.add(attr & defines["TERRAIN_ATTRIBUTES_TILE_BEHAVIOR_MASK"])
    if models != len(chunks):
        problems.append(
            "%d of %d land-data chunks carry a BMD0 model where the third field says they do"
            % (models, len(chunks))
        )
    if not blocked or not walkable:
        problems.append(
            "the collision bit reads the same on every tile (%d blocked, %d walkable), "
            "the mask is being applied to the wrong bits" % (blocked, walkable)
        )

    # --- headers, and the archives each one names.
    events = {}
    matrices = {}
    shared = {}
    for name, fields in headers.items():
        ev = os.path.join(root, "events", "%s.json" % fields.get("eventsArchiveID", ""))
        mx = os.path.join(root, "matrices", "%s.json" % fields.get("mapMatrixID", ""))
        if not os.path.exists(ev):
            problems.append("%s names an events archive that is not on disk" % name)
            continue
        if not os.path.exists(mx):
            problems.append("%s names a map matrix that is not on disk" % name)
            continue
        events[name] = json.load(open(ev, encoding="utf-8"))
        matrices[name] = json.load(open(mx, encoding="utf-8"))
        shared.setdefault(fields["mapMatrixID"], []).append(name)

    # --- warps resolve by (destination header, index into its warp list), which is
    # the same shape the GBA reader already resolves, so no new model is needed.
    warps = resolved = dynamic = 0
    for name, ev in events.items():
        for warp in ev.get("warp_events", []):
            warps += 1
            dest = warp["dest_header_id"]
            if dest == DYNAMIC_HEADER:
                dynamic += 1
            elif dest not in events:
                problems.append("%s warps to %s, which has no events archive" % (name, dest))
            elif warp["dest_warp_id"] < len(events[dest].get("warp_events", [])):
                resolved += 1
            else:
                problems.append(
                    "%s warps to %s slot %d, past its %d warps"
                    % (name, dest, warp["dest_warp_id"], len(events[dest].get("warp_events", [])))
                )

    # --- and every event's coordinates are in its matrix, not in a per-map grid.
    # This is the finding the whole decision turns on: 84 headers share the one
    # overworld matrix, so their tile coordinates are global to it.
    counts = dict.fromkeys(EVENT_KINDS, 0)
    placed = 0
    for name, ev in events.items():
        cols, rows = matrix_dims(matrices[name])
        for kind in EVENT_KINDS:
            for event in ev.get(kind, []):
                counts[kind] += 1
                if 0 <= event["x"] < cols * side and 0 <= event["z"] < rows * side:
                    placed += 1
                else:
                    problems.append(
                        "%s has a %s at (%d,%d), outside its %dx%d matrix"
                        % (name, kind, event["x"], event["z"], cols * side, rows * side)
                    )

    biggest = max(shared.items(), key=lambda kv: len(kv[1]))
    cols, rows = matrix_dims(matrices[biggest[1][0]])
    return {
        "defines": defines,
        "headers": len(headers),
        "chunks": len(chunks),
        "side": side,
        "totals": totals,
        "blocked": blocked,
        "walkable": walkable,
        "behaviours": len(behaviours),
        "matrices": len(shared),
        "encounters": len(glob.glob(os.path.join(root, "encounters", "*.json"))),
        "scripts": len(glob.glob(os.path.join(root, "scripts", "*.s"))),
        "warps": warps,
        "resolved": resolved,
        "dynamic": dynamic,
        "counts": counts,
        "placed": placed,
        "overworld": (biggest[0], len(biggest[1]), cols * side, rows * side),
    }


def survey_gba(decomp, regions, problems):
    out = {}
    for region, name in regions:
        root = os.path.join(decomp, name)
        layouts_json = os.path.join(root, "data", "layouts", "layouts.json")
        if not os.path.exists(layouts_json):
            continue
        area = 0
        layouts = 0
        for layout in json.load(open(layouts_json, encoding="utf-8"))["layouts"]:
            if not layout or "width" not in layout:
                continue
            layouts += 1
            area += layout["width"] * layout["height"]
        maps = len(glob.glob(os.path.join(root, "data", "maps", "*", "map.json")))
        if not maps:
            problems.append("%s has layouts but no maps" % name)
        out[region] = {"decomp": name, "maps": maps, "layouts": layouts, "area": area}
    return out


def emit_facts(plat, gba, problems):
    """The same measurement as the printed survey, in a shape a checker can read."""
    if plat:
        d = plat["defines"]
        total = sum(plat["totals"].values())
        name, headers_here, width, height = plat["overworld"]
        for key, value in (
            ("headers", plat["headers"]),
            ("chunks", plat["chunks"]),
            ("chunk_side", plat["side"]),
            ("terrain_bytes", d["TERRAIN_ATTRIBUTES_SIZE"]),
            ("terrain_offset", d["TERRAIN_ATTRIBUTES_OFFSET"]),
            ("collision_bit", d["TERRAIN_ATTRIBUTES_COLLISION_SHIFT"]),
            ("walkable", plat["walkable"]),
            ("blocked", plat["blocked"]),
            ("behaviours", plat["behaviours"]),
            ("warps", plat["warps"]),
            ("warps_static", plat["resolved"]),
            ("warps_dynamic", plat["dynamic"]),
            ("objects", plat["counts"]["object_events"]),
            ("bg_events", plat["counts"]["bg_events"]),
            ("coord_events", plat["counts"]["coord_events"]),
            ("events_placed", plat["placed"]),
            ("encounters", plat["encounters"]),
            ("scripts", plat["scripts"]),
            ("overworld_matrix", name),
            ("overworld_headers", headers_here),
            ("overworld_width", width),
            ("overworld_height", height),
            ("model_share", "%.1f" % (100.0 * plat["totals"]["model"] / total)),
            ("terrain_share", "%.1f" % (100.0 * plat["totals"]["terrain"] / total)),
        ):
            print("%s %s" % (key, value))
    chunks = 0
    for region, info in sorted(gba.items()):
        side = plat["side"] if plat else 32
        n = -(-info["area"] // (side * side))
        chunks += n
        print("%s_maps %d" % (region, info["maps"]))
        print("%s_layouts %d" % (region, info["layouts"]))
        print("%s_metatiles %d" % (region, info["area"]))
    if gba:
        print("gba_chunks %d" % chunks)
    for problem in problems:
        print("problem %s" % problem)
    return 1 if problems else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", help="the pokeplatinum checkout the client is built on")
    ap.add_argument("--decomp", help="the directory holding the pret GBA decomps")
    ap.add_argument("--check", action="store_true", help="assert, do not just print")
    ap.add_argument("--facts", action="store_true", help="emit `name value` lines for a checker")
    args = ap.parse_args()

    problems = []
    plat = None
    if args.engine and os.path.isdir(os.path.join(args.engine, "res", "field")):
        plat = survey_platinum(args.engine, problems)
    gba = {}
    if args.decomp:
        gba = survey_gba(args.decomp, (("hoenn", "pokeemerald"), ("kanto", "pokefirered")), problems)

    if args.facts:
        return emit_facts(plat, gba, problems)

    if plat:
        d = plat["defines"]
        side = plat["side"]
        print("sinnoh, as the platinum decomp holds it")
        print("  map headers          %d, over %d matrices" % (plat["headers"], plat["matrices"]))
        print("  land-data chunks     %d, each %dx%d tiles" % (plat["chunks"], side, side))
        print(
            "  terrain plane        %d bytes at offset %d, collision at bit %d, behaviour mask 0x%02X"
            % (d["TERRAIN_ATTRIBUTES_SIZE"], d["TERRAIN_ATTRIBUTES_OFFSET"],
               d["TERRAIN_ATTRIBUTES_COLLISION_SHIFT"], d["TERRAIN_ATTRIBUTES_TILE_BEHAVIOR_MASK"]))
        print(
            "  tiles                %d walkable, %d blocked, %d distinct behaviours"
            % (plat["walkable"], plat["blocked"], plat["behaviours"]))
        total = sum(plat["totals"].values())
        print("  land data            %s" % ", ".join(
            "%s %.1f%%" % (k, 100.0 * v / total) for k, v in plat["totals"].items()))
        print("  events               %s" % ", ".join(
            "%s %d" % (k.replace("_events", ""), v) for k, v in plat["counts"].items()))
        print("  warps                %d, %d resolved statically, %d dynamic, 0 unresolved"
              % (plat["warps"], plat["resolved"], plat["dynamic"]))
        print("  encounter tables     %d; script files %d" % (plat["encounters"], plat["scripts"]))
        name, n, w, h = plat["overworld"]
        print("  overworld            %s carries %d headers over %dx%d tiles" % (name, n, w, h))
    else:
        print("sinnoh: SKIP (no platinum checkout given)")

    if gba:
        print("")
        print("the server's regions, as the pret decomps hold them")
        chunks = 0
        for region, info in sorted(gba.items()):
            side = plat["side"] if plat else 32
            n = -(-info["area"] // (side * side))
            chunks += n
            print("  %-8s %-13s %d maps, %d layouts, %d metatiles = %d %dx%d chunks"
                  % (region, info["decomp"], info["maps"], info["layouts"], info["area"], n, side, side))
        print("  drawing these on this engine needs %d new chunks, each with a model" % chunks)
    else:
        print("")
        print("gba regions: SKIP (no decomp directory given)")

    if args.check:
        if not plat and not gba:
            print("")
            print("world: SKIP (nothing to measure)")
            return 0
        print("")
        for problem in problems:
            print("  FAIL %s" % problem)
        if problems:
            print("world: %d checks failed" % len(problems))
            return 1
        print("world: the two trees hold what this survey says they do")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Failure as exc:
        print("world: %s" % exc, file=sys.stderr)
        sys.exit(2)
