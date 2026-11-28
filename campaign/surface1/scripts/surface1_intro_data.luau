-- surface1_intro_data: the level intro as data, from specs/surface1.md (Cinematics: Intro), in the units of
-- specs/intro.md (world units, API degrees, seconds); played by intro.play() (scripts/intro.luau).
-- A data module: nothing runs at its top level.

return {
    lines = { 0x7C14, 0x7C15 },   -- the location title, then the time caption: an 8 s shot
    cameras = {   -- 3, one picked at random: position, API yaw / pitch (fov 60)
        { pos = { -467.68, 374.25, -7190.45 }, yaw = 153.6, pitch = 22.0, pad = 59 },   -- camera 0, room of pad 59
        { pos = { 3453.03, 2014.25, -9406.21 }, yaw = -116.6, pitch = -16.0, pad = 59 },   -- camera 1, room of pad 59
        { pos = { -6199.34, 198.88, -16040.75 }, yaw = 75.4, pitch = 0.0, pad = 59 },   -- camera 2, room of pad 59
    },
    swirl = {   -- P0..Pn: offset from the eye point, duration and tension of the leg after the point,
        -- flags (2: player frame, x left, y up, z forward; 4: look at the eye); the camera starts at P1
        -- and stops at P(n-1); swirl length 3.864 s (the durations of P1 to P(n-2))
        { off = { 181, 0, 4675 }, time = 0.333, tension = 0.6, flags = 2 },   -- P0 (only the previous control point of the first leg: its leg never plays)
        { off = { 181, 0, 4675 }, time = 0.333, tension = 0.6, flags = 2 },   -- P1
        { off = { 167, 0, 4075 }, time = 0.333, tension = 0.6, flags = 2 },   -- P2
        { off = { 163, 0, 3475 }, time = 0.333, tension = 0.6, flags = 2 },   -- P3
        { off = { 149, 0, 2875 }, time = 0.333, tension = 0.6, flags = 2 },   -- P4
        { off = { 135, -17, 2275 }, time = 0.333, tension = 0.6, flags = 2 },   -- P5
        { off = { 121, -29, 1675 }, time = 0.333, tension = 0.6, flags = 2 },   -- P6
        { off = { 107, 0, 1075 }, time = 0.333, tension = 0.6, flags = 2 },   -- P7
        { off = { 128, 87, 527 }, time = 0.333, tension = 0.6, flags = 2 },   -- P8
        { off = { 165, 153, 172 }, time = 0.333, tension = 0.591, flags = 2 },   -- P9
        { off = { 162, 133, -80 }, time = 0.333, tension = 0.591, flags = 2 },   -- P10
        { off = { 67, 59, -174 }, time = 0.267, tension = 0.431, flags = 2 },   -- P11
        { off = { 9, 21, -106 }, time = 0.267, tension = 0.422, flags = 2 },   -- P12
        { off = { 0, 0, 0 }, flags = 2 },   -- P13 (final)
        { off = { 0, 0, 0 }, flags = 2 },   -- P14 (shape only)
        { flags = 1 },   -- the end marker
    },
    intro_anim = 0,   -- no record: table entry 0 (intro.md section 3)
    body = {
        -- Body during the swirl (a level script): animation 102 frames 0 to 1 for 0.833 s, then frame 1 to its end,
        -- held (blend 16 ticks)
        { at = 0, anim = 102, from = 0, to = 1, speed = 1 / (60 * 0.833), hold = true, blend = 16 / 60 },
        { at = 0.833, anim = 102, from = 1, hold = true },
    },
    start_pad = 45,
    weapon = 5,   -- the first start item (silenced PP7)
}
