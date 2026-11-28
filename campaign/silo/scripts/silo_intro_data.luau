-- silo_intro_data: the level intro as data, from specs/silo.md (Cinematics: Intro), in the units of
-- specs/intro.md (world units, API degrees, seconds); played by intro.play() (scripts/intro.luau).
-- A data module: nothing runs at its top level.

return {
    lines = { 0x8844 },   -- the location title; no second line: a 5 s shot
    cameras = {   -- 1, one picked at random: position, API yaw / pitch (fov 60)
        { pos = { 5121.78, -1043.25, 2746.10 }, yaw = -128.4, pitch = -16.0, pad = 129 },   -- camera 0, room of pad 129
    },
    swirl = {   -- P0..Pn: offset from the eye point, duration and tension of the leg after the point,
        -- flags (2: player frame, x left, y up, z forward; 4: look at the eye); the camera starts at P1
        -- and stops at P(n-1); swirl length 5.526 s (the durations of P1 to P(n-2))
        { off = { -108, 2087, 700 }, time = 1.167, tension = 0.65, flags = 2 },   -- P0 (only the previous control point of the first leg: its leg never plays)
        { off = { -108, 2087, 700 }, time = 1.167, tension = 0.65, flags = 2 },   -- P1
        { off = { -88, 1557, 700 }, time = 1, tension = 0.65, flags = 2 },   -- P2
        { off = { 97, 972, 714 }, time = 0.833, tension = 0.65, flags = 2 },   -- P3
        { off = { 266, 488, 508 }, time = 0.75, tension = 0.65, flags = 2 },   -- P4
        { off = { 284, 126, 190 }, time = 0.515, tension = 0.544, flags = 2 },   -- P5
        { off = { 195, 20, 33 }, time = 0.505, tension = 0.502, flags = 2 },   -- P6
        { off = { 61, 7, -29 }, time = 0.423, tension = 0.455, flags = 2 },   -- P7
        { off = { 0, 10, -30 }, time = 0.333, tension = 0.25, flags = 2 },   -- P8
        { off = { 0, 0, 0 }, flags = 2 },   -- P9 (final)
        { off = { 0, 0, 0 }, flags = 2 },   -- P10 (shape only)
        { flags = 1 },   -- the end marker
    },
    intro_anim = 0,   -- no record: table entry 0 (intro.md section 3)
    body = {
        -- Body during the swirl (a level script): rule 20: animation 98 frames 0 to 1 for 1 s, then frame 1 to its
        -- end, held (blend 16 ticks)
        { at = 0, anim = 98, from = 0, to = 1, speed = 1 / 60, hold = true, blend = 16 / 60 },
        { at = 1, anim = 98, from = 1, hold = true },
    },
    start_pad = 209,
    weapon = 5,   -- the first start item (silenced PP7)
}
