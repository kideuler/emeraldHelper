# Pokémon Emerald Battle Companion — Build Plan

A Qt desktop app that reads live battle state from a running mGBA instance and displays
damage roll ranges and predicted opponent AI behavior.

> **Address caveat:** every hex address and struct offset in this document is an
> *example* from a US Emerald (rev 0) build. Extract your own from `pokeemerald.map`.
> Do not trust numbers copied from a document.

---

## Architecture

```
┌──────────────┐   TCP    ┌──────────────────────────────────────┐
│    mGBA      │  :8888   │            Qt Application            │
│              │ ───────► │                                      │
│  Lua script  │  raw     │  MemoryBridge  → BattleSnapshot      │
│  readRange() │  bytes   │       ↓                              │
│  every N     │          │  StructDecoder                       │
│  frames      │          │       ↓                              │
└──────────────┘          │  CalcEngine (ported decomp C)        │
                          │   ├─ damage rolls                    │
                          │   └─ AI score simulation             │
                          │       ↓                              │
                          │  UI (widgets / QML)                  │
                          └──────────────────────────────────────┘
```

Four layers, each independently testable:

1. **Transport** — get bytes out of the emulator.
2. **Decode** — bytes → typed `BattleSnapshot`.
3. **Simulate** — snapshot → damage rolls + AI move probabilities.
4. **Present** — Qt UI.

Layers 2 and 3 must be pure functions with no emulator dependency. That is what makes
the project testable offline.

---

## Phase 0 — Prerequisites

| Item | Notes |
|---|---|
| mGBA 0.10+ | Scripting lives under Tools → Scripting. Confirm the menu exists. |
| Pokémon Emerald ROM | `md5sum` it and record the revision. Addresses are per-revision. |
| devkitARM + agbcc | Follow pokeemerald's `INSTALL.md` for your OS. |
| Qt 6 (or 5.15+) | Needs `Network` and `Widgets` modules. |
| CMake | Easiest route for mixing C (ported decomp) with C++ (Qt). |

---

## Phase 1 — pokeemerald: build and harvest

### 1.1 Build the matching ROM

```bash
git clone https://github.com/pret/pokeemerald
cd pokeemerald
make -j$(nproc)
make compare      # verifies byte-identical output vs retail
```

Use the **matching** build (`make`), **not** `make modern`. Only the matching build
produces addresses that line up with a retail ROM in an emulator.

If `make compare` fails, stop. Everything downstream depends on address fidelity.

### 1.2 Extract the symbol table

The build emits `pokeemerald.map`. Lines look like:

```
                0x0000000002024084                gBattleMons
```

Symbols you will need immediately:

| Symbol | Purpose |
|---|---|
| `gBattleMons` | `BattlePokemon[4]` — decrypted, in-battle stats. Your primary source. |
| `gBattleTypeFlags` | Nonzero during battle; bitfield for double/trainer/safari/etc. |
| `gBattlerAttacker`, `gBattlerTarget` | Current actor indices. |
| `gBattlerPartyIndexes` | Maps battler slot → party slot. |
| `gAbsentBattlerFlags` | Which slots are fainted/absent. |
| `gBattleStruct` | Pointer to a larger scratch struct (EWRAM pointer, needs deref). |
| `gPlayerParty`, `gEnemyParty` | Encrypted `Pokemon[6]`. Needed for switch prediction. |
| `gTrainerBattleOpponent_A` | Index into `gTrainers[]` → gives AI flags. |
| `gRngValue` | LCG state (IWRAM). Optional, for exact-roll prediction. |
| `gBattleMoves` | ROM move table (power, type, accuracy, effect). |
| `gSpeciesInfo` | ROM species table (base stats, types, abilities). |

### 1.3 Generate an address config

Write a small script so this is reproducible, not hand-copied:

