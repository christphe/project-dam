-- dam_intro_data: the level intro as data, from specs/dam.md (Cinematics: Intro), in the units of
-- specs/intro.md (world units, API degrees, seconds); played by intro.play() (scripts/intro.luau).
-- A data module: nothing runs at its top level.

return {
    lines = { 0x2C1A, 0x2C1B },   -- the location title, then the time caption: an 8 s shot
    cameras = {   -- 6, one picked at random: position, API yaw / pitch (fov 60)
        { pos = { 17608.08, -59.79, 17408.57 }, yaw = 162.4, pitch = 26.0 },   -- camera 0, room of pad 312
        { pos = { 16740.06, 1261.11, 20253.95 }, yaw = 170.1, pitch = -28.0 },   -- camera 1, room of pad 298
        { pos = { 8947.16, 2920.27, 4098.50 }, yaw = -146.6, pitch = -22.0 },   -- camera 2, room of pad 269
        { pos = { 16872.39, 60.30, 9502.19 }, yaw = 155.1, pitch = 6.0 },   -- camera 3, room of pad 6
        { pos = { 5248.06, 480.29, -8941.07 }, yaw = -41.2, pitch = -12.0 },   -- camera 4, room of pad 69
        { pos = { 7424.92, 1160.29, 3484.91 }, yaw = -143.2, pitch = -12.0 },   -- camera 5, room of pad 273
    },
    swirl = {   -- P0..Pn: offset from the eye point, duration and tension of the leg after the point,
        -- flags (2: player frame, x left, y up, z forward; 4: look at the eye); the camera starts at P1
        -- and stops at P(n-1); swirl length 9.89 s (the durations of P1 to P(n-2))
        { off = { 2812, 930, 5168 }, time = 1.667, tension = 0.5, flags = 2 },   -- P0 (only the previous control point of the first leg: its leg never plays), room of pad 287
        { off = { 2812, 930, 5168 }, time = 2.199, tension = 0.5, flags = 2 },   -- P1, room of pad 287
        { off = { 2800, 450, 3852 }, time = 1.667, tension = 0.5, flags = 2 },   -- P2, room of pad 287
        { off = { 2264, 418, 3283 }, time = 1.667, tension = 0.5, flags = 2 },   -- P3, room of pad 311
        { off = { 1196, 176, 2448 }, time = 1.667, tension = 0.5, flags = 2 },   -- P4
        { off = { -52, -48, 1551 }, time = 1.422, tension = 0.51, flags = 2 },   -- P5
        { off = { -100, -6, 12 }, time = 0.518, tension = 0.175, flags = 2 },   -- P6
        { off = { 0, 14, -107 }, time = 0.75, tension = 0.5, flags = 2 },   -- P7
        { off = { 0, 0, 0 }, flags = 2 },   -- P8 (final)
        { off = { 0, 0, 0 }, flags = 2 },   -- P9 (shape only)
        { flags = 1 },   -- the end marker
    },
    intro_anim = 0,   -- no record: table entry 0 (intro.md section 3)
    body = {
        -- Body during the swirl (a level script): the entry 0 pose for 6 s, then animation 99 from its start to frame
        -- 96, held (blend 16 ticks)
        { at = 6, anim = 99, from = 0, to = 96, hold = true, blend = 16 / 60 },
    },
    start_pad = 33,
    weapon = 5,   -- the first start item (silenced PP7)
}
