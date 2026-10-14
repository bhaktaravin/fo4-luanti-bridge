-- fo4bridge: Luanti side of the Fallout 4 <-> Luanti bridge.
--
-- Sends block place/dig events and your position to bridge.py, and shows the
-- Fallout 4 player as a marker in this world.
--
-- REQUIRED: add this line to luanti.conf (or minetest.conf), or set it in
-- Settings > All settings > "HTTP mods":
--     secure.http_mods = fo4bridge

local core = core or minetest
local BRIDGE = core.settings:get("fo4bridge.url") or "http://127.0.0.1:8765"
local SYNC_INTERVAL = 0.15 -- seconds between syncs

local http = core.request_http_api()
if not http then
	core.log("error", "[fo4bridge] HTTP access denied. Add 'secure.http_mods = fo4bridge' to luanti.conf and restart.")
	core.register_on_joinplayer(function(player)
		core.chat_send_player(player:get_player_name(),
			"[fo4bridge] Not connected: add 'secure.http_mods = fo4bridge' to luanti.conf, then restart.")
	end)
	return
end

local pending = {}          -- events waiting to be sent
local in_flight = false     -- one request at a time
local connected = false
local marker = nil          -- the Fallout player entity

local function vec(p) return {x = p.x, y = p.y, z = p.z} end

-- ---------------------------------------------------------------- events
core.register_on_placenode(function(pos, newnode)
	pending[#pending + 1] = {type = "place", pos = vec(pos), node = newnode.name}
end)

core.register_on_dignode(function(pos, oldnode)
	pending[#pending + 1] = {type = "dig", pos = vec(pos), node = oldnode.name}
end)

-- ---------------------------------------------------------------- marker
core.register_entity("fo4bridge:fallout_player", {
	initial_properties = {
		visual = "cube",
		visual_size = {x = 0.6, y = 1.8, z = 0.6},
		collisionbox = {-0.3, -0.9, -0.3, 0.3, 0.9, 0.3},
		physical = false,
		pointable = true,
		static_save = false, -- don't persist in the world file
		textures = {
			"[fill:16x16:#3a6ea5", "[fill:16x16:#3a6ea5", "[fill:16x16:#e8c547",
			"[fill:16x16:#e8c547", "[fill:16x16:#e8c547", "[fill:16x16:#e8c547",
		}, -- Vault-suit blue and yellow
		nametag = "Fallout 4 Player",
		nametag_color = "#e8c547",
	},
})

local function update_marker(fp)
	if not fp then
		if marker and marker:get_pos() then marker:remove() end
		marker = nil
		return
	end
	-- Fallout gives feet position; the marker is centred, so lift it.
	local pos = {x = fp.x, y = fp.y + 0.9, z = fp.z}
	if not marker or not marker:get_pos() then
		marker = core.add_entity(pos, "fo4bridge:fallout_player")
		return
	end
	marker:move_to(pos, true) -- interpolated
	if fp.angle then
		-- Fallout yaw is clockwise from north; Luanti yaw is counter-clockwise.
		marker:set_yaw(-fp.angle)
	end
end

-- ---------------------------------------------------------------- sync loop
local timer = 0
core.register_globalstep(function(dtime)
	timer = timer + dtime
	if timer < SYNC_INTERVAL or in_flight then return end
	timer = 0

	local body = {events = pending}
	local players = core.get_connected_players()
	if players[1] then body.player = vec(players[1]:get_pos()) end

	local sent = pending
	pending = {}
	in_flight = true

	http.fetch({
		url = BRIDGE .. "/luanti/sync",
		method = "POST",
		data = core.write_json(body),
		extra_headers = {"Content-Type: application/json"},
		timeout = 2,
	}, function(res)
		in_flight = false
		if not res.succeeded or res.code ~= 200 then
			-- put the events back so nothing is lost
			for i = #sent, 1, -1 do table.insert(pending, 1, sent[i]) end
			if connected then
				connected = false
				core.chat_send_all("[fo4bridge] Lost connection to bridge.py")
			end
			return
		end
		if not connected then
			connected = true
			core.chat_send_all("[fo4bridge] Connected to bridge.py")
		end
		local reply = core.parse_json(res.data)
		if reply then update_marker(reply.fallout_player) end
	end)
end)

-- ---------------------------------------------------------------- commands
core.register_chatcommand("fo_anchor", {
	description = "Make your current position line up with the Fallout 4 player's anchor point",
	func = function(name)
		local player = core.get_player_by_name(name)
		if not player then return false, "Player not found" end
		local p = vector.round(player:get_pos())
		http.fetch({
			url = BRIDGE .. "/luanti/anchor",
			method = "POST",
			data = core.write_json(vec(p)),
			extra_headers = {"Content-Type: application/json"},
			timeout = 2,
		}, function(res)
			core.chat_send_player(name, res.succeeded
				and ("[fo4bridge] Anchor set at " .. core.pos_to_string(p))
				or "[fo4bridge] Couldn't reach bridge.py")
		end)
		return true, "[fo4bridge] Setting anchor..."
	end,
})

core.register_chatcommand("fo_status", {
	description = "Show the Fallout 4 bridge status",
	func = function(name)
		http.fetch({url = BRIDGE .. "/status", timeout = 2}, function(res)
			if not res.succeeded then
				core.chat_send_player(name, "[fo4bridge] bridge.py isn't running")
				return
			end
			local s = core.parse_json(res.data) or {}
			core.chat_send_player(name, ("[fo4bridge] Fallout: %s | blocks synced: %d"):format(
				s.fallout_connected and "connected" or "waiting", s.events or 0))
		end)
		return true
	end,
})
