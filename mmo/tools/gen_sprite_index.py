#!/usr/bin/env python3
"""Generate the client's sprite-archive index tables from the engine's own code."""
import os
import re
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ENGINE = os.environ.get(
    "ENGINE_DIR", os.path.join(REPO, "engine", "pokeplatinum")
)

SPECIES_TXT = "generated/species.txt"
GENDERS_TXT = "generated/genders.txt"
FORMS_H = "include/constants/forms.h"
POKEMON_H = "include/pokemon.h"
POKEMON_C = "src/pokemon.c"
POKEGRA_NARC = "build/rom/res/pokemon/pl_pokegra.narc"
OTHERPOKE_NARC = "build/rom/res/pokemon/pl_otherpoke.narc"

# The default arm, verbatim. It is transcribed into src/sprite.c rather than
# generated, it is two lines and encoding them would cost more than it saves,
# so the check that it has not moved is that this text is still in the function.
DEFAULT_ARM = (
    "spriteTemplate->character = species * 6 + face + (gender != GENDER_FEMALE ? 1 : 0);",
    "spriteTemplate->palette = species * 6 + 4 + shiny;",
)
POKEGRA_STRIDE = 6  # members per species in the default arm, from that first line


def die(msg):
    sys.stderr.write("gen_sprite_index: %s\n" % msg)
    sys.exit(1)


def read_ids(path, what):
    """One constant per line, in id order."""
    ids = {}
    with open(path) as fh:
        for i, line in enumerate(fh):
            name = line.strip()
            if name:
                ids[name] = i
    if not ids:
        die("%s named no %s" % (path, what))
    return ids


def read_defines(path, names):
    """A handful of plain `#define NAME <int>` values, all of them required."""
    found = {}
    with open(path) as fh:
        for line in fh:
            m = re.match(r"\s*#define\s+(\w+)\s+(\d+)\s*$", line)
            if m and m.group(1) in names:
                found[m.group(1)] = int(m.group(2))
    missing = [n for n in names if n not in found]
    if missing:
        die("%s no longer defines %s" % (path, ", ".join(missing)))
    return found


def read_form_counts(path):
    counts = {}
    with open(path) as fh:
        for line in fh:
            m = re.match(r"\s*#define\s+(\w+)_FORM_COUNT\s+(\d+)\s*$", line)
            if m:
                counts[m.group(1)] = int(m.group(2))
    if not counts:
        die("%s defined no *_FORM_COUNT" % path)
    return counts


def function_body(text, signature, path):
    """The braced body of a function, by its opening line."""
    start = text.find(signature)
    if start < 0:
        die("%s no longer defines `%s`" % (path, signature))
    start = text.index("{", start)
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start : i + 1]
    die("%s: unterminated body for `%s`" % (path, signature))


def read_sanitizer(body, species_ids, form_counts, path):
    """Pokemon_SanitizeFormId: species -> how many forms it has."""
    forms = {}
    species = None
    for line in body.splitlines():
        m = re.match(r"\s*case\s+(SPECIES_\w+):", line)
        if m:
            species = m.group(1)
            continue
        m = re.match(r"\s*if\s*\(\s*monForm\s*(>=?)\s*(\w+)_FORM_COUNT\s*(- 1\s*)?\)", line)
        if m:
            if species is None:
                die("%s: a form clamp with no case above it" % path)
            op, macro, minus = m.group(1), m.group(2), m.group(3)
            if (op == ">") != bool(minus):
                die("%s: %s clamps with `%s` and %s `- 1`"
                    % (path, species, op, "with" if minus else "without"))
            if macro not in form_counts:
                die("%s: %s_FORM_COUNT is not in %s" % (path, macro, FORMS_H))
            if species not in species_ids:
                die("%s: %s is not a species id" % (path, species))
            forms[species_ids[species]] = form_counts[macro]
            species = None
    if not forms:
        die("%s: Pokemon_SanitizeFormId clamped nothing" % path)
    return forms


# character = base + face_mul * (face / face_div) + form_mul * form
CHARACTER_SHAPES = [
    (r"(\d+) \+ \(face / 2\) \+ form \* 2", lambda m: (int(m.group(1)), 1, 2, 2)),
    (r"(\d+) \+ face \+ form", lambda m: (int(m.group(1)), 1, 1, 1)),
    (r"(\d+) \+ \(face \* 2\) \+ form", lambda m: (int(m.group(1)), 2, 1, 1)),
    (r"(\d+) \+ form", lambda m: (int(m.group(1)), 0, 1, 1)),
    (r"(\d+)", lambda m: (int(m.group(1)), 0, 1, 0)),
]

