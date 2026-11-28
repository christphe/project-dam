-- jungle_intro_data: the level intro as data, from specs/jungle.md (Cinematics: Intro), in the units of
-- specs/intro.md (world units, API degrees, seconds); played by intro.play() (scripts/intro.luau).
-- A data module: nothing runs at its top level.

return {
    lines = { 0x4829 },   -- the location title; no second line: a 5 s shot
    cameras = {   -- 3, one picked at random: position, API yaw / pitch (fov 60)
        { pos = { -257.41, 1016.96, 129.32 }, yaw = 3.1, pitch = -34.0 },   -- camera 0, room of pad 514
        { pos = { 5629.52, 826.74, -10144.17 }, yaw = 143.5, pitch = 18.0 },   -- camera 1, room of pad 520
        { pos = { 17940.40, 883.71, -9857.71 }, yaw = -27.0, pitch = 10.0 },   -- camera 2, room of pad 516
    },
    swirl = {   -- P0..Pn: offset from the eye point, duration and tension of the leg after the point,
        -- flags (2: player frame, x left, y up, z forward; 4: look at the eye); the camera starts at P1
        -- and stops at P(n-1); swirl length 3.975 s (the durations of P1 to P(n-2))
        { off = { 599, -107, 653 }, time = 1, tension = 0.346, flags = 2 },   -- P0 (only the previous control point of the first leg: its leg never plays)
        { off = { 593, -106.5, 644 }, time = 1, tension = 0.615, flags = 2 },   -- P1
        { off = { 536, -106, 583 }, time = 0.833, tension = 0.75, flags = 2 },   -- P2
        { off = { 224, -95, 320 }, time = 0.74, tension = 0.546, flags = 2 },   -- P3
        { off = { 156, 8, -9 }, time = 0.583, tension = 0.546, flags = 2 },   -- P4
        { off = { 41, 17, -78 }, time = 0.486, tension = 0.531, flags = 2 },   -- P5
        { off = { 0, 10, -30 }, time = 0.333, tension = 0.209, flags = 2 },   -- P6
        { off = { 0, 0, 0 }, flags = 2 },   -- P7 (final)
        { off = { 0, 0, 0 }, flags = 2 },   -- P8 (shape only)
        { flags = 1 },   -- the end marker
    },
    intro_anim = 0,   -- no record: table entry 0 (intro.md section 3)
    body = {
        -- Body during the swirl (a level script): animation 103 frames 0 to 1 for 1 s, then frame 1 to its end, held
        -- (blend 16 ticks)
        { at = 0, anim = 103, from = 0, to = 1, speed = 1 / 60, hold = true, blend = 16 / 60 },
        { at = 1, anim = 103, from = 1, hold = true },
    },
    start_pad = 514,
    weapon = 4,   -- the first start item (PP7)
}
