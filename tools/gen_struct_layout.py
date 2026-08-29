# tools/gen_struct_layout.py
#
# CODE_PLAN.md Phase 1.4: "Record struct layouts... Write down, for
# BattlePokemon, the byte offset and width of every field you need. That
# offset table is the contract between the Lua script and the Qt decoder."
#
# BattlePokemon (include/pokemon.h) is already annotated with /*0xNN*/
# byte-offset comments by the decomp itself, so its layout is *extracted*
# from those comments rather than computed -- straight from the source of
# truth, per Phase 1.4's instruction to prefer the decomp's own assertions
# over hand-counting fields.
#
# BoxPokemon / Pokemon / the four PokemonSubstructs (needed for Phase 2.2
# party decryption) have no such comments, so their offsets are *computed*
# with a small ARM/AAPCS-style sequential layout algorithm: each field
# aligns to its own size (max 4 bytes), and a run of consecutive bitfields
# sharing the same declared base type packs LSB-first into one storage unit
# of that width. The algorithm is self-validated below by re-deriving
# BattlePokemon's layout from its field list and asserting it matches the
# offsets extracted from the header comments -- if that assertion ever
# fails, do not trust the computed structs until it's fixed.
#
#   python3 tools/gen_struct_layout.py \
#       > helperapp/config/battle_pokemon_layout_us_rev0.json
import json
import re
import sys

POKEMON_H = "include/pokemon.h"

# Constants referenced by array-length expressions in the struct
# definitions (include/constants/global.h, include/constants/pokemon.h).
CONSTS = {
    "MAX_MON_MOVES": 4,
    "NUM_BATTLE_STATS": 8,
    "POKEMON_NAME_LENGTH": 10,
    "PLAYER_NAME_LENGTH": 7,
}

BASE_SIZE = {"u8": 1, "s8": 1, "u16": 2, "s16": 2, "u32": 4, "s32": 4}

ANNOTATED_FIELD_RE = re.compile(
    r"/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s+"
    r"(u8|s8|u16|s16|u32|s32)\s+"
    r"(\w+)"
    r"(?:\[([^\]]+)\])?"
    r"(?:\s*:\s*(\d+))?"
    r"\s*;"
)


def resolve_array_len(expr):
    if expr is None:
        return 1
    total = 0
    for tok in expr.split("+"):
        tok = tok.strip()
        total += int(tok) if tok.isdigit() else CONSTS[tok]
    return total


def extract_struct_body(header_text, struct_name):
    m = re.search(rf"struct {struct_name}\s*\{{", header_text)
    if not m:
        raise ValueError(f"struct {struct_name} not found in {POKEMON_H}")
    start = m.end()
    depth = 1
    i = start
    while depth:
        if header_text[i] == "{":
            depth += 1
        elif header_text[i] == "}":
            depth -= 1
        i += 1
    return header_text[start:i]


def extract_annotated(header_text, struct_name):
    """Byte offsets taken directly from the struct's own /*0xNN*/ comments."""
    body = extract_struct_body(header_text, struct_name)
    fields = {}
    for m in ANNOTATED_FIELD_RE.finditer(body):
        offset = int(m.group(1), 16)
        base = BASE_SIZE[m.group(2)]
        name = m.group(3)
        count = resolve_array_len(m.group(4))
        bits = int(m.group(5)) if m.group(5) else None
        entry = {"offset": offset, "size": base, "count": count}
        if bits is not None:
            entry["bits"] = bits
        fields[name] = entry
    return fields


def align(x, a):
    return (x + a - 1) // a * a