```python
# tools/gen_symbols.py
import json, re, sys

WANTED = {
    "gBattleMons", "gBattleTypeFlags", "gBattlerAttacker", "gBattlerTarget",
    "gBattlerPartyIndexes", "gAbsentBattlerFlags", "gBattleStruct",
    "gPlayerParty", "gEnemyParty", "gTrainerBattleOpponent_A",
    "gRngValue", "gBattleMoves", "gSpeciesInfo", "gTrainers",
}

pat = re.compile(r"^\s+0x([0-9a-fA-F]{16})\s+(\w+)$")
out = {}
for line in open(sys.argv[1]):
    m = pat.match(line.rstrip())
    if m and m.group(2) in WANTED:
        out[m.group(2)] = int(m.group(1), 16)

missing = WANTED - out.keys()
if missing:
    print(f"WARNING: not found: {sorted(missing)}", file=sys.stderr)

json.dump(out, sys.stdout, indent=2, sort_keys=True)
```

```bash
python3 tools/gen_symbols.py pokeemerald.map > symbols_us_rev0.json
```

Ship this JSON as a **runtime-loaded config**, not a compiled-in header. Supporting a
second ROM revision then costs one file, not a rebuild.

### 1.4 Record struct layouts

Get sizes from the decomp's own assertions rather than counting fields by hand:

```bash
grep -rn "STATIC_ASSERT" include/pokemon.h include/battle.h
grep -rn "struct BattlePokemon" -A 40 include/pokemon.h
```

Write down, for `BattlePokemon`, the byte offset and width of every field you need.
That offset table is the contract between the Lua script and the Qt decoder.

---

## Phase 2 — Struct decoding rules

### 2.1 Do not memcpy

The decomp's structs are laid out for ARM AAPCS. Bitfield packing (`abilityNum`,
`isEgg`, `ppBonuses`, `markings`) is **implementation-defined** and can differ under
x86-64. Write explicit little-endian readers:

```cpp
static inline quint16 rd16(const QByteArray &b, int off) {
    return quint8(b[off]) | (quint8(b[off + 1]) << 8);
}
static inline quint32 rd32(const QByteArray &b, int off) {
    return rd16(b, off) | (quint32(rd16(b, off + 2)) << 16);
}
```

### 2.2 Prefer `gBattleMons` over party data

`gBattleMons` entries are plaintext and already contain final computed stats, current
stat stages, ability, status, and PP. Use it for everything happening in the active
battle.

You only need party data (`gPlayerParty` / `gEnemyParty`) for benched Pokémon, i.e.
switch prediction. Those are encrypted:

1. `key = personality ^ otId`
2. XOR-decrypt the 48-byte data block in 32-bit words with `key`
3. Substruct order (Growth / Attacks / EVs / Misc) is permutation `personality % 24`
4. Verify with the checksum field before trusting the result

### 2.3 Pointer fields

Any pointer stored in emulated memory is a GBA address (`0x02xxxxxx` / `0x03xxxxxx`).
To follow it, subtract the domain base and index into your snapshot buffer. If a
pointer targets a region you did not capture, capture that region too — do not do a
second round trip per pointer.

---

## Phase 3 — The Lua bridge

### 3.1 Design

- **Qt listens, Lua connects out.** `socket.connect()` in mGBA is blocking, so a failed
  connect freezes the emulator briefly. Retry on a timer, never in a tight loop.
- **One `readRange` per tick**, not many small reads. A bulk read is atomic enough and
  avoids torn state mid-frame.
- **Fixed-size frames.** If every message is exactly N bytes, framing is trivial.
  Add a length header only once you send variable-size payloads.
- **~4 Hz is enough.** Battle state changes on menu transitions, not per frame.

### 3.2 Script

```lua
-- emerald_bridge.lua
local ADDR, PORT = "127.0.0.1", 8888

-- >>> replace with values from YOUR symbols json / map file <<<
local GBATTLEMONS  = 0x02024084
local MON_SIZE     = 0x58
local BLOCK_SIZE   = MON_SIZE * 4   -- four battler slots

local sock = nil
local lastAttempt = 0

local function tryConnect()
  local s, err = socket.connect(ADDR, PORT)
  if s == nil then
    console:warn("bridge: connect failed: " .. tostring(err))
    return
  end
  sock = s
  sock:add("error", function()
    console:warn("bridge: socket error, dropping")
    sock = nil
  end)
  console:log("bridge: connected")
end

callbacks:add("frame", function()
  local frame = emu:currentFrame()

  if sock == nil then
    if frame - lastAttempt > 120 then   -- retry ~every 2s
      lastAttempt = frame
      tryConnect()
    end
    return
  end

  if frame % 15 ~= 0 then return end    -- ~4 Hz

  local raw = emu:readRange(GBATTLEMONS, BLOCK_SIZE)
  local ok, err = sock:send(raw)
  if ok == nil then
    console:warn("bridge: send failed: " .. tostring(err))
    sock = nil
  end
end)

console:log("bridge: loaded, waiting for listener on " .. ADDR .. ":" .. PORT)
```

