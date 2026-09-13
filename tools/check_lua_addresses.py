# tools/check_lua_addresses.py
#
# helperapp/lua/emerald_bridge.lua hardcodes addresses because mGBA's Lua
# sandbox has no JSON parser to load symbols_us_rev0.json at runtime (see
# that file's own header comment). That means every ROM rebuild risks the
# two silently drifting apart. This script is the one place that drift
# gets caught -- run after regenerating symbols_us_rev0.json, before
# trusting a rebuild.
import json
import re
import sys

SYMBOLS_JSON = "helperapp/config/symbols_us_rev0.json"
LAYOUT_JSON = "helperapp/config/battle_pokemon_layout_us_rev0.json"
LUA_BRIDGE = "helperapp/lua/emerald_bridge.lua"

REGION_RE = re.compile(r'name\s*=\s*"(\w+)".*?addr\s*=\s*0x([0-9A-Fa-f]+)')
# The pointer globals the bridge follows: `local gFoo = 0x...`
POINTER_RE = re.compile(r'^local\s+(g\w+)\s*=\s*0x([0-9A-Fa-f]+)', re.MULTILINE)
CONST_RE = re.compile(r'^local\s+([A-Z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)', re.MULTILINE)


REGION_LEN_RE = re.compile(r'name\s*=\s*"(\w+)".*?len\s*=\s*([0-9x*\s]+?)\s*}')


def expected_region_lengths(layout):
    """sizeof() each struct-array global emerald_bridge.lua reads."""
    structs = layout["structs"]
    return {
        "gBattleMons": structs["BattlePokemon"]["size"] * 4,
        "gSideTimers": structs["SideTimer"]["size"] * 2,
        "gDisableStructs": structs["DisableStruct"]["size"] * 4,
        "gProtectStructs": structs["ProtectStruct"]["size"] * 4,
        "gEnigmaBerries": structs["BattleEnigmaBerry"]["size"] * 4,
        "gPlayerParty": structs["Pokemon"]["size"] * 6,
        "gEnemyParty": structs["Pokemon"]["size"] * 6,
    }


def eval_len(expr):
    """`0x58 * 4` -> 352: products of integer literals only."""
    total = 1
    for factor in expr.split("*"):
        total *= int(factor.strip(), 0)
    return total


def expected_constants(layout):
    """The struct sizes/offsets emerald_bridge.lua hardcodes for pointer chasing."""
    structs = layout["structs"]
    flags = structs["SaveBlock1"]["fields"]["flags"]
    return {
        "BATTLE_RESOURCES_SIZE": structs["BattleResources"]["size"],
        "RESOURCE_FLAGS_SIZE": structs["ResourceFlags"]["size"],
        "SAVEBLOCK1_FLAGS_OFFSET": flags["offset"],
        "NUM_FLAG_BYTES": flags["count"],
    }


def main():
    with open(SYMBOLS_JSON) as f:
        symbols = json.load(f)
    with open(LAYOUT_JSON) as f:
        layout = json.load(f)
    with open(LUA_BRIDGE) as f:
        lua = f.read()

    stale = []
    seen = set()
    for m in list(REGION_RE.finditer(lua)) + list(POINTER_RE.finditer(lua)):
        name, addr = m.group(1), int(m.group(2), 16)
        seen.add(name)
        if name in symbols and symbols[name] != addr:
            stale.append((name, addr, symbols[name]))

    missing = sorted(n for n in seen if n not in symbols)

    lua_consts = {m.group(1): int(m.group(2), 0) for m in CONST_RE.finditer(lua)}
    bad_consts = []
    for name, want in expected_constants(layout).items():
        got = lua_consts.get(name)
        if got != want:
            bad_consts.append((name, got, want))
    lua_lens = {m.group(1): eval_len(m.group(2)) for m in REGION_LEN_RE.finditer(lua)}
    for name, want in expected_region_lengths(layout).items():
        got = lua_lens.get(name)
        if got != want:
            bad_consts.append((f"{name} len", got, want))

    if stale:
        print("STALE: emerald_bridge.lua addresses don't match symbols_us_rev0.json:",
              file=sys.stderr)
        for name, old, new in stale:
            print(f"  {name}: 0x{old:08X} -> 0x{new:08X}", file=sys.stderr)
    if bad_consts:
        print(f"STALE: emerald_bridge.lua struct constants don't match {LAYOUT_JSON}:",
              file=sys.stderr)
        for name, got, want in bad_consts:
            print(f"  {name}: {got!r} -> {want}", file=sys.stderr)
    if missing:
        print(f"NOTE: emerald_bridge.lua references symbols not in {SYMBOLS_JSON}: "
              f"{missing}", file=sys.stderr)

    if stale or bad_consts:
        sys.exit(1)
    print("emerald_bridge.lua addresses and struct constants match the current config.")


if __name__ == "__main__":
    main()
