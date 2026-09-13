# tools/verify_struct_layout.py
#
# Ground-truth check for helperapp/config/battle_pokemon_layout_us_rev0.json
# (tools/gen_struct_layout.py's output). gen_struct_layout.py lays most
# structs out with its own aligner, self-validated only against
# BattlePokemon's /*0xNN*/ annotations; this script asks the actual ROM
# compiler instead:
#
#   1. copies each struct's declaration verbatim out of the decomp headers,
#   2. emits one global per field, initialized with that field (and only
#      that field -- the last element, for arrays) set to all-ones,
#   3. compiles that with agbcc (the compiler that builds the matching ROM)
#      and assembles it with arm-none-eabi-as,
#   4. reads each global's bytes back out of the object's .data section and
#      checks the set bits land exactly where the JSON says the field is.
#
# Also checks sizeof() of every struct the JSON gives a size for. Run from
# the repo root after regenerating the layout JSON:
#
#   python3 tools/verify_struct_layout.py
#
# --modern checks against arm-none-eabi-gcc (-mabi=apcs-gnu, the modern
# build's ABI) instead of agbcc.
import json
import os
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True  # no tools/__pycache__ from the import below
sys.path.insert(0, os.path.dirname(__file__))
from gen_struct_layout import CONSTS, extract_struct_body  # noqa: E402

LAYOUT_JSON = "helperapp/config/battle_pokemon_layout_us_rev0.json"

# struct name -> header it's declared in. SaveBlock1 is deliberately absent:
# it's mostly nested struct members, and its one field in the JSON (flags)
# comes straight from the decomp's own /*0xNN*/ annotation.
STRUCT_HEADERS = {
    "BattlePokemon": "include/pokemon.h",
    "BoxPokemon": None,  # union/substruct members -- not self-contained
    "DisableStruct": "include/battle.h",
    "ProtectStruct": "include/battle.h",
    "SideTimer": "include/battle.h",
    "BattleResources": "include/battle.h",
    "ResourceFlags": "include/battle.h",
    "BattleEnigmaBerry": "include/global.berry.h",
}

PRELUDE = """
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;
typedef u8 bool8;
typedef u32 bool32;
"""


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw)


def all_ones(bits):
    return (1 << bits) - 1


def build_source(layout):
    lines = [PRELUDE]
    lines += [f"#define {k} {v}" for k, v in CONSTS.items()]
    checks = []  # (symbol, struct, field or None)
    for struct_name, header in STRUCT_HEADERS.items():
        if header is None or struct_name not in layout:
            continue
        with open(header) as f:
            body = extract_struct_body(f.read(), struct_name)
        lines.append(f"struct {struct_name} {{{body};")
        lines.append(f"u32 size_{struct_name} = sizeof(struct {struct_name});")
        checks.append((f"size_{struct_name}", struct_name, None))
        for field, info in layout[struct_name]["fields"].items():
            bits = info.get("bits", info["size"] * 8)
            value = f"0x{all_ones(bits):X}u"
            if re.search(rf"\*\s*{field}\s*;", body):
                value = f"(void *){value}"
            init = f"{{ [{info['count'] - 1}] = {value} }}" if info["count"] > 1 else value
            sym = f"t_{struct_name}_{field}"
            lines.append(f"struct {struct_name} {sym} = {{ .{field} = {init} }};")
            checks.append((sym, struct_name, field))
    return "\n".join(lines) + "\n", checks


def compile_to_object(src, workdir, modern):
    c_path = os.path.join(workdir, "layout_check.c")
    i_path = os.path.join(workdir, "layout_check.i")
    s_path = os.path.join(workdir, "layout_check.s")
    o_path = os.path.join(workdir, "layout_check.o")
    with open(c_path, "w") as f:
        f.write(src)
    if modern:
        run(["arm-none-eabi-gcc", "-mthumb", "-mabi=apcs-gnu", "-O2", "-c", c_path, "-o", o_path])
    else:
        run(["cc", "-E", "-P", "-x", "c", c_path, "-o", i_path])
        run(["tools/agbcc/bin/agbcc", "-mthumb-interwork", "-O2", i_path, "-o", s_path])
        run(["arm-none-eabi-as", "-mcpu=arm7tdmi", s_path, "-o", o_path])
    return o_path


def read_symbols(o_path, workdir):
    """symbol -> bytes, from the object's .data section."""
    bin_path = os.path.join(workdir, "data.bin")
    run(["arm-none-eabi-objcopy", "-O", "binary", "-j", ".data", o_path, bin_path])
    with open(bin_path, "rb") as f:
        data = f.read()
    out = {}
    nm = run(["arm-none-eabi-nm", "-S", "--defined-only", o_path]).stdout
    for line in nm.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[2] in "dD":
            addr, size, name = int(parts[0], 16), int(parts[1], 16), parts[3]
            out[name] = data[addr:addr + size]
    return out


def expected_bytes(struct_size, info):
    """Bytes of a zeroed struct with just this field (last element) all-ones."""
    buf = bytearray(struct_size)
    elem = info["count"] - 1
    if "bits" in info:
        unit = info["offset"] - info["bitOffset"] // 8
        value = all_ones(info["bits"]) << info["bitOffset"]
        width = info["size"]
    else:
        unit = info["offset"] + elem * info["size"]
        value = all_ones(info["size"] * 8)
        width = info["size"]
    for i in range(width):
        buf[unit + i] |= (value >> (8 * i)) & 0xFF
    return bytes(buf)


def main():
    modern = "--modern" in sys.argv[1:]
    with open(LAYOUT_JSON) as f:
        layout = json.load(f)["structs"]

    src, checks = build_source(layout)
    failures = []
    with tempfile.TemporaryDirectory() as workdir:
        o_path = compile_to_object(src, workdir, modern)
        symbols = read_symbols(o_path, workdir)

    for sym, struct_name, field in checks:
        got = symbols.get(sym)
        if got is None:
            failures.append(f"{sym}: not found in compiled object")
            continue
        entry = layout[struct_name]
        if field is None:
            size = int.from_bytes(got[:4], "little")
            if size != entry["size"]:
                failures.append(f"sizeof(struct {struct_name}) = {size}, JSON says {entry['size']}")
            continue
        want = expected_bytes(entry["size"], entry["fields"][field])
        if got != want:
            failures.append(f"struct {struct_name}.{field}: compiler bytes {got.hex()} != JSON {want.hex()}")

    compiler = "arm-none-eabi-gcc (modern)" if modern else "agbcc"
    if failures:
        print(f"FAIL: {LAYOUT_JSON} disagrees with {compiler}:", file=sys.stderr)
        for f in failures:
            print(f"  {f}", file=sys.stderr)
        sys.exit(1)
    fields = sum(1 for _, _, f in checks if f is not None)
    print(f"OK: {fields} fields across {len(checks) - fields} structs match {compiler}'s layout.")


if __name__ == "__main__":
    main()
