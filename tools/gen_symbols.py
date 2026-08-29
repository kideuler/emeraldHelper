# tools/gen_symbols.py
#
# Extracts the runtime addresses the companion app needs from a linker .map
# file and writes them as a small JSON config (see CODE_PLAN.md Phase 1.3).
# Re-run this any time the ROM is rebuilt; never hand-copy addresses.
#
#   python3 tools/gen_symbols.py pokeemerald_modern.map > helperapp/config/symbols_us_rev0.json
import json
import re
import sys

WANTED = {
    "gBattleMons", "gBattleTypeFlags", "gBattlerAttacker", "gBattlerTarget",
    "gBattlerPartyIndexes", "gAbsentBattlerFlags", "gBattleStruct",
    "gPlayerParty", "gEnemyParty", "gTrainerBattleOpponent_A",
    "gRngValue", "gBattleMoves", "gSpeciesInfo", "gTrainers",
}

# Matches an exact symbol-definition line, e.g.:
#                 0x02001438                gBattleMons
# Anchored on both ends so it can't match a substring reference elsewhere
# in the map file (object file paths, cross-reference notes, etc).
LINE_RE = re.compile(r"^\s+0x([0-9a-fA-F]+)\s+(\w+)$")


def extract(map_path):
    out = {}
    with open(map_path) as f:
        for line in f:
            m = LINE_RE.match(line.rstrip("\n"))
            if m and m.group(2) in WANTED:
                out[m.group(2)] = int(m.group(1), 16)
    return out


def main():
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <path/to/pokeemerald.map>", file=sys.stderr)
        sys.exit(2)

    out = extract(sys.argv[1])

    missing = WANTED - out.keys()
    if missing:
        print(f"WARNING: not found: {sorted(missing)}", file=sys.stderr)

    json.dump(out, sys.stdout, indent=2, sort_keys=True)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
