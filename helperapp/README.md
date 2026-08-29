# Emerald Battle Companion

Qt desktop app that reads live battle state from a running mGBA instance
and displays it. Build plan and rationale: `../CODE_PLAN.md`. Everything
under this directory is a standalone project, independent of the
pokeemerald ROM build one directory up.

Status vs. CODE_PLAN.md: Phases 0-4 done (transport, decode, minimal UI).
Phases 5 (damage calc) and 6 (AI prediction) are not implemented yet.

## Layout

- `config/` -- runtime-loaded JSON (Phase 1.3/1.4), regenerated from a
  pokeemerald build, never hand-edited:
  - `symbols_us_rev0.json` -- `../tools/gen_symbols.py <map file>`
  - `battle_pokemon_layout_us_rev0.json` -- `../tools/gen_struct_layout.py`
- `include/emerald/`, `src/` -- the app itself.
- `lua/emerald_bridge.lua` -- loaded into mGBA (Tools -> Scripting), sends
  battle memory to the app over TCP.
- `tests/` -- offline unit tests (no emulator, no GUI event loop).

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
```

Then copy the new `REGIONS` addresses from `symbols_us_rev0.json` into
`lua/emerald_bridge.lua` (see the comment at the top of that file) --
mGBA's Lua sandbox has no JSON parser, so those addresses are duplicated
there by hand and must be kept in sync manually.
