-- statue_mishkin: Defence Minister Mishkin and his soldiers at the park gates (specs/statue.md rules 24-26): their
-- arrival, his talk on a distance clock, and the side turning hostile. A require()d module: start() spawns.

local lib = require("campaign_lib")
local sig = require("statue_signal")

local M = {}

local STEP = 0.25
local GATES = 9

local show = lib.bottom

-- rule 26: a soldier's loadout
local function soldier_loadout(g: Target)
    g:set_stats({ grenade_chance = 75 })
    g:give_weapon(8, { left_hand = lib.chance(50) })
    g:give_hat(0xD6)
end

function M.start(S: any)
    spawn(function()
        -- rule 24: Natalya at the gates and objective e complete, then every player more than 50 m from pad 9
        lib.wait_until(function() return S.natalya_at_gates and objectives.is_complete("e") end)
        lib.wait_until(function() return lib.all_farther(pad(GATES), 5000) end)
        local give_up = function() return S.exiting end
        local m = lib.spawn_retry({ chr = 0x0A, pad = 12, body = 19, head = 69, force = true }, give_up)
        if m == nil then
            return
        end
        local mishkin = m :: Target
        S.mishkin = mishkin
        S.mishkin_arrived = true   -- Natalya raises her hands (rule 19)
        mishkin:set_flag("no_autoaim", true)
        mishkin:give_weapon(6)

        -- the five soldiers: chr 0x0B at pad 10, chr 0x0C at pad 11, three more at pads 13, 14, 15
        local cover: { Target } = {}     -- chr 0x0B, 0x0C: keep Natalya covered
        local kneelers: { Target } = {}  -- the other three: kneel and aim at the player
        for _, s in { { 0x0B, 10 }, { 0x0C, 11 } } do
            local g = lib.spawn_retry({ chr = s[1], pad = s[2], body = 19 }, give_up)
            if g then
                soldier_loadout(g)
                table.insert(cover, g)
            end
        end
        for _, p in { 13, 14, 15 } do
            local g = lib.spawn_retry({ pad = p, body = 19 }, give_up)
            if g then
                soldier_loadout(g)
                table.insert(kneelers, g)
            end
        end
        local side: { Target } = { mishkin }
        for _, g in cover do
            table.insert(side, g)
        end
        for _, g in kneelers do
            table.insert(side, g)
        end

        -- rule 25: Mishkin aims his DD44 at the player; rule 26: the soldiers take their posture
        mishkin:fire_at(nil, { aim_only = true })
        for _, g in cover do
            local n = S.natalya
            if n ~= nil and n:exists() then
                g:aim_at(n)
            else
                g:fire_at(nil, { aim_only = true })
            end
        end
        for _, g in kneelers do
            g:fire_at(nil, { stance = "kneel", aim_only = true })
        end

        -- rule 25: a shot missing any of them (the engine's near-miss signal), one of them dying, turns the side
        -- hostile
        spawn(function()
            local missed: { () -> boolean } = {}
            for k, g in side do
                missed[k] = sig.latch(g, "guard_near_miss")
            end
            while not S.mishkin_hostile do
                for k, g in side do
                    if missed[k]() or g:is_dying() or (g:is_dead() and lib.present(g)) then
                        S.mishkin_hostile = true
                        break
                    end
                end
                wait(STEP)
            end
            for _, m in missed do
                sig.off(m)
            end
            -- everyone opens fire on the player and keeps kneeling, aiming and firing
            for _, g in side do
                spawn(function()
                    local first = true
                    while g:exists() do
                        if not g:is_busy() then
                            local n = S.natalya
                            if first and table.find(cover, g) and n ~= nil and n:exists() then
                                g:fire_at(n, { stance = "kneel" })   -- rule 26: first shots at Natalya
                            else
                                g:fire_at(nil, { stance = "kneel" })
                            end
                            first = false
                        end
                        wait(STEP)
                    end
                end)
            end
        end)

        -- rule 25: the talk, once a player is within 15 m, on a clock that runs only within 25 m
        lib.wait_until(function() return S.mishkin_hostile or not mishkin:exists() or mishkin:near(1500) end)
        local LINES: { { at: number, id: string } } = { { at = 5, id = "8C28" }, { at = 10, id = "8C29" },
            { at = 15, id = "8C2A" }, { at = 20, id = "8C2B" }, { at = 25, id = "8C2C" }, { at = 30, id = "8C2D" },
            { at = 40, id = "8C2F" } }
        local t = 0
        local i = 1
        local away = false
        local last = clock()
        while i <= #LINES and mishkin:exists() and not S.mishkin_hostile do
            local now = clock()
            if mishkin:distance() <= 2500 then
                away = false
                t += now - last
                if t >= LINES[i].at then
                    show(LINES[i].id)
                    i += 1
                end
            elseif not away then
                away = true
                show("8C2E")   -- come back (again on each new step away)
            end
            last = now
            wait(STEP)
        end
    end)
end

return M
