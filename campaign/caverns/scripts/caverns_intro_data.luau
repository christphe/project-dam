-- caverns_intro_data: the level intro as data, from specs/caverns.md (Cinematics: Intro), in the units of
-- specs/intro.md (world units, API degrees, seconds); played by intro.play() (scripts/intro.luau).
-- A data module: nothing runs at its top level.

return {
    lines = { 0x1C19 },   -- the location title; no second line: a 5 s shot
    cameras = {   -- 4, one picked at random: position, API yaw / pitch (fov 60)
        { pos = { 852.71, -3343.55, 689.46 }, yaw = 55.3, pitch = 38.0 },   -- camera 0, room of pad 224
        { pos = { 920.50, 976.44, 737.37 }, yaw = 57.5, pitch = -46.0 },   -- camera 1, room of pad 224
        { pos = { -7733.62, 718.16, -3006.01 }, yaw = 31.4, pitch = -20.0 },   -- camera 2, room of pad 303
        { pos = { 2121.75, -2774.07, 364.46 }, yaw = -22.0, pitch = 42.0 },   -- camera 3, room of pad 224
    },
    swirl = {   -- P0..Pn: offset from the eye point, duration and tension of the leg after the point,
        -- flags (2: player frame, x left, y up, z forward; 4: look at the eye); the camera starts at P1
        -- and stops at P(n-1); swirl length 4.68 s (the durations of P1 to P(n-2))
        { off = { -101, -159, 122 }, time = 1.167, tension = 0.557, flags = 2 },   -- P0 (only the previous control point of the first leg: its leg never plays)
        { off = { -101, -159, 124 }, time = 1.167, tension = 0.557, flags = 2 },   -- P1
        { off = { 72, -142, 119 }, time = 1.167, tension = 0.557, flags = 2 },   -- P2
        { off = { 129, -79, 0 }, time = 1.167, tension = 0.557, flags = 2 },   -- P3
        { off = { 23, 14, -74 }, time = 0.846, tension = 0.495, flags = 2 },   -- P4
        { off = { 0.562, 10, -30 }, time = 0.333, tension = 0.25, flags = 2 },   -- P5
        { off = { 0, 0, 0 }, flags = 2 },   -- P6 (final)
        { off = { 0, 0, 0 }, flags = 2 },   -- P7 (shape only)
        { flags = 1 },   -- the end marker
    },
    intro_anim = 0,   -- no record: table entry 0 (intro.md section 3)
    body = {
        -- Body during the swirl (a level script): animation 98 frames 0 to 1, and from the next tick frame 1 to its
        -- end, held (blend 16 ticks)
        { at = 0, anim = 98, from = 0, to = 1, hold = true, blend = 16 / 60 },
        { at = 1 / 60, anim = 98, from = 1, hold = true },
    },
    start_pad = 368,
    weapon = 9,   -- the first start item (ZMG)
}
