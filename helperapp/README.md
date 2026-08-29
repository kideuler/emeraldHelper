# Emerald Battle Companion

Qt desktop app that reads live battle state from a running mGBA instance
and displays it. Build plan and rationale: `../CODE_PLAN.md`. Everything
under this directory is a standalone project, independent of the
pokeemerald ROM build one directory up.

Status vs. CODE_PLAN.md: Phases 0-5 done (transport, decode, minimal UI,
damage calc). Phase 6 (AI prediction) is intentionally not implemented --
this repo's AI is the old vanilla-Emerald bytecode interpreter
(`data/battle_ai_scripts.s`, ~3200 lines, run through
`src/battle_ai_script_commands.c`), not a single portable C function, and
faithfully porting it is out of scope for now.

## Layout

- `config/` -- runtime-loaded JSON (Phase 1.3/1.4/5.1), regenerated from a
  pokeemerald build, never hand-edited:
  - `symbols_us_rev0.json` -- `../tools/gen_symbols.py <map file>`
  - `battle_pokemon_layout_us_rev0.json` -- `../tools/gen_struct_layout.py`
  - `item_hold_effects_us_rev0.json` -- `../tools/gen_item_hold_effects.py`
  - `display_names_us_rev0.json` -- `../tools/gen_display_names.py` (species/
    move/ability/type names, pulled from `src/data/text/*.h`)
- `include/emerald/`, `src/` -- the app itself. The main window is a
  battler-column grid: 2 columns (4 in a double battle), one per battler,
  rows for name / HP / each of the four moves (live damage range, number
  and % of the opposing battler's max HP) / level / type / ability /
  status / stats. `battlerview.h`/`.cpp` build that grid as a pure
  function of a `BattleSnapshot` (`BuildBattlerGrid()`); `mainwindow.cpp`
  only renders it into a `QTableWidget`.
- `calc/` -- Phase 5 damage calculation, a plain C library (`emerald_calc`)
  ported from the decomp's own `CalculateBaseDamage()`/`TypeCalc()`
  (src/pokemon.c, src/battle_script_commands.c), not reimplemented from a
  formula writeup. See `calc/include/emerald_calc.h`'s header comment for
  what it does and doesn't model (no live per-turn field state -- weather,
  screens, crit, etc. are caller-supplied `EmeraldCalcFieldConditions`
  rather than read off a battle turn in progress).
- `lua/emerald_bridge.lua` -- loaded into mGBA (Tools -> Scripting), sends
  battle memory to the app over TCP.
- `tests/` -- offline unit tests (no emulator, no GUI event loop). The
  damage-calc tests are hand-computed against the real Gen 3 formula (see
  `tests/test_damage.cpp`'s header comment) -- not yet cross-checked
  against a live mGBA savestate (CODE_PLAN.md Phase 5.5).

## Build

```sh
cmake -S helperapp -B helperapp/build
cmake --build helperapp/build
ctest --test-dir helperapp/build --output-on-failure
```

Requires Qt6 (`Widgets`, `Network`) and, optionally, GoogleTest to build
`tests/` (skipped automatically if not found).

## Running it

1. Build/run a pokeemerald ROM in mGBA (0.10+).
2. mGBA: Tools -> Scripting -> load `lua/emerald_bridge.lua`.
3. Run `./build/emerald_companion`. It listens on `127.0.0.1:8888`; the Lua
   script dials in and retries every ~2s until it connects (mGBA's
   `socket.connect` is blocking, so the app has to be the listener).

## Regenerating config for a different ROM build

Every address and struct offset here is specific to one pokeemerald build.
After rebuilding pokeemerald (or targeting a different revision):

```sh
python3 ../tools/gen_symbols.py <path/to/pokeemerald.map> \
    > config/symbols_us_rev0.json
python3 ../tools/gen_struct_layout.py > config/battle_pokemon_layout_us_rev0.json
python3 ../tools/gen_item_hold_effects.py > config/item_hold_effects_us_rev0.json
python3 ../tools/gen_display_names.py > config/display_names_us_rev0.json
```

Or just run `../tools/rebuild_all.sh` from the repo root, which does all of
the above (plus the ROM build itself) in one shot.

Then copy the new `REGIONS` addresses from `symbols_us_rev0.json` into
`lua/emerald_bridge.lua` (see the comment at the top of that file) --
mGBA's Lua sandbox has no JSON parser, so those addresses are duplicated
there by hand and must be kept in sync manually.