def layout_sequential(fields):
    """
    fields: ordered list of {name, base, count=1, bits=None}.
    Lays fields out with natural alignment (align to own size, max 4);
    a run of consecutive same-base bitfields packs LSB-first into one
    storage unit of that base's width.
    Returns (offsets_dict, total_size_rounded_to_4).
    """
    offsets = {}
    cur = 0
    i, n = 0, len(fields)
    while i < n:
        f = fields[i]
        if f.get("bits") is not None:
            base = f["base"]
            unit_off = align(cur, base)
            bit_pos = 0
            j = i
            while (
                j < n
                and fields[j].get("bits") is not None
                and fields[j]["base"] == base
                and bit_pos + fields[j]["bits"] <= base * 8
            ):
                # The decomp's own /*0xNN*/ annotations label a bitfield by
                # the byte containing its low bit, not the start of the
                # whole storage unit (see e.g. BattlePokemon's hpIV..
                # spDefenseIV, which walk 0x14 -> 0x17 across one u32 unit).
                # Match that convention so this is directly comparable.
                offsets[fields[j]["name"]] = {
                    "offset": unit_off + bit_pos // 8,
                    "size": base,
                    "count": 1,
                    "bits": fields[j]["bits"],
                    "bitOffset": bit_pos,
                }
                bit_pos += fields[j]["bits"]
                j += 1
            cur = unit_off + base
            i = j
        else:
            off = align(cur, f["base"])
            count = f.get("count", 1)
            offsets[f["name"]] = {"offset": off, "size": f["base"], "count": count}
            cur = off + f["base"] * count
            i += 1
    return offsets, align(cur, 4)


# --- Field lists for the computed (unannotated) structs -------------------

BOX_POKEMON_FIELDS = [
    {"name": "personality", "base": 4},
    {"name": "otId", "base": 4},
    {"name": "nickname", "base": 1, "count": CONSTS["POKEMON_NAME_LENGTH"]},
    {"name": "language", "base": 1},
    {"name": "isBadEgg", "base": 1, "bits": 1},
    {"name": "hasSpecies", "base": 1, "bits": 1},
    {"name": "isEgg", "base": 1, "bits": 1},
    {"name": "blockBoxRS", "base": 1, "bits": 1},
    {"name": "unused", "base": 1, "bits": 4},
    {"name": "otName", "base": 1, "count": CONSTS["PLAYER_NAME_LENGTH"]},
    {"name": "markings", "base": 1},
    {"name": "checksum", "base": 2},
    {"name": "unknown", "base": 2},
    {"name": "secure", "base": 1, "count": 48},  # 4 substructs x 12 bytes
]

POKEMON_TAIL_FIELDS = [
    {"name": "status", "base": 4},
    {"name": "level", "base": 1},
    {"name": "mail", "base": 1},
    {"name": "hp", "base": 2},
    {"name": "maxHP", "base": 2},
    {"name": "attack", "base": 2},
    {"name": "defense", "base": 2},
    {"name": "speed", "base": 2},
    {"name": "spAttack", "base": 2},
    {"name": "spDefense", "base": 2},
]

SUBSTRUCT0_FIELDS = [
    {"name": "species", "base": 2},
    {"name": "heldItem", "base": 2},
    {"name": "experience", "base": 4},
    {"name": "ppBonuses", "base": 1},
    {"name": "friendship", "base": 1},
    {"name": "filler", "base": 2},
]

SUBSTRUCT1_FIELDS = [
    {"name": "moves", "base": 2, "count": CONSTS["MAX_MON_MOVES"]},
    {"name": "pp", "base": 1, "count": CONSTS["MAX_MON_MOVES"]},
]

SUBSTRUCT2_FIELDS = [
    {"name": n, "base": 1}
    for n in (
        "hpEV", "attackEV", "defenseEV", "speedEV", "spAttackEV", "spDefenseEV",
        "cool", "beauty", "cute", "smart", "tough", "sheen",
    )
]

# BattlePokemon's own field list, used only to self-validate layout_sequential
# against the ground-truth annotated offsets extracted from the header.
BATTLE_POKEMON_FIELDS_FOR_VALIDATION = [
    {"name": "species", "base": 2},
    {"name": "attack", "base": 2},
    {"name": "defense", "base": 2},
    {"name": "speed", "base": 2},
    {"name": "spAttack", "base": 2},
    {"name": "spDefense", "base": 2},
    {"name": "moves", "base": 2, "count": CONSTS["MAX_MON_MOVES"]},
    {"name": "hpIV", "base": 4, "bits": 5},
    {"name": "attackIV", "base": 4, "bits": 5},
    {"name": "defenseIV", "base": 4, "bits": 5},
    {"name": "speedIV", "base": 4, "bits": 5},
    {"name": "spAttackIV", "base": 4, "bits": 5},
    {"name": "spDefenseIV", "base": 4, "bits": 5},
    {"name": "isEgg", "base": 4, "bits": 1},
    {"name": "abilityNum", "base": 4, "bits": 1},
    {"name": "statStages", "base": 1, "count": CONSTS["NUM_BATTLE_STATS"]},
    {"name": "ability", "base": 1},
    {"name": "types", "base": 1, "count": 2},
    {"name": "unknown", "base": 1},
    {"name": "pp", "base": 1, "count": CONSTS["MAX_MON_MOVES"]},
    {"name": "hp", "base": 2},
    {"name": "level", "base": 1},
    {"name": "friendship", "base": 1},
    {"name": "maxHP", "base": 2},
    {"name": "item", "base": 2},
    {"name": "nickname", "base": 1, "count": CONSTS["POKEMON_NAME_LENGTH"] + 1},
    {"name": "ppBonuses", "base": 1},
    {"name": "otName", "base": 1, "count": CONSTS["PLAYER_NAME_LENGTH"] + 1},
    {"name": "experience", "base": 4},
    {"name": "personality", "base": 4},
    {"name": "status1", "base": 4},
    {"name": "status2", "base": 4},
    {"name": "otId", "base": 4},
]


