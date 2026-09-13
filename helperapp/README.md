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
  and % of the opposing battler's max HP, one-use KO chance, plus the
  move's computed power/type/hit count when its script changes them, or
  why it does nothing) / level / type / ability / status / stat stages /
  stats, under a line showing weather and each side's screens. `battlerview.h`/`.cpp` build that grid as a pure
  function of a `BattleSnapshot` (`BuildBattlerGrid()`); `mainwindow.cpp`
  only renders it into a `QTableWidget`.
- `calc/` -- Phase 5 move calculation, a plain C library (`emerald_calc`)
  ported from the decomp, not reimplemented from a formula writeup:
  - `calc/include/battle_state.h` -- `struct BattleState`, a one-for-one
    mirror of the game's battle globals (`gBattleMons`, `gBattleWeather`,
    `gSideStatuses`, `gSideTimers`, `gStatuses3`, `gDisableStructs`,
    `gProtectStructs`, `gEnigmaBerries`, Flash Fire flags, badge flags,
    both parties, ...), with the decomp's own struct and field names.
  - `calc/src/damage.c` -- `CalculateBaseDamage()` and the `damagecalc` /
    `typecalc` / `critcalc` / `accuracycheck` script commands.
  - `calc/src/move_scripts.c` -- each move effect's battle script from
    `data/battle_scripts_1.s`, run up to where damage is dealt, with the
    effect-specific commands (Low Kick's `weightdamagecalculation`,
    Flail, Hidden Power, Weather Ball, Rollout, Magnitude, Present, Beat
    Up, multi-hit, fixed-damage and OHKO moves, ...). Every `Random()` is
    enumerated rather than rolled.
  - `calc/src/ko.c` -- exact KO odds from those outcomes (accuracy, crits,
    rolls, hit counts, Focus Band, Endure).
  - `calc/src/game_data.c` -- `src/data/battle_moves.h`, `species_info.h`
    and the Pokedex tables, compiled in unmodified.

  `EmeraldCalc_SimulateMove(state, attacker, target, move)` is the entry
  point; see `calc/include/emerald_calc.h`.
- `lua/emerald_bridge.lua` -- loaded into mGBA (Tools -> Scripting), sends
  every region `BattleState` needs to the app over TCP, following the
  `gBattleResources` and `gSaveBlock1Ptr` pointers itself. The decoder
  lists any region it didn't receive in the window's status line.
- `tests/` -- offline unit tests (no emulator, no GUI event loop):
  - `test_damage.cpp` -- hand-computed cases from the decomp's code,
    derivations in the comments.
  - `test_decomp_diff.cpp` -- a differential test: `CalculateBaseDamage()`
    and the script commands are extracted *verbatim* from `src/` at build
    time (`extract_decomp.py`, `decomp_reference.c`) and the port is
    required to match them exactly over hundreds of thousands of random
    battle states (crit/accuracy over all 65536 `Random()` values).

  Neither is yet cross-checked against a live mGBA savestate
  (CODE_PLAN.md Phase 5.5).

## Build

```sh
cmake -S helperapp -B helperapp/build
cmake --build helperapp/build
ctest --test-dir helperapp/build --output-on-failure
```

Requires Qt6 (`Widgets`, `Network`) and, optionally, GoogleTest (plus Python 3,
for the differential test's source extraction) to build
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
python3 ../tools/verify_struct_layout.py   # from the repo root: checks every offset against agbcc
python3 ../tools/gen_item_hold_effects.py > config/item_hold_effects_us_rev0.json
python3 ../tools/gen_display_names.py > config/display_names_us_rev0.json
```

Or just run `../tools/rebuild_all.sh` from the repo root, which does all of
the above (plus the ROM build itself) in one shot. Add `--clean` to wipe
both build trees first for a from-scratch rebuild.

Then copy the new `REGIONS` addresses (and the `gBattleResources` /
`gSaveBlock1Ptr` pointer addresses below them) from `symbols_us_rev0.json`
into `lua/emerald_bridge.lua` (see the comment at the top of that file) --
mGBA's Lua sandbox has no JSON parser, so those addresses, plus a few
struct sizes, are duplicated there by hand. `../tools/check_lua_addresses.py`
(run by `rebuild_all.sh`) flags any address, length or struct constant that
no longer matches the config.