### 3.3 Growing the payload

Once the single-block version works, extend to a multi-region snapshot. Send a small
header so the Qt side stays version-tolerant:

```
[u32 magic 'EMBC'][u16 version][u16 regionCount]
  [u32 gbaAddr][u32 length][bytes...] x regionCount
```

Regions worth capturing: the `gBattleMons` array, the scalar battle globals block,
both party arrays, and the dereferenced `gBattleStruct`.

### 3.4 API reference

Relevant pieces of mGBA's Lua API:

- `emu:readRange(addr, len)` → string of raw bytes, full bus address space
- `emu.memory.wram` / `emu.memory.iwram` → `MemoryDomain` objects with domain-relative
  offsets (`wram` base `0x02000000`, `iwram` base `0x03000000`)
- `emu:read8/16/32(addr)` → single values, for debugging
- `callbacks:add("frame", fn)` → once per emulated frame
- `socket.connect(addr, port)` → blocking; `sock:send(str)`, `sock:receive(n)`,
  `sock:add("received"|"error", fn)`
- `console:log/warn/error` → the scripting window

---

## Phase 4 — Qt hookup

### 4.1 MemoryBridge

```cpp
// memorybridge.h
#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QByteArray>

class MemoryBridge : public QObject {
    Q_OBJECT
public:
    explicit MemoryBridge(quint16 port, int frameSize, QObject *parent = nullptr);
    bool isConnected() const { return m_client != nullptr; }

signals:
    void rawSnapshot(const QByteArray &raw);
    void connectionChanged(bool connected);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();

private:
    QTcpServer  *m_server;
    QTcpSocket  *m_client = nullptr;
    QByteArray   m_buffer;
    int          m_frameSize;
};
```

```cpp
// memorybridge.cpp
#include "memorybridge.h"

MemoryBridge::MemoryBridge(quint16 port, int frameSize, QObject *parent)
    : QObject(parent), m_frameSize(frameSize)
{
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection,
            this, &MemoryBridge::onNewConnection);
    if (!m_server->listen(QHostAddress::LocalHost, port))
        qWarning() << "listen failed:" << m_server->errorString();
}

void MemoryBridge::onNewConnection()
{
    if (m_client) {                 // only one emulator at a time
        m_server->nextPendingConnection()->deleteLater();
        return;
    }
    m_client = m_server->nextPendingConnection();
    connect(m_client, &QTcpSocket::readyRead,     this, &MemoryBridge::onReadyRead);
    connect(m_client, &QTcpSocket::disconnected,  this, &MemoryBridge::onDisconnected);
    m_buffer.clear();
    emit connectionChanged(true);
}

void MemoryBridge::onReadyRead()
{
    m_buffer.append(m_client->readAll());
    while (m_buffer.size() >= m_frameSize) {
        emit rawSnapshot(m_buffer.left(m_frameSize));
        m_buffer.remove(0, m_frameSize);
    }
}

void MemoryBridge::onDisconnected()
{
    m_client->deleteLater();
    m_client = nullptr;
    m_buffer.clear();
    emit connectionChanged(false);
}
```

### 4.2 Decoder

```cpp
struct BattleMon {
    quint16 species, attack, defense, speed, spAttack, spDefense;
    quint16 hp, maxHP;
    quint16 moves[4];
    quint8  pp[4];
    qint8   statStages[8];
    quint8  type1, type2, ability, level;
    quint32 status1;
};

struct BattleSnapshot {
    BattleMon mons[4];
    quint32   typeFlags = 0;
    bool      inBattle() const { return typeFlags != 0; }
};

BattleSnapshot decode(const QByteArray &raw, const SymbolTable &sym);
```

`decode` takes bytes and returns a value type. No sockets, no globals, no Qt widgets.
This is the function your unit tests hammer.

### 4.3 Threading

