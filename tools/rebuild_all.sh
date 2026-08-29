#!/usr/bin/env bash
set -euo pipefail

# tools/rebuild_all.sh
#
# End-to-end rebuild pipeline for the Emerald Battle Companion:
#   1. build the pokeemerald ROM (+ its .map)
#   2. regenerate helperapp/config/*.json from that .map (Phase 1.3/1.4 --
#      "reproducible, not hand-copied")
#   3. warn if helperapp/lua/emerald_bridge.lua's hardcoded addresses have
#      drifted from the freshly generated symbol table (see
#      tools/check_lua_addresses.py for why that can't just be automated
#      away)
#   4. rebuild the Qt companion app and, unless --skip-tests, run its tests
#
# Usage: tools/rebuild_all.sh [--matching] [--jobs N] [--skip-rom] [--skip-tests]
#
#   --matching   build the byte-identical `make` target (agbcc) instead of
#                the default `make modern`. Only matters if you intend to
#                point mGBA at a *retail* ROM rather than the one this
#                builds -- see CODE_PLAN.md's "Wrong ROM revision" gotcha.
#                Runs `make compare` afterward to confirm it's byte-exact.
#   --jobs N     parallel jobs for `make` / `cmake --build` (default: nproc)
#   --skip-rom   skip the ROM build; regenerate config from whatever .map
#                already exists and rebuild the app
#   --skip-tests skip `ctest` after building the app

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

matching=0
jobs="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
skip_rom=0
skip_tests=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --matching) matching=1; shift ;;
        --jobs) jobs="$2"; shift 2 ;;
        --skip-rom) skip_rom=1; shift ;;
        --skip-tests) skip_tests=1; shift ;;
        *) echo "unknown argument: $1" >&2; exit 1 ;;
    esac
done

if [[ $matching -eq 1 ]]; then
    map_file="pokeemerald.map"
    make_target="rom"
else
    map_file="pokeemerald_modern.map"
    make_target="modern"
fi

if [[ $skip_rom -eq 0 ]]; then
    echo "==> Building pokeemerald ($make_target, -j$jobs)"
    make "$make_target" -j"$jobs"
    if [[ $matching -eq 1 ]]; then
        echo "==> make compare"
        make compare
    fi
else
    echo "==> Skipping ROM build (--skip-rom); using existing $map_file"
fi

if [[ ! -f "$map_file" ]]; then
    echo "error: $map_file not found (build it first, or drop --skip-rom)" >&2
    exit 1
fi

echo "==> Regenerating helperapp/config/symbols_us_rev0.json from $map_file"
python3 tools/gen_symbols.py "$map_file" > helperapp/config/symbols_us_rev0.json

echo "==> Regenerating helperapp/config/battle_pokemon_layout_us_rev0.json"
python3 tools/gen_struct_layout.py > helperapp/config/battle_pokemon_layout_us_rev0.json

echo "==> Checking helperapp/lua/emerald_bridge.lua against the fresh symbol table"
python3 tools/check_lua_addresses.py || true # warn only, never block the rebuild

echo "==> Building helperapp (Qt companion app, -j$jobs)"
cmake -S helperapp -B helperapp/build >/dev/null
cmake --build helperapp/build -j"$jobs"

if [[ $skip_tests -eq 0 ]]; then
    echo "==> Running helperapp tests"
    ctest --test-dir helperapp/build --output-on-failure
fi

echo "==> Done."
