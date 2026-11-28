-- egypt_samedi: Baron Samedi's behaviour in a fight (specs/egyptian.md rules 7, 11-13): each form appears
-- invincible, turns vulnerable after 5 s, laughs and fights when aimed at or when it sees the player, dodging
-- the player's aim. Required by egypt.luau; form(g) runs one form in a task of its own. The top level only
-- defines functions.

local U = require("campaign_lib")
local STEP: number = U.STEP

local M = {}

local LAUGH = 54

local function alive(g: Target): boolean
    return g:exists() and not g:is_dying()
end

local function wait_idle(g: Target, timeout: number)
    wait(STEP)
    U.wait_until(function(): boolean return not alive(g) or not g:is_busy() end, timeout)
end

function M.laugh(g: Target)
    sound.play(LAUGH, { from = g })
end

-- rule 13: a dodge, then back to what he was doing
local DODGES = { { 24, "side_hop" }, { 36, "sidestep" }, { 28, "side_run" }, { 12, "anim" } }
local function dodge(g: Target)
    local m = U.pick(DODGES)
    local ok = false
    if m ~= "anim" then
        ok = g:combat_move(m)
    end
    if not ok then
        g:anim(11, { from = 20, to = 60 })   -- the quick dodge
    end
    wait_idle(g, 3)
end

-- rule 12: an attack
local ATTACKS = {
    { 16, "roll_fire" }, { 16, "walk_fire" }, { 16, "run_fire" }, { 16, "stand" }, { 16, "kneel" },
    { 10, "aim_kneel" }, { 12, "aim_stand" },
}
local function attack(g: Target)
    if math.random() < 0.5 and g:is_aimed_at() then
        dodge(g)
        if not alive(g) then return end
    end
    -- the player he is attacking: the nearest one (co-op notes)
    local p = U.nearest_player(g)
    if p ~= nil and U.health(p) < 0.25 then
        if not g:combat_move("run_fire") then
            g:fire_at(nil, { stance = "stand" })
        end
        wait_idle(g, 5)
        return
    end
    local m = U.pick(ATTACKS)
    local ok = false
    if m == "stand" or m == "kneel" then
        ok = g:fire_at(nil, { stance = m })
    elseif m == "aim_kneel" or m == "aim_stand" then
        local stance = if m == "aim_kneel" then "kneel" else "stand"
        ok = g:fire_at(nil, { stance = stance, aim_only = true })
        wait_idle(g, 1)                       -- the aim settles, then he fires
        ok = g:fire_at(nil, { stance = stance })
    else
        ok = g:combat_move(m)
    end
    if not ok then
        g:fire_at(nil, { stance = "stand" })
    end
    wait_idle(g, 5)
end

-- rule 11: the fight loop
local function fight(g: Target)
    while alive(g) do
        if g:is_busy() then
            wait(STEP)
        elseif math.random() < 0.5 and g:is_aimed_at() then
            dodge(g)                              -- any player aiming at him (co-op notes)
        elseif g:sees_player() then
            attack(g)
        else
            g:run_to_player("run")
            wait(STEP)
            U.wait_until(function(): boolean
                if not alive(g) or g:sees_player() or not g:is_moving() then return true end
                return g:is_aimed_at() and math.random() < 0.5
            end)
            if alive(g) and g:is_aimed_at() and not g:sees_player() then
                dodge(g)                          -- aimed at on the way
            end
        end
    end
end

-- rule 7: one form, from its appearance
function M.form(g: Target)
    spawn(function()
        g:set_invincible(true)
        g:face_player()
        wait(5)
        if not g:exists() then return end
        g:set_invincible(false)
        U.wait_until(function(): boolean
            return not alive(g) or g:is_aimed_at() or g:sees_player()
        end)
        if not alive(g) then return end
        M.laugh(g)
        fight(g)
    end)
end

return M
