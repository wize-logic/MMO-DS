#!/usr/bin/env python3
"""table_diff.py, walk the id tables the client and the server both hold."""

import argparse
import os
import re
import sys

# The two halves, per class: which engine constant list the client numbers by,
# and which codegen render the server holds. The server paths are relative to
# codegen/build/generated/source.
SERVER_ROOT = "codegen/build/generated/source"

SPECIES_KT = "pokemon/kotlin/de/fiereu/openmmo/pokemon/generated/GeneratedSpecies.kt"
MOVES_KT = "moves/kotlin/de/fiereu/openmmo/moves/generated/GeneratedMoves.kt"
ITEMS_KT = "item/kotlin/de/fiereu/openmmo/items/generated/Items.kt"
MAPS_KT = "maps/kotlin/de/fiereu/openmmo/maps/generated/GeneratedMaps.kt"
STORY_DIR = "story/kotlin/de/fiereu/openmmo/story/generated"

# The wire block the server's item ids sit in: shared item 1 is 5001 there.
# 6.8.4 measured that; idmap_items.gen.h is built on it.
ITEM_REGION_BLOCK = 5000

CLASSES = ("species", "moves", "items", "maps", "flags")
KINDS = ("missing", "extra", "named")

# Rows of a table that are not a thing of that class, so neither half holds one.
NOT_AN_ENTRY = {
    "species": {"NONE", "EGG", "BAD_EGG"},
    "moves": {"NONE"},
    "items": {"NONE"},
}


def normalize(name):
    """One name in two houses: SPECIES_NIDORAN_F and NIDORAN(f) are one species."""
    folded = name.upper().replace("♀", "F").replace("♂", "M")
    folded = folded.replace("É", "E")
    return re.sub(r"[^A-Z0-9]", "", folded)


def enum_table(path, prefix):
    """The engine's generated constant list, read the way codegen reads it."""
    table = {}
    with open(path, encoding="utf-8") as handle:
        for index, line in enumerate(handle):
            line = line.strip()
            if not line or "=" in line:
                continue
            # The trailing count sentinel is the table's length, not a member.
            if line.startswith("MAX_") or line.endswith("_COUNT"):
                continue
            table[index] = line[len(prefix):] if line.startswith(prefix) else line
    if not table:
        raise SystemExit("table_diff: %s parsed as empty" % path)
    return table


def run_table(path):
    """A list the decomp compiles into a C enum, where a value is a running count."""
    table = {}
    by_name = {}
    value = 0
    with open(path, encoding="utf-8") as handle:
        for raw in handle:
            if raw.startswith("#"):
                continue
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            name, _, assigned = (part.strip() for part in line.partition("="))
            if assigned:
                value = int(assigned) if assigned.lstrip("-").isdigit() else by_name[assigned]
            by_name[name] = value
            table.setdefault(value, name)
            value += 1
    if not table:
        raise SystemExit("table_diff: %s parsed as empty" % path)
    return table


def kotlin_rows(path, pattern, what):
    """id -> name out of a codegen render, refusing to read one as empty."""
    if not os.path.isfile(path):
        raise SystemExit("table_diff: no %s table at %s" % (what, path))
    with open(path, encoding="utf-8") as handle:
        rows = {int(i): n for i, n in re.findall(pattern, handle.read())}
    if not rows:
        raise SystemExit(
            "table_diff: no %s rows in %s, the render moved and this is blind" % (what, path)
        )
    return rows


def read_shared_items(root):
    """The client's committed shared-item table: the ids both worlds do hold."""
    path = os.path.join(root, "src/idmap_items.gen.h")
    ids = []
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            match = re.match(r"\s*(\d+), /\*", line)
            if match:
                ids.append(int(match.group(1)))
    if not ids:
        raise SystemExit("table_diff: no ids in %s" % path)
    return set(ids)


def load_classes(engine_dir, root):
    """Both sides of every class, or the reason one of them cannot be read."""
    server_root = os.path.normpath(os.path.join(root, "..", SERVER_ROOT))
    generated = os.path.join(engine_dir, "generated")
    if not os.path.isdir(generated):
        return None, "no engine constant tables at %s" % generated
    if not os.path.isdir(server_root):
        return None, (
            "no codegen renders at %s, build the server once "
            "(./gradlew :codegen:compileKotlin)" % server_root
        )

    def engine_table(name, prefix):
        return enum_table(os.path.join(generated, name), prefix)

    def server_file(relative):
        return os.path.join(server_root, relative)

    classes = {}
    classes["species"] = (
        engine_table("species.txt", "SPECIES_"),
        kotlin_rows(
            server_file(SPECIES_KT), r'SpeciesDef\(id = (\d+), name = "([^"]*)"', "species"
        ),
        None,
    )
    classes["moves"] = (
        engine_table("moves.txt", "MOVE_"),
        kotlin_rows(server_file(MOVES_KT), r'MoveDef\(id = (\d+), name = "([^"]*)"', "move"),
        None,
    )
    # Items are the one class where the two worlds are not meant to hold the same
    # set: the server speaks a Gen 5 table and the engine a Gen 4 one. The
    # contract between them is the committed shared table, so that is the walk.
    items = kotlin_rows(
        server_file(ITEMS_KT), r'/\*\* (\d+) \*/\s*\n\s*val \w+ = item\("([^"]*)"', "item"
    )
    classes["items"] = (
        engine_table("items.txt", "ITEM_"),
        {wire - ITEM_REGION_BLOCK: name for wire, name in items.items()},
        read_shared_items(root),
    )
    # The server registers a map by name rather than by the engine's header
    # index, so this class is a coverage walk: a header with no map behind it is
    # a place the client can stand and the server is not hosting.
    header = engine_table("map_headers.txt", "MAP_HEADER_")
    hosted = set(
        re.findall(
            r"maps\.generated\.sinnoh\.bank\d+\.(\w+)\)",
            read_text(server_file(MAPS_KT)),
        )
    )
    if not hosted:
        raise SystemExit("table_diff: no sinnoh maps in %s" % server_file(MAPS_KT))
    classes["maps"] = (header, {i: n for i, n in header.items() if n.lower() in hosted}, None)
    # Flags and vars: the story generator reads the DS decomp's own list as well
    # now, so this class asks whether the two halves number it the same way. Its
    # values are a run rather than positions, which is why it does not go
    # through enum_table.
    flags = run_table(os.path.join(generated, "vars_flags.txt"))
    classes["flags"] = (flags, sinnoh_story(server_file(STORY_DIR), flags), None)
    return classes, None