# palette = base + shiny_mul * shiny + form_mul * form
PALETTE_SHAPES = [
    (r"(\d+) \+ shiny \+ form \* 2", lambda m: (int(m.group(1)), 1, 2)),
    (r"(\d+) \+ \(shiny \* (\d+)\) \+ form", lambda m: (int(m.group(1)), int(m.group(2)), 1)),
    (r"(\d+) \+ shiny", lambda m: (int(m.group(1)), 1, 0)),
    (r"(\d+) \+ form", lambda m: (int(m.group(1)), 0, 1)),
    (r"(\d+)", lambda m: (int(m.group(1)), 0, 0)),
]


def match_shape(expr, shapes, what, species, path):
    for pattern, take in shapes:
        m = re.match(pattern + r"\s*$", expr)
        if m:
            return take(m)
    die("%s: %s's %s expression `%s` is a shape this generator does not know. "
        "Teach it the shape rather than approximating one, a wrong base draws "
        "the wrong Pokemon and no test here can see it." % (path, species, what, expr))


def read_special_cases(body, species_ids, path):
    """The fourteen cases that index pl_otherpoke, each with its own arithmetic."""
    cases = []
    pending = []
    narc = character = palette = None
    for line in body.splitlines():
        m = re.match(r"\s*case\s+(SPECIES_\w+):", line)
        if m:
            pending.append(m.group(1))
            continue
        m = re.match(r"\s*spriteTemplate->narcID = NARC_INDEX_(\w+);", line)
        if m:
            narc = m.group(1)
            continue
        m = re.match(r"\s*spriteTemplate->character = (.+);\s*$", line)
        if m:
            character = m.group(1)
            continue
        m = re.match(r"\s*spriteTemplate->palette = (.+);\s*$", line)
        if m:
            palette = m.group(1)
            continue
        if re.match(r"\s*break;", line) and pending:
            if narc != "POKETOOL__POKEGRA__PL_OTHERPOKE":
                die("%s: %s is special-cased into %s, which this client's "
                    "archive ids do not cover" % (path, pending[0], narc))
            if character is None or palette is None:
                die("%s: %s sets no character or no palette" % (path, pending[0]))
            for name in pending:
                if name not in species_ids:
                    die("%s: %s is not a species id" % (path, name))
                cb, cface, cfdiv, cform = match_shape(
                    character, CHARACTER_SHAPES, "character", name, path)
                pb, pshiny, pform = match_shape(
                    palette, PALETTE_SHAPES, "palette", name, path)
                cases.append((species_ids[name], name, cb, cface, cfdiv, cform,
                              pb, pshiny, pform))
            pending, narc, character, palette = [], None, None, None
    if not cases:
        die("%s: BuildPokemonSpriteTemplate special-cased nothing" % path)
    return sorted(cases)


def narc_members(path):
    """A NARC's member count: the BTAF chunk's first word, after the header."""
    with open(path, "rb") as fh:
        head = fh.read(0x1C)
    if len(head) < 0x1C or head[:4] != b"NARC" or head[0x10:0x14] != b"BTAF":
        die("%s is not a NARC with a BTAF chunk where one belongs" % path)
    return struct.unpack("<I", head[0x18:0x1C])[0]


