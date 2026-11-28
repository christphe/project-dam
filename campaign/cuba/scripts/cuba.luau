-- cuba: the Cuba ending (specs/cuba.md): one scene, no gameplay. Bond and Natalya on the clearing with their
-- animation loop (rules 2-3), the orbiting camera, three dialogue lines, the credits, then the level ends (rule 4).
local guard_ai = require("guard_ai")
local L = require("campaign_lib")
guard_ai.boot()

local NATALYA = 0x00

-- rule 1: the closing scene at once; HUD hidden except the top text line, weapon hidden, controls locked.
-- TODO(api): no verb stops the mission clock (cinema.begin does not say it does).
cinema.begin({ keep_top_text = true })
for _, i in L.players() do
    player(i):hide_weapon(true)
    if i > 0 then
        bond_body(i):show(false)   -- Co-op: only player 1's Bond body is shown
    end
end

-- rule 2: the couple on their pads, each on its animation
local natalya: Target = chr(NATALYA)
guard_ai.stop(natalya)
natalya:teleport(0x2D)
local bond: Target = bond_body(0)
bond:teleport(0x2C)
bond:show(true)
-- rule 2: whole animation with its root translation, played once and held on its last frame (nothing loops)
bond:anim(0xB5, { hold = true })
natalya:anim(0xB6, { hold = true })

-- rule 3: each time Bond's play ends, the pair picks the next shared moment
spawn(function()
    while true do
        wait(0.25)
        if bond:anim_done() then
            local from: number, to: number? = 158, 250
            if math.random() * 100 >= 86 then
                from, to = 259, nil   -- 14%: frames 259 to the end
            end
            -- no root translation, once, held on the last frame; only Bond's play is watched
            bond:anim(0xB5, { from = from, to = to, move = false, blend = 0.5, hold = true })
            natalya:anim(0xB6, { from = from, to = to, move = false, blend = 0.5, hold = true })
        end
    end
end)

-- Cinematics 1: third-person orbit around the couple (the midpoint of pads 0x2C and 0x2D). Distance 100, 18 units
-- up, aimed 70 units below, starting at 0x1C71 (40 degrees), 46/65536 turn per tick (about 15.2 degrees per second).
camera.orbit({
    centre = { pad(0x2C), pad(0x2D) },
    distance = 100,
    height = 18,
    look_height = -70,
    start_angle = 0x1C71 * 360 / 65536,
    speed = 46 * 60 * 360 / 65536,
})

-- Cinematics 2-5: fade in, then the three lines
screen.fade_in()
L.wait_fade({ poll = 0.1 })
wait(0.5)
L.top("5008")
wait(3)
L.top("5009")
wait(3)
L.top("500A")
-- Cinematics 6: the credits, the orbit and the animation loop going on behind them
wait(5)
credits.roll()
while not credits.done() do
    wait(0.25)
end
-- Cinematics 7, rule 4: fade to black and the level ends as completed
screen.fade_out()
L.wait_fade({ poll = 0.1 })
mission.win()