def read_text(path):
    if not os.path.isfile(path):
        raise SystemExit("table_diff: no table at %s" % path)
    with open(path, encoding="utf-8") as handle:
        return handle.read()


def sinnoh_story(story_dir, flags):
    """The engine flag and var names the server has a sinnoh constant for."""
    directory = os.path.join(story_dir, "sinnoh")
    if not os.path.isdir(directory):
        return {}
    names = set()
    for entry in sorted(os.listdir(directory)):
        names.update(re.findall(r'const val (\w+) = "', read_text(os.path.join(directory, entry))))
    return {i: n for i, n in flags.items() if n in names}


def compare(engine, server, restrict):
    """The three shapes of disagreement, each as a set of ids."""
    keys = set(engine) & restrict if restrict is not None else set(engine)
    return {
        "missing": {k for k in keys if k not in server},
        "extra": set() if restrict is not None else {k for k in server if k not in engine},
        "named": {
            k for k in keys if k in server and normalize(engine[k]) != normalize(server[k])
        },
    }


def read_ledger(path):
    """mmo/TABLES: every disagreement that is allowed to stand, and why."""
    declared = {}
    with open(path, encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            line = line.split("#")[0].strip()
            if not line:
                continue
            parts = line.split(None, 3)
            if len(parts) < 4:
                raise SystemExit("%s:%d: a row is <class> <kind> <ids> <reason>" % (path, number))
            klass, kind, ids, _reason = parts
            if klass not in CLASSES:
                raise SystemExit("%s:%d: %s is not one of the classes" % (path, number, klass))
            if kind == "absent":
                if ids != "all":
                    raise SystemExit("%s:%d: an absent class covers `all`" % (path, number))
                declared[(klass, kind)] = "all"
                continue
            if kind not in KINDS:
                raise SystemExit("%s:%d: %s is not a kind" % (path, number, kind))
            declared.setdefault((klass, kind), set()).update(expand(path, number, ids))
    return declared


def expand(path, number, ids):
    """`387-493,500` as the set it names."""
    out = set()
    for piece in ids.split(","):
        low, dash, high = piece.partition("-")
        try:
            out.update(range(int(low), int(high) + 1) if dash else [int(low)])
        except ValueError:
            raise SystemExit("%s:%d: %s is not an id or an id range" % (path, number, piece))
    return out


def describe(kind, ident, engine, server):
    if kind == "missing":
        return "engine %s, the server's table has no such id" % engine[ident]
    if kind == "extra":
        return "server %s, the engine's table has no such id" % server[ident]
    return "engine %s / server %s" % (engine[ident], server[ident])


def summarize(found):
    parts = ["%d %s declared" % (len(found[k]), k) for k in KINDS if found[k]]
    return ", ".join(parts) if parts else "the two tables agree entry for entry"


def walk(klass, engine, server, restrict, declared):
    """One class: what the two tables disagree about, against what is declared."""
    ignore = NOT_AN_ENTRY.get(klass, set())
    engine = {i: n for i, n in engine.items() if n not in ignore}
    problems = []

    if declared.get((klass, "absent")) == "all":
        if server:
            problems.append(
                "  FAIL %s: mmo/TABLES calls this class absent, but the server holds %d of them"
                % (klass, len(server))
            )
        else:
            print("  ok   %s: %d ids, no server-side table, declared absent" % (klass, len(engine)))
        return problems

    found = compare(engine, server, restrict)
    for kind in KINDS:
        allowed = declared.get((klass, kind), set())
        undeclared = sorted(found[kind] - allowed)
        stale = sorted(allowed - found[kind])
        if undeclared:
            problems.append(
                "  FAIL %s: %d %s not written down, first is id %d (%s)"
                % (klass, len(undeclared), kind, undeclared[0], describe(kind, undeclared[0], engine, server))
            )
        if stale:
            problems.append(
                "  FAIL %s: mmo/TABLES declares %d %s that no longer are, first is id %d"
                % (klass, len(stale), kind, stale[0])
            )
    if not problems:
        walked = len(set(engine) & restrict) if restrict is not None else len(engine)
        print("  ok   %s: %d ids walked, %s" % (klass, walked, summarize(found)))
    return problems


def main():
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--engine", required=True)
    parser.add_argument("--root", required=True)
    args = parser.parse_args()

    declared = read_ledger(os.path.join(args.root, "TABLES"))
    classes, why = load_classes(args.engine, args.root)
    if classes is None:
        print("tablediff: SKIP (%s)" % why)
        return 0

    print("the id classes the client and the server both hold:")
    problems = []
    for klass in CLASSES:
        engine, server, restrict = classes[klass]
        problems += walk(klass, engine, server, restrict, declared)
    for line in problems:
        print(line)
    if problems:
        print("tablediff: %d disagreement(s) the two halves have not written down" % len(problems))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
