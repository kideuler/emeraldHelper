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
-- >>> Addresses below are for pokeemerald_modern.map as built in this
-- >>> repo. Regenerate helperapp/config/symbols_us_rev0.json with
-- >>> `python3 tools/gen_symbols.py <your.map>` after any rebuild and
-- >>> copy the new values in here -- they WILL move between builds.
local ADDR, PORT = "127.0.0.1", 8888

-- Cross-checked against helperapp/config/symbols_us_rev0.json (this
-- table's raison d'etre -- see the module comment above for how to
-- regenerate both after a ROM rebuild).
local REGIONS = {
  { name = "gBattleMons",           addr = 0x02001438, len = 0x58 * 4 },
  { name = "gBattleTypeFlags",      addr = 0x0200039C, len = 4 },
  { name = "gBattlerAttacker",      addr = 0x020015BF, len = 1 },
  { name = "gBattlerTarget",        addr = 0x020015C0, len = 1 },
  { name = "gAbsentBattlerFlags",   addr = 0x020015C4, len = 1 },
  { name = "gBattlerPartyIndexes",  addr = 0x02001420, len = 8 },
  { name = "gPlayerParty",          addr = 0x02036FF4, len = 100 * 6 },
  { name = "gEnemyParty",           addr = 0x0203724C, len = 100 * 6 },
}

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

local function buildFrame()
  local parts = { "EMBC", u16le(PROTOCOL_VERSION), u16le(#REGIONS) }
  for _, region in ipairs(REGIONS) do
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
