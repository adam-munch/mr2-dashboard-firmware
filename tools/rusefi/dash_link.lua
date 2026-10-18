-- MR2 dashboard -> rusEFI: receive the dash status frame.
--
-- Load this as the ECU's Lua script (TunerStudio: Lua script editor, then
-- burn). It listens for the dashboard's status frame, works out whether the
-- dash is alive, and publishes what it reports on Lua gauges 1..3 so you can
-- see and log it in TunerStudio. Add your own actions in onDashInputs().
--
-- Frame layout (see src/app/ecu_link.h), 8 bytes, ID 0x6D0, 11-bit:
--   data[1]     counter, +1 per frame
--   data[2]     flags: bit 0 display running, bit 1 dash receiving ECU data
--   data[3..4]  inputs, little-endian, bit n = input n active
--   data[5]     mode
-- (Lua tables start at 1, so data[1] is byte 0 on the wire.)
--
-- The dash sends this every 100 ms, and at once when an input changes. It
-- goes quiet for ~2 s while its display starts, so don't treat a missing
-- dash as an error at power-up.

local DASH_ID = 0x6D0
local DASH_TIMEOUT_S = 0.5 -- 5 missed frames

local dashTimer = Timer.new()
local dashSeen = false
local inputs = 0
local mode = 0

-- Called whenever the inputs or mode change, including the fail-safe reset
-- to 0 when the dash goes quiet. Put your actions here.
local function onDashInputs(newInputs, newMode)
	print("dash inputs " .. newInputs .. " mode " .. newMode)
	-- Example: input 0 held = add 2 degrees of timing (replace with your own).
	-- if (newInputs & 1) ~= 0 then setTimingAdd(2) else setTimingAdd(0) end
end

local function setState(newInputs, newMode)
	if newInputs ~= inputs or newMode ~= mode then
		inputs = newInputs
		mode = newMode
		onDashInputs(inputs, mode)
	end
end

local function onDashStatus(bus, id, dlc, data)
	if dlc < 5 then
		return
	end
	dashTimer:reset()
	dashSeen = true
	setState(data[3] + data[4] * 256, data[5])
end

canRxAdd(DASH_ID, onDashStatus)
setTickRate(20)

function onTick()
	local alive = dashSeen and dashTimer:getElapsedSeconds() < DASH_TIMEOUT_S
	if not alive then
		-- Fail safe: a silent dash has no buttons held.
		setState(0, mode)
	end

	setLuaGauge(1, inputs)
	setLuaGauge(2, mode)
	setLuaGauge(3, alive and 1 or 0)
end