def self_validate(annotated_battlemon):
    computed, total = layout_sequential(BATTLE_POKEMON_FIELDS_FOR_VALIDATION)
    if total != 0x58:
        raise AssertionError(f"layout_sequential self-check: size {total:#x} != 0x58")
    for name, expected in annotated_battlemon.items():
        got = computed.get(name)
        if got is None:
            raise AssertionError(f"layout_sequential self-check: missing field {name!r}")
        # Compare the fields the annotated extractor also produces.
        for key in ("offset", "size", "count"):
            if got.get(key) != expected.get(key):
                raise AssertionError(
                    f"layout_sequential self-check: field {name!r} {key} "
                    f"computed={got.get(key)!r} annotated={expected.get(key)!r}"
                )
    print(
        "self-check OK: layout_sequential() reproduces BattlePokemon's "
        "annotated offsets exactly",
        file=sys.stderr,
    )


def main():
    with open(POKEMON_H) as f:
        header_text = f.read()

    battlemon_fields = extract_annotated(header_text, "BattlePokemon")
    self_validate(battlemon_fields)

    box_fields, box_size = layout_sequential(BOX_POKEMON_FIELDS)
    pokemon_fields, pokemon_size = layout_sequential(BOX_POKEMON_FIELDS + POKEMON_TAIL_FIELDS)
    sub0_fields, sub0_size = layout_sequential(SUBSTRUCT0_FIELDS)
    sub1_fields, sub1_size = layout_sequential(SUBSTRUCT1_FIELDS)
    sub2_fields, sub2_size = layout_sequential(SUBSTRUCT2_FIELDS)
    sub3_fields = extract_annotated(header_text, "PokemonSubstruct3")
    # union PokemonSubstruct pads all four substructs to the same size (see
    # its comment in pokemon.h); max(offset+size) over sub3's own bitfields
    # would overcount since "size" there is each field's declared base type
    # width, not its remaining byte span. Substruct0's computed size is that
    # shared size, since PokemonSubstruct0 has no bitfields to obscure it.
    sub3_size = sub0_size

    out = {
        "_comment": (
            "Generated by tools/gen_struct_layout.py from include/pokemon.h. "
            "BattlePokemon and PokemonSubstruct3 are extracted from the "
            "decomp's own /*0xNN*/ offset comments; the rest are computed "
            "with an aligner that is self-validated against BattlePokemon's "
            "annotated layout before being trusted (see script header)."
        ),
        "structs": {
            "BattlePokemon": {"size": 0x58, "source": "annotated", "fields": battlemon_fields},
            "BoxPokemon": {"size": box_size, "source": "computed", "fields": box_fields},
            "Pokemon": {"size": pokemon_size, "source": "computed", "fields": pokemon_fields},
            "PokemonSubstruct0": {"size": sub0_size, "source": "computed", "fields": sub0_fields},
            "PokemonSubstruct1": {"size": sub1_size, "source": "computed", "fields": sub1_fields},
            "PokemonSubstruct2": {"size": sub2_size, "source": "computed", "fields": sub2_fields},
            "PokemonSubstruct3": {"size": sub3_size, "source": "annotated", "fields": sub3_fields},
        },
    }

    json.dump(out, sys.stdout, indent=2, sort_keys=True)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
