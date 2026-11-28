-- runway_intro_data: the level intro as data, from specs/runway.md (Cinematics: Intro), in the units of
-- specs/intro.md (world units, API degrees, seconds); played by intro.play() (scripts/intro.luau).
-- A data module: nothing runs at its top level.

return {
    lines = { 0x7013 },   -- the location title; no second line: a 5 s shot
    cameras = {   -- 6, one picked at random: position, API yaw / pitch (fov 60)
        { pos = { 5946.68, 569.68, -10252.42 }, yaw = 34.0, pitch = -6.0 },   -- camera 0, room of pad 172
        { pos = { 10614.85, 453.68, -27156.78 }, yaw = -5.0, pitch = -6.8 },   -- camera 1, room of pad 165
        { pos = { 8819.42, 1757.68, -5022.98 }, yaw = -170.3, pitch = -23.2 },   -- camera 2, room of pad 132
        { pos = { 5389.93, 628.58, -6104.64 }, yaw = 116.0, pitch = -24.0 },   -- camera 3, room of pad 39
        { pos = { 5389.83, 129.68, -8253.13 }, yaw = 60.5, pitch = 2.8 },   -- camera 4, room of pad 39
        { pos = { 9252.87, -491.62, -6454.90 }, yaw = -74.6, pitch = 4.0 },   -- camera 5, room of pad 110
    },
    swirl = {   -- P0..Pn: offset from the eye point, duration and tension of the leg after the point,
        -- flags (2: player frame, x left, y up, z forward; 4: look at the eye); the camera starts at P1
        -- and stops at P(n-1); swirl length 4.554 s (the durations of P1 to P(n-2))
        { off = { 0, -100, 200 }, time = 0.667, tension = 0.75, flags = 2 },   -- P0 (only the previous control point of the first leg: its leg never plays)
        { off = { 1, -98, 201 }, time = 0.68, tension = 0.64, flags = 2 },   -- P1
        { off = { -96, -100, 170 }, time = 1.094, tension = 0.75, flags = 2 },   -- P2
        { off = { -181, -20, 0 }, time = 0.991, tension = 0.666, flags = 2 },   -- P3
        { off = { -110, 38, -90 }, time = 0.751, tension = 0.64, flags = 2 },   -- P4
        { off = { -29, 32, -88 }, time = 0.519, tension = 0.402, flags = 2 },   -- P5
        { off = { -5, 10, -40 }, time = 0.519, tension = 0.226, flags = 2 },   -- P6
        { off = { 0, 0, 0 }, flags = 2 },   -- P7 (final)
        { off = { 0, 0, 0 }, flags = 2 },   -- P8 (shape only)
        { flags = 1 },   -- the end marker
    },
    intro_anim = 0,   -- no record: table entry 0 (intro.md section 3)
    body = {
        -- Body during the swirl (a level script): from the swirl start, animation 99 from its start to its end, held
        -- (blend 16 ticks)
        { at = 0, anim = 99, from = 0, hold = true, blend = 16 / 60 },
    },
    start_pad = 53,
    weapon = 5,   -- the first start item (silenced PP7)
}