Start single-threaded. Once decoding plus calculation exceeds a few milliseconds, move
`MemoryBridge` and the decoder onto a worker `QThread` and marshal `BattleSnapshot`
across via a queued signal. Register it first:

```cpp
qRegisterMetaType<BattleSnapshot>("BattleSnapshot");
```

Never touch `QTcpSocket` from the GUI thread once it lives on a worker.

### 4.4 Change detection

Recomputing rolls at 4 Hz when nothing changed wastes cycles and makes the UI flicker.
Hash the raw snapshot and skip identical frames:

```cpp
const QByteArray h = QCryptographicHash::hash(raw, QCryptographicHash::Md5);
if (h == m_lastHash) return;
m_lastHash = h;
```

### 4.5 CMake

```cmake
cmake_minimum_required(VERSION 3.16)
project(emerald_companion LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_library(emerald_calc STATIC
    calc/damage.c
    calc/ai.c
    calc/shim.c
)
target_include_directories(emerald_calc PUBLIC calc/include)

add_executable(emerald_companion
    main.cpp
    memorybridge.cpp
    decoder.cpp
    mainwindow.cpp
)
target_link_libraries(emerald_companion PRIVATE
    Qt6::Widgets Qt6::Network emerald_calc)
```

---

## Phase 5 — Damage calculation

### 5.1 Port, do not reimplement

Copy the calculation functions out of `src/pokemon.c` and `src/battle_util.c` into
`calc/`. Compile them as C. Write a shim that backs the decomp's globals with your
decoded snapshot.

Gen 3 truncates to integer at many intermediate steps. Hand-rolled calculators built
from a wiki formula get the truncation ordering wrong and are off by 1–2 HP in exactly
the cases that decide a battle. Porting the real code makes those bugs impossible.

Set `include/config/battle.h` to vanilla Emerald values — upstream pokeemerald has
diverged behind `B_*` toggles that change damage behavior.

### 5.2 Shim sketch

```c
/* calc/shim.c — decomp globals backed by our decoded snapshot */
struct BattlePokemon gBattleMons[MAX_BATTLERS_COUNT];
u8  gBattlerAttacker;
u8  gBattlerTarget;
u32 gBattleTypeFlags;
u16 gCurrentMove;
s32 gBattleMoveDamage;

void CalcShim_LoadSnapshot(const CalcSnapshot *s);   /* memcpy into the globals */
```

### 5.3 Roll range

```c
typedef struct { s32 rolls[16]; s32 min; s32 max; bool32 isCrit; } RollSet;

RollSet CalcRolls(const CalcSnapshot *s, u8 attacker, u8 target,
                  u16 move, bool32 crit);
```

The random factor spans 85–100 inclusive, giving 16 discrete outcomes. Compute crit
and non-crit sets separately and surface both.

### 5.4 Derived output the UI actually wants

- **OHKO chance** — fraction of rolls ≥ target HP
- **Rolls to KO** — smallest n where `n × minRoll ≥ HP` vs `n × maxRoll ≥ HP`
- **Guaranteed vs possible** — "2HKO guaranteed" reads better than a raw range
- **Accuracy folded in** — a guaranteed OHKO at 70% accuracy is a different decision

### 5.5 Validation

Load a savestate. Compute the 16 rolls. Execute that move ~25 times from the state.
Every observed damage value must appear in your set, and the observed spread should
approach the full set. If a single value falls outside, the port is wrong — fix it
before proceeding.

---

## Phase 6 — AI prediction

### 6.1 Locate the AI

- **Vanilla Emerald:** a bytecode interpreter — `src/battle_ai_script_commands.c` plus
  scripts in `data/battle_ai_scripts.s`.
- **Modern pokeemerald:** rewritten in C in `src/battle_ai_main.c`.

Match whichever your checkout uses. Port it into `calc/ai.c` against the same shim.

### 6.2 Mechanics to preserve

- Each move starts at **score 100**.
- Enabled `AI_FLAG_*` bits gate which scoring passes run. Flags come from the trainer
  entry in `gTrainers[]`, indexed by `gTrainerBattleOpponent_A`.
- Highest score wins; **ties break randomly**.
- Separate routines govern **switching** (`ShouldSwitch` in
  `battle_ai_switch_items.c`) and **item use**. Skip these and you will mispredict
  every Full Restore and every pivot.

