-- emerald_bridge.lua
--
-- CODE_PLAN.md Phase 3: runs inside mGBA (Tools -> Scripting), reads a
-- fixed set of battle-related regions out of RAM at ~4 Hz, and streams
-- them to the Qt companion app (MemoryBridge, helperapp/src/memorybridge.cpp)
-- over a plain TCP socket using the framed, multi-region protocol from
-- Phase 3.3:
--
--   [4 bytes 'EMBC'][u16 version LE][u16 regionCount LE]
--     ([u32 gbaAddr LE][u32 length LE][bytes...]) x regionCount
--
-- Each region is addressed, not positional -- the Qt side resolves which
-- symbol a region is by its gbaAddr (via the same symbols_us_rev0.json
-- this table was generated from), so region order here doesn't matter to
-- it and isn't a wire contract.
--
-- >>> Addresses below are for pokeemerald.map (the matching/agbcc build)
-- >>> as built in this repo. Regenerate helperapp/config/symbols_us_rev0.json
-- >>> with `python3 tools/gen_symbols.py <your.map>` after any rebuild and
-- >>> copy the new values in here -- they WILL move between builds, and
-- >>> WILL differ between the matching and modern builds (different
-- >>> compiler, different layout).
local ADDR, PORT = "127.0.0.1", 8888

-- Cross-checked against helperapp/config/symbols_us_rev0.json (this
-- table's raison d'etre -- see the module comment above for how to
-- regenerate both after a ROM rebuild).
-- Lengths are sizeof() the global (struct sizes from
-- helperapp/config/battle_pokemon_layout_us_rev0.json). Together these are
-- everything the companion's BattleState (helperapp/calc/include/
-- battle_state.h) mirrors.
local REGIONS = {
  { name = "gBattleMons",              addr = 0x02024084, len = 0x58 * 4 },
  { name = "gBattleTypeFlags",         addr = 0x02022FEC, len = 4 },
  { name = "gBattleEnvironment",       addr = 0x02022FF0, len = 1 },
  { name = "gBattlersCount",           addr = 0x0202406C, len = 1 },
  { name = "gBattlerPositions",        addr = 0x02024076, len = 4 },
  { name = "gBattlerAttacker",         addr = 0x0202420B, len = 1 },
  { name = "gBattlerTarget",           addr = 0x0202420C, len = 1 },
  { name = "gAbsentBattlerFlags",      addr = 0x02024210, len = 1 },
  { name = "gBattlerPartyIndexes",     addr = 0x0202406E, len = 8 },
  { name = "gBattleWeather",           addr = 0x020243CC, len = 2 },
  { name = "gSideStatuses",            addr = 0x0202428E, len = 2 * 2 },
  { name = "gSideTimers",              addr = 0x02024294, len = 12 * 2 },
  { name = "gStatuses3",               addr = 0x020242AC, len = 4 * 4 },
  { name = "gDisableStructs",          addr = 0x020242BC, len = 28 * 4 },
  { name = "gProtectStructs",          addr = 0x0202433C, len = 16 * 4 },
  { name = "gEnigmaBerries",           addr = 0x02024404, len = 28 * 4 },
  { name = "gTrainerBattleOpponent_A", addr = 0x02038BCA, len = 2 },
  { name = "gPlayerParty",             addr = 0x020244EC, len = 100 * 6 },
  { name = "gEnemyParty",              addr = 0x02024744, len = 100 * 6 },
}

-- Pointer globals. Their targets live on the heap / in the save block, so
-- they're followed here and each hop is sent as its own region at the
-- address it was read from -- the app follows the same pointers through
-- those regions (src/decoder.cpp), so nothing here needs to know what the
-- pointed-at structs mean beyond their size and the one field chased.
local gBattleResources = 0x020244A8 -- struct BattleResources *
local gSaveBlock1Ptr   = 0x03005D8C -- struct SaveBlock1 *
local BATTLE_RESOURCES_SIZE     = 32     -- sizeof(struct BattleResources); ->flags at +4
local RESOURCE_FLAGS_SIZE       = 16     -- sizeof(struct ResourceFlags)
local SAVEBLOCK1_FLAGS_OFFSET   = 0x1270 -- offsetof(struct SaveBlock1, flags)
local NUM_FLAG_BYTES            = 300

local PROTOCOL_VERSION = 1

local sock = nil
local lastAttempt = 0

-- mGBA's bundled Lua may not have string.pack (5.3+), so pack integers by
-- hand rather than assume it's available.
local function u16le(n)
  return string.char(n % 256, math.floor(n / 256) % 256)
end

local function u32le(n)
  return u16le(n % 65536) .. u16le(math.floor(n / 65536) % 65536)
end

-- Only follow pointers into EWRAM/IWRAM: before a battle (or save) exists
-- these can be NULL or stale.
local function isRamPointer(p)
  return (p >= 0x02000000 and p < 0x02040000) or (p >= 0x03000000 and p < 0x03008000)
end

local function buildFrame()
  local regions = {}
  for _, region in ipairs(REGIONS) do
    regions[#regions + 1] = { addr = region.addr, len = region.len }
  end

  local function addPointer(addr)
    regions[#regions + 1] = { addr = addr, len = 4 }
    return emu:read32(addr)
  end

  -- gBattleResources -> struct BattleResources -> ->flags (struct ResourceFlags)
  local resources = addPointer(gBattleResources)
  if isRamPointer(resources) then
    regions[#regions + 1] = { addr = resources, len = BATTLE_RESOURCES_SIZE }
    local flags = emu:read32(resources + 4)
    if isRamPointer(flags) then
      regions[#regions + 1] = { addr = flags, len = RESOURCE_FLAGS_SIZE }
    end
  end

  -- gSaveBlock1Ptr -> ->flags[] (badge flags)
  local saveBlock1 = addPointer(gSaveBlock1Ptr)
  if isRamPointer(saveBlock1) then
    regions[#regions + 1] = { addr = saveBlock1 + SAVEBLOCK1_FLAGS_OFFSET, len = NUM_FLAG_BYTES }
  end

  local parts = { "EMBC", u16le(PROTOCOL_VERSION), u16le(#regions) }
  for _, region in ipairs(regions) do
    parts[#parts + 1] = u32le(region.addr)
    parts[#parts + 1] = u32le(region.len)
    -- One readRange per region, all within this single frame callback --
    -- avoids torn state mid-frame (Phase 2.3 / the "Torn reads" gotcha).
    parts[#parts + 1] = emu:readRange(region.addr, region.len)
  end
  return table.concat(parts)
end

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
    if frame - lastAttempt > 120 then -- retry ~every 2s
      lastAttempt = frame
      tryConnect()
    end
    return
  end

  if frame % 15 ~= 0 then return end -- ~4 Hz; battle state changes on menu
                                      -- transitions, not per frame

  local ok, err = sock:send(buildFrame())
  if ok == nil then
    console:warn("bridge: send failed: " .. tostring(err))
    sock = nil
  end
end)

console:log("bridge: loaded, waiting for listener on " .. ADDR .. ":" .. PORT)
