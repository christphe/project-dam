-- statue_intro_data: the level intro as data, from specs/statue.md (Cinematics: Intro), in the units of
-- specs/intro.md (world units, API degrees, seconds); played by intro.play() (scripts/intro.luau).
-- A data module: nothing runs at its top level.

return {
    lines = { 0x8C3E },   -- the location title; no second line: a 5 s shot
    cameras = {   -- 3, one picked at random: position, API yaw / pitch (fov 60)
        { pos = { -12550.35, 127.27, 590.90 }, yaw = -55.4, pitch = 16.0 },   -- camera 0, room of pad 217
        { pos = { 248.90, 154.79, 4892.35 }, yaw = -0.9, pitch = 32.0 },   -- camera 1, room of pad 59
        { pos = { -3528.08, -45.22, 4828.61 }, yaw = -16.9, pitch = 6.0 },   -- camera 2, room of pad 104
    },
    swirl = {   -- P0..Pn: offset from the eye point, duration and tension of the leg after the point,
        -- flags (2: player frame, x left, y up, z forward; 4: look at the eye); the camera starts at P1
        -- and stops at P(n-1); swirl length 5.515 s (the durations of P1 to P(n-2))
        { off = { -739, -29, 1417 }, time = 0.833, tension = 0.6, flags = 2 },   -- P0 (only the previous control point of the first leg: its leg never plays)
        { off = { -739, -29, 1417 }, time = 0.917, tension = 0.6, flags = 2 },   -- P1
        { off = { -347, 0, 921 }, time = 0.917, tension = 0.6, flags = 2 },   -- P2
        { off = { 128, 87, 527 }, time = 0.917, tension = 0.6, flags = 2 },   -- P3
        { off = { 253, 153, 172 }, time = 0.833, tension = 0.591, flags = 2 },   -- P4
        { off = { 162, 133, -80 }, time = 0.67, tension = 0.591, flags = 2 },   -- P5
        { off = { 67, 59, -174 }, time = 0.67, tension = 0.431, flags = 2 },   -- P6
        { off = { 9, 21, -106 }, time = 0.591, tension = 0.422, flags = 2 },   -- P7
        { off = { 0, 0, 0 }, flags = 2 },   -- P8 (final)
        { off = { 0, 0, 0 }, flags = 2 },   -- P9 (shape only)
        { flags = 1 },   -- the end marker
    },
    intro_anim = 0,   -- no record: table entry 0 (intro.md section 3)
    body = {
        -- Body during the swirl (a level script): from the next tick, animation 97 from its start to frame 175, held
        -- (blend 16 ticks)
        { at = 1 / 60, anim = 97, from = 0, to = 175, hold = true, blend = 16 / 60 },
    },
    start_pad = 21,
    weapon = 4,   -- the first start item (PP7)
}
