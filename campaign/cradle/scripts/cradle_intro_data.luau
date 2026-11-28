-- cradle_intro_data: the level intro as data, from specs/cradle.md (Cinematics: Intro), in the units of
-- specs/intro.md (world units, API degrees, seconds); played by intro.play() (scripts/intro.luau).
-- A data module: nothing runs at its top level.

return {
    lines = { 0x2422 },   -- the location title; no second line: a 5 s shot
    cameras = {   -- 2, one picked at random: position, API yaw / pitch (fov 60)
        { pos = { -5153.33, 622.41, -3235.23 }, yaw = 52.0, pitch = 0.0 },   -- camera 0, room of pad 121
        { pos = { 246.05, -1181.20, -1619.83 }, yaw = -34.4, pitch = 26.0 },   -- camera 1, room of pad 106
    },
    swirl = {   -- P0..Pn: offset from the eye point, duration and tension of the leg after the point,
        -- flags (2: player frame, x left, y up, z forward; 4: look at the eye); the camera starts at P1
        -- and stops at P(n-1); swirl length 4.748 s (the durations of P1 to P(n-2))
        { off = { 181, 0, 4675 }, time = 0.333, tension = 0.6, flags = 2 },   -- P0 (only the previous control point of the first leg: its leg never plays)
        { off = { 181, 0, 4675 }, time = 0.333, tension = 0.6, flags = 2 },   -- P1
        { off = { 167, 0, 4075 }, time = 0.333, tension = 0.6, flags = 2 },   -- P2
        { off = { 163, 0, 3475 }, time = 0.333, tension = 0.6, flags = 2 },   -- P3
        { off = { 149, -6.5, 2875 }, time = 0.333, tension = 0.6, flags = 2 },   -- P4
        { off = { 135, -22, 2275 }, time = 0.333, tension = 0.6, flags = 2 },   -- P5
        { off = { 121, -29, 1675 }, time = 0.333, tension = 0.6, flags = 2 },   -- P6
        { off = { 107, 0, 1075 }, time = 0.333, tension = 0.6, flags = 2 },   -- P7
        { off = { 128, 87, 527 }, time = 0.333, tension = 0.6, flags = 2 },   -- P8
        { off = { 165, 153, 232 }, time = 0.417, tension = 0.591, flags = 2 },   -- P9
        { off = { 173, 133, 31 }, time = 0.5, tension = 0.591, flags = 2 },   -- P10
        { off = { 67, 59, -81 }, time = 0.667, tension = 0.398, flags = 2 },   -- P11
        { off = { 9, 21, -46 }, time = 0.5, tension = 0.422, flags = 2 },   -- P12
        { off = { 0, 0, 0 }, flags = 2 },   -- P13 (final)
        { off = { 0, 0, 0 }, flags = 2 },   -- P14 (shape only)
        { flags = 1 },   -- the end marker
    },
    intro_anim = 0,   -- no record: table entry 0 (intro.md section 3)
    body = {
        -- Body during the swirl (a level script): animation 99 frames 0 to 1 for 0.333 s, then frame 1 to its end,
        -- held (blend 16 ticks)
        { at = 0, anim = 99, from = 0, to = 1, speed = 1 / (60 * 0.333), hold = true, blend = 16 / 60 },
        { at = 0.333, anim = 99, from = 1, hold = true },
    },
    start_pad = 151,
    weapon = 4,   -- the first start item (PP7)
}
