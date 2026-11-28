-- train_intro_data: the level intro as data, from specs/train.md (Cinematics: Intro), in the units of
-- specs/intro.md (world units, API degrees, seconds); played by intro.play() (scripts/intro.luau).
-- A data module: nothing runs at its top level.

return {
    lines = { 0x902C },   -- the location title; no second line: a 5 s shot
    cameras = {   -- 1, one picked at random: position, API yaw / pitch (fov 60)
        { pos = { -4058.53, 393.69, -94.33 }, yaw = 125.4, pitch = -22.0 },   -- camera 0, room of pad 184
    },
    swirl = {   -- P0..Pn: offset from the eye point, duration and tension of the leg after the point,
        -- flags (2: player frame, x left, y up, z forward; 4: look at the eye); the camera starts at P1
        -- and stops at P(n-1); swirl length 3.066 s (the durations of P1 to P(n-2))
        { off = { 4, -100, 276 }, time = 0.833, tension = 0.75, flags = 2 },   -- P0 (only the previous control point of the first leg: its leg never plays)
        { off = { -113, -103, 235 }, time = 0.75, tension = 0.75, flags = 2 },   -- P1
        { off = { 93, -100, 194 }, time = 0.679, tension = 0.505, flags = 2 },   -- P2
        { off = { 163, -49, 45 }, time = 0.557, tension = 0.495, flags = 2 },   -- P3
        { off = { 112.5, -10, -45.75 }, time = 0.414, tension = 0.495, flags = 2 },   -- P4
        { off = { 38, 10.5, -61.5 }, time = 0.333, tension = 0.5, flags = 2 },   -- P5
        { off = { 2.75, 10, -29 }, time = 0.333, tension = 0.25, flags = 2 },   -- P6
        { off = { 0, 0, 0 }, flags = 2 },   -- P7 (final)
        { off = { 0, 0, 0 }, flags = 2 },   -- P8 (shape only)
        { flags = 1 },   -- the end marker
    },
    intro_anim = 0,   -- no record: table entry 0 (intro.md section 3)
    body = {
        -- Body during the swirl (a level script): animation 100 frames 0 to 1, and from the next tick frame 1 to its
        -- end, held (blend 16 ticks)
        { at = 0, anim = 100, from = 0, to = 1, hold = true, blend = 16 / 60 },
        { at = 1 / 60, anim = 100, from = 1, hold = true },
    },
    start_pad = 186,
    weapon = 4,   -- the first start item (PP7)
}