### 6.3 Output a distribution, not a prediction

Because ties break randomly, the honest answer is a probability per move:

```cpp
struct AiPrediction {
    struct MoveOdds { quint16 move; int score; double probability; };
    QVector<MoveOdds> moves;
    double switchProbability;
    double itemProbability;
};
```

A UI that says "Earthquake, 100%" when it is a three-way tie is worse than no UI. Show
"Earthquake / Rock Slide / Protect — 33% each."

### 6.4 Optional: exact prediction via RNG

Gen 3's LCG is `seed = seed * 0x41C64E6D + 0x6073`, with `Random()` returning the high
16 bits. Track `gRngValue` and you can advance the sequence to predict the exact damage
roll, crit, and tie-break rather than a distribution.

This is a large scope increase and it is fragile — every unaccounted RNG consumer
desynchronizes you. Build it last, behind a toggle, and only after everything else is
solid.

---

## Phase 7 — UI

Suggested layout:

- **Header** — connection status, detected ROM revision, in-battle indicator
- **Left** — your active Pokémon: stats, stat stages, status, HP
- **Center** — your four moves, each with roll range, KO odds, accuracy-adjusted odds
- **Right** — opponent: known stats, predicted move distribution as a bar list
- **Footer** — switch/item probability, and a "stale data" warning when the bridge drops

Start with `QTableView` and a `QAbstractTableModel`. Make it correct, then make it
pretty. Do not build the UI first — you will rebuild it once the data model settles.

---

## Testing strategy

### Fixture capture

From Lua, dump memory regions to disk at interesting moments:

```lua
local function dumpRegion(path, addr, len)
  local f = assert(io.open(path, "wb"))
  f:write(emu:readRange(addr, len))
  f:close()
  console:log("dumped " .. path)
end
```

Capture ~10 varied battles: singles, doubles, statused mons, active weather, stat
drops, a mon behind Substitute, a Focus Sash-style edge case, a two-type resist.

Commit those `.bin` files. They are your regression suite. Every layer above the
transport gets tested against them with **no emulator in the loop** — which means tests
run in milliseconds in CI, and a bug is reproducible forever instead of "that thing
that happened in a battle last Tuesday."

### Test ordering

| Layer | Test |
|---|---|
| Transport | Frame size is exactly N bytes, every time, across a reconnect |
| Decoder | Fixture → known species/HP/stat values |
| Damage | Fixture → roll set; cross-check against 25 in-game executions |
| AI | Fixture → score vector; cross-check against observed AI choices over many resets |
| UI | Manual |

---

## Gotchas

- **Wrong ROM revision** — symbol drift produces plausible-looking garbage rather than
  an obvious crash. Checksum the ROM at startup and refuse to run on a mismatch.
- **Bitfield packing** — never `reinterpret_cast` a decomp struct onto received bytes.
- **Torn reads** — one bulk `readRange` per tick, never a sequence of small reads.
- **Blocking connect** — `socket.connect` stalls the emulator; always retry on a timer.
- **`make modern`** — produces a non-matching binary with different addresses.
- **`B_*` config flags** — upstream pokeemerald's battle config defaults are not
  vanilla Emerald behavior.
- **Stale UI** — when the bridge disconnects, grey out the display. Confidently wrong
  numbers are worse than a blank panel.

---

## Milestones

| # | Deliverable | Done when |
|---|---|---|
| 0 | Toolchain | `make compare` passes |
| 1 | Symbol config | `symbols_us_rev0.json` generated from the map |
| 2 | Lua echo | `console:log` prints the correct opposing species |
| 3 | Fixtures | 10 battle memory dumps committed |
| 4 | Transport | Qt receives fixed-size frames; survives reconnect |
| 5 | Decoder | Fixture tests pass for all fields |
| 6 | Damage | Roll sets validated against 25 in-game executions |
| 7 | UI v1 | Ugly table, live, correct |
| 8 | AI | Move distribution matches observed choices |
| 9 | UI v2 | Actually pleasant to use |
| 10 | RNG (optional) | Exact roll prediction |

Do not skip 2 and 3. Every hour spent on fixtures saves several later.