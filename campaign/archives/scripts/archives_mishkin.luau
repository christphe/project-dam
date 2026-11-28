-- archives_mishkin: Defence Minister Mishkin (chr 1), rules 16-21: he walks up to the player, talks while a player
-- stays close in his room, hands over the safe key at the 30 s line, then walks off and fades out; a missed shot
-- makes him draw his pistol. Exposes `talk_over` (Natalya's hint, rule 11) and `left` (he faded out, not died).
-- A require()d module: no wait() at its top level.
local lib = require("campaign_lib")
local guard_ai = require("guard_ai")

local M = {}

local STEP: number = lib.STEP
local MISHKIN = 1
local ROOM_PAD = 73
local EXIT_PAD = 174
local SAFE_KEY_TAG = 4

M.talk_over = false        -- rule 19: the talk is over
M.left = false             -- rule 19: he faded out (not a death)
M.has_key = false          -- rule 18 / 21: a player has the safe key (co-op: any player)

-- rule 17: the talk, on its own clock
local LINES: { { at: number, id: string } } = {
    { at = 0, id = "0819" }, { at = 5, id = "081A" }, { at = 10, id = "081B" }, { at = 15, id = "081C" },
    { at = 20, id = "081D" }, { at = 25, id = "081E" }, { at = 30, id = "081F" }, { at = 35, id = "0820" },
    { at = 40, id = "0821" },
}
local KEY_AT = 30          -- rule 18
local TALK_END = 45        -- rule 19

function M.key_held(): boolean
    if not M.has_key and tag(SAFE_KEY_TAG):held_by() then
        M.has_key = true
    end
    return M.has_key
end

-- rule 18: the safe key goes to the player he talks to (co-op: the nearest one)
local function give_safe_key(n: number)
    if give_object(tag(SAFE_KEY_TAG), n) then
        M.has_key = true
    end
end

-- A living player within 400 of Mishkin and in the room of pad 73 (co-op: any player): the talk clock runs.
local function listener(m: Target): number?
    for _, n in lib.living() do
        local p = player(n)
        if p:in_room_of(ROOM_PAD) and m:distance_to(p) <= 400 then
            return n
        end
    end
    return nil
end

function M.run()
    local m: Target = chr(MISHKIN)
    if not m:exists() then
        return
    end
    guard_ai.stop(m)
    local drawn = false

    -- rule 20: a shot from a player misses him before he leaves: he draws his TT33 and attacks
    local miss: Connection? = nil
    miss = on("guard_near_miss", function()
        if drawn or M.left or not m:exists() then
            return
        end
        drawn = true
        guard_ai.start(m, "draw_and_fire")
        if miss then
            miss:off()
        end
    end, { chr = MISHKIN })

    -- rule 16: line of sight to a player: he walks up to him (within 300) and faces him
    while m:exists() and not drawn and m:line_to_player() == nil do
        wait(STEP)
    end
    if drawn or not m:exists() then
        return
    end
    local p = lib.nearest_player(m)
    if p ~= nil and m:run_to_player("walk", p) then
        while m:exists() and not drawn and m:is_moving() and m:distance() > 300 do
            wait(STEP)
        end
        if not drawn then
            m:halt()
        end
    end
    if drawn then
        return
    end
    m:face_player()

    -- rule 17: the talk clock runs only while a player listens
    local talk = 0
    local last = clock()
    local nextLine = 1
    local keyDone = false
    while talk < TALK_END do
        if drawn or not m:exists() then
            return                  -- rule 20: the talk never resumes
        end
        local now = clock()
        local who = listener(m)
        if who ~= nil then
            talk += now - last
        end
        last = now
        while nextLine <= #LINES and talk >= LINES[nextLine].at do
            lib.top(LINES[nextLine].id)
            m:face_player()
            nextLine += 1
        end
        if not keyDone and talk >= KEY_AT then
            -- rule 18: at the 30 s line, if no player has it yet
            keyDone = true
            if not M.key_held() then
                give_safe_key(who or lib.nearest_player(m) or 0)
                lib.bottom("0823")
            end
        end
        wait(STEP)
    end

    -- rule 19: the talk is over; he walks to pad 174 and fades out once out of sight or there
    M.talk_over = true
    m:run_to(EXIT_PAD, "walk")
    while m:exists() and not drawn do
        if m:line_to_player() == nil or m:distance_to(pad(EXIT_PAD)) <= 150 then
            M.left = true
            m:fade_out()            -- 1.5 s, then removed
            return
        end
        if not m:is_moving() then
            m:run_to(EXIT_PAD, "walk")   -- stopped elsewhere while still seen: walks on
        end
        wait(STEP)
    end
end

return M
