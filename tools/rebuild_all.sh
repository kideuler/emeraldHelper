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
# Usage: tools/rebuild_all.sh [--modern] [--jobs N] [--skip-rom] [--skip-tests] [--clean]
#
#   --modern     build with modern arm-none-eabi-gcc (`make modern`)
#                instead of the default byte-identical agbcc build
#                (`make rom`, requires tools/agbcc -- see cmake/Agbcc.cmake
#                or INSTALL.md to build/install it). helperapp's config
#                and lua/emerald_bridge.lua get regenerated against
#                whichever variant you build, so don't mix: running once
#                with --modern and once without leaves the *other*
#                variant's addresses in helperapp until you rebuild that
#                one too.
#   --matching   accepted for backwards compatibility; this is now the
#                default, so it's a no-op.
#   --jobs N     parallel jobs for `make` / `cmake --build` (default: nproc)
#   --skip-rom   skip the ROM build; regenerate config from whatever .map
#                already exists and rebuild the app. Incompatible with
#                --clean, which removes that .map as part of wiping the
#                ROM build.
#   --skip-tests skip `ctest` after building the app
#   --clean      wipe both build trees first, for a from-scratch rebuild:
#                `make clean` (ROM objects, tools, generated assets, the
#                ROM/ELF/.map themselves) and `rm -rf helperapp/build`
#                (Qt app's CMake build dir, so it's freshly reconfigured
#                too). Slower, but rules out stale-object weirdness.

# $0, not BASH_SOURCE[0]: BASH_SOURCE is a bash-only array and is unset
# under zsh/sh, which silently resolves dirname to "." and points ROOT
# one directory above the repo instead of at it. $0 holds the invoked
# script path under bash, zsh, and sh alike (as long as the script isn't
# sourced, which it isn't here).
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

matching=1
jobs="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
skip_rom=0
skip_tests=0
clean=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --modern) matching=0; shift ;;
        --matching) matching=1; shift ;;
        --jobs) jobs="$2"; shift 2 ;;
        --skip-rom) skip_rom=1; shift ;;
        --skip-tests) skip_tests=1; shift ;;
        --clean) clean=1; shift ;;
        *) echo "unknown argument: $1" >&2; exit 1 ;;
    esac
done

if [[ $clean -eq 1 && $skip_rom -eq 1 ]]; then
    echo "error: --clean and --skip-rom conflict (--clean removes the .map --skip-rom expects to reuse)" >&2
    exit 1
fi

if [[ $matching -eq 1 ]]; then
    map_file="pokeemerald.map"
    make_target="rom"
else
    map_file="pokeemerald_modern.map"
    make_target="modern"
fi

if [[ $clean -eq 1 ]]; then
    echo "==> make clean"
    make clean
    echo "==> Removing helperapp/build"
    rm -rf helperapp/build
fi

# The agbcc build needs tools/agbcc (bin/{agbcc,old_agbcc,agbcc_arm},
# lib/{libgcc,libc}.a); make clean never removes it (agbcc isn't one of
# make_tools.mk's TOOL_NAMES), but a fresh checkout won't have it yet.
# Build it from the ./agbcc submodule via the same script CMake's
# agbcc_toolchain target uses (cmake/Agbcc.cmake), so this Makefile-driven
# path doesn't need its own copy of that build logic.
if [[ $matching -eq 1 && $skip_rom -eq 0 ]]; then
    if [[ ! -x tools/agbcc/bin/agbcc || ! -x tools/agbcc/bin/old_agbcc \
       || ! -x tools/agbcc/bin/agbcc_arm || ! -f tools/agbcc/lib/libgcc.a \
       || ! -f tools/agbcc/lib/libc.a ]]; then
        if [[ ! -f agbcc/build.sh ]]; then
            echo "error: agbcc submodule not checked out (agbcc/build.sh missing). Run: git submodule update --init agbcc" >&2
            exit 1
        fi
        echo "==> tools/agbcc missing or incomplete; building it from ./agbcc"
        sh cmake/build_agbcc.sh agbcc "$ROOT"
    fi
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

# Fatal, unlike the Lua check below: a wrong offset here means the decoder
# silently reads garbage into BattleState.
echo "==> Verifying struct layouts against the ROM compiler"
if [[ $matching -eq 1 ]]; then
    python3 tools/verify_struct_layout.py
else
    python3 tools/verify_struct_layout.py --modern
fi

echo "==> Regenerating helperapp/config/item_hold_effects_us_rev0.json (Phase 5)"
python3 tools/gen_item_hold_effects.py > helperapp/config/item_hold_effects_us_rev0.json

echo "==> Regenerating helperapp/config/display_names_us_rev0.json (species/move/ability/type names)"
python3 tools/gen_display_names.py > helperapp/config/display_names_us_rev0.json

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