def emit(out, species_max, egg, bad_egg, pokegra, otherpoke, forms, cases,
         genders, faces, spinda):
    w = out.write
    w("""/* Generated by tools/gen_sprite_index.py; Do not edit. */

#define MMO_SPRITE_SPECIES_MAX       %d  /* last species pl_pokegra holds */
#define MMO_SPRITE_EGG               %d
#define MMO_SPRITE_BAD_EGG           %d
#define MMO_SPRITE_POKEGRA_STRIDE    %d
#define MMO_SPRITE_POKEGRA_MEMBERS   %d
#define MMO_SPRITE_OTHERPOKE_MEMBERS %d
#define MMO_SPRITE_GENDER_FEMALE     %d  /* the id the default arm compares against */
#define MMO_SPRITE_FACE_BACK         %d
#define MMO_SPRITE_FACE_FRONT        %d
#define MMO_SPRITE_SPINDA            %d  /* the one front sprite drawn from personality */

/* species -> how many forms the engine will draw for it */
static const struct {
    short species;
    short forms;
} MMO_SPRITE_FORMS[] = {
""" % (species_max, egg, bad_egg, POKEGRA_STRIDE, pokegra, otherpoke,
       genders["GENDER_FEMALE"], faces["FACE_BACK"], faces["FACE_FRONT"],
       spinda))
    for species, count in sorted(forms.items()):
        w("    { %3d, %2d },\n" % (species, count))
    w("};\n#define MMO_SPRITE_FORMS_COUNT %d\n\n" % len(forms))
    w("""/* The pl_otherpoke cases:
 *   character = char_base + char_face * (face / char_face_div) + char_form * form
 *   palette   = pal_base  + pal_shiny * shiny                  + pal_form  * form */
static const struct {
    short species;
    short char_base, char_face, char_face_div, char_form;
    short pal_base, pal_shiny, pal_form;
} MMO_SPRITE_OTHERPOKE[] = {
""")
    width = max(len(c[1]) for c in cases)
    for (species, name, cb, cface, cfdiv, cform, pb, pshiny, pform) in cases:
        w("    { %3d, %3d, %d, %d, %d, %3d, %d, %d }, /* %-*s */\n"
          % (species, cb, cface, cfdiv, cform, pb, pshiny, pform, width, name))
    w("};\n#define MMO_SPRITE_OTHERPOKE_COUNT %d\n" % len(cases))


def main(argv):
    engine = argv[1] if len(argv) > 1 else ENGINE
    out_path = argv[2] if len(argv) > 2 else os.path.join(
        REPO, "mmo", "src", "sprite_index.gen.h")

    species_ids = read_ids(os.path.join(engine, SPECIES_TXT), "species")
    genders = read_ids(os.path.join(engine, GENDERS_TXT), "genders")
    if "GENDER_FEMALE" not in genders:
        die("%s has no GENDER_FEMALE" % GENDERS_TXT)
    form_counts = read_form_counts(os.path.join(engine, FORMS_H))
    faces = read_defines(os.path.join(engine, POKEMON_H),
                         ("FACE_BACK", "FACE_FRONT"))

    pokemon_c = os.path.join(engine, POKEMON_C)
    with open(pokemon_c) as fh:
        text = fh.read()
    sanitize = function_body(text, "u8 Pokemon_SanitizeFormId(", pokemon_c)
    build = function_body(
        text, "void BuildPokemonSpriteTemplate(PokemonSpriteTemplate *spriteTemplate",
        pokemon_c)
    for line in DEFAULT_ARM:
        if line not in build:
            die("BuildPokemonSpriteTemplate's default arm has changed: `%s` is "
                "no longer in it, and src/sprite.c transcribes it" % line)

    forms = read_sanitizer(sanitize, species_ids, form_counts, pokemon_c)
    cases = read_special_cases(build, species_ids, pokemon_c)

    for name in ("SPECIES_EGG", "SPECIES_BAD_EGG", "SPECIES_SPINDA"):
        if name not in species_ids:
            die("%s no longer names %s" % (SPECIES_TXT, name))
    egg, bad_egg = species_ids["SPECIES_EGG"], species_ids["SPECIES_BAD_EGG"]
    species_max = egg - 1

    pokegra = narc_members(os.path.join(engine, POKEGRA_NARC))
    otherpoke = narc_members(os.path.join(engine, OTHERPOKE_NARC))
    if pokegra != (species_max + 1) * POKEGRA_STRIDE:
        die("pl_pokegra holds %d members, which is not %d species x %d: the "
            "default arm's stride and the archive disagree"
            % (pokegra, species_max + 1, POKEGRA_STRIDE))
    reach = max(max(cb + cface * (2 // cfdiv) + cform * (forms.get(sp, 1) - 1),
                    pb + pshiny + pform * (forms.get(sp, 1) - 1))
                for (sp, _n, cb, cface, cfdiv, cform, pb, pshiny, pform) in cases)
    if reach >= otherpoke:
        die("a pl_otherpoke case reaches member %d and the archive holds %d"
            % (reach, otherpoke))

    with open(out_path, "w") as fh:
        emit(fh, species_max, egg, bad_egg, pokegra, otherpoke, forms, cases,
             genders, faces, species_ids["SPECIES_SPINDA"])
    sys.stderr.write(
        "gen_sprite_index: %d species in pl_pokegra (%d members), %d special "
        "cases in pl_otherpoke (%d members, reaching %d), %d form clamps -> %s\n"
        % (species_max + 1, pokegra, len(cases), otherpoke, reach, len(forms),
           out_path))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
