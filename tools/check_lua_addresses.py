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
LUA_BRIDGE = "helperapp/lua/emerald_bridge.lua"

REGION_RE = re.compile(r'name\s*=\s*"(\w+)".*?addr\s*=\s*0x([0-9A-Fa-f]+)')


def main():
    with open(SYMBOLS_JSON) as f:
        symbols = json.load(f)
    with open(LUA_BRIDGE) as f:
        lua = f.read()

    stale = []
    seen = set()
    for m in REGION_RE.finditer(lua):
        name, addr = m.group(1), int(m.group(2), 16)
        seen.add(name)
        if name in symbols and symbols[name] != addr:
            stale.append((name, addr, symbols[name]))

    missing = sorted(n for n in seen if n not in symbols)

    if stale:
        print("STALE: emerald_bridge.lua addresses don't match symbols_us_rev0.json:",
              file=sys.stderr)
        for name, old, new in stale:
            print(f"  {name}: 0x{old:08X} -> 0x{new:08X}", file=sys.stderr)
    if missing:
        print(f"NOTE: emerald_bridge.lua references symbols not in {SYMBOLS_JSON}: "
              f"{missing}", file=sys.stderr)

    if stale:
        sys.exit(1)
    print("emerald_bridge.lua addresses match the current symbol table.")


if __name__ == "__main__":
    main()
