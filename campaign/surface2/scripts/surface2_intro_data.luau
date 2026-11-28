-- surface2_intro_data: the level intro as data, from specs/surface2.md (Cinematics: Intro), in the units of
-- specs/intro.md (world units, API degrees, seconds); played by intro.play() (scripts/intro.luau).
-- A data module: nothing runs at its top level.

return {
    lines = { 0x8018 },   -- the location title; no second line: a 5 s shot
    cameras = {   -- 3, one picked at random: position, API yaw / pitch (fov 60)
        { pos = { 4012.26, 2774.39, -10688.95 }, yaw = -109.4, pitch = -22.0, pad = 50 },   -- camera 0, room of pad 50
        { pos = { 2714.40, 2893.51, -10181.77 }, yaw = -149.6, pitch = -30.0, pad = 287 },   -- camera 1, room of pad 287
        { pos = { 5780.34, 193.10, -19523.57 }, yaw = -45.7, pitch = -4.0, pad = 257 },   -- camera 2, room of pad 257
    },
    swirl = {   -- P0..Pn: offset from the eye point, duration and tension of the leg after the point,
        -- flags (2: player frame, x left, y up, z forward; 4: look at the eye); the camera starts at P1
        -- and stops at P(n-1); swirl length 5.333 s (the durations of P1 to P(n-2))
        { off = { -138, -101, 117 }, time = 1, tension = 0.75, flags = 2 },   -- P0 (only the previous control point of the first leg: its leg never plays)
        { off = { -138, -101, 117 }, time = 1, tension = 0.75, flags = 2 },   -- P1
        { off = { -47, -143, 223 }, time = 1, tension = 0.75, flags = 2 },   -- P2
        { off = { 143, -138, 170 }, time = 1, tension = 0.75, flags = 2 },   -- P3
        { off = { 181, -32, 0 }, time = 1, tension = 0.75, flags = 2 },   -- P4
        { off = { 20, 40, -100 }, time = 1, tension = 0.75, flags = 2 },   -- P5
        { off = { 0, 10, -30 }, time = 0.333, tension = 0.25, flags = 2 },   -- P6
        { off = { 0, 0, 0 }, flags = 2 },   -- P7 (final)
        { off = { 0, 0, 0 }, flags = 2 },   -- P8 (shape only)
        { flags = 1 },   -- the end marker
    },
    intro_anim = 0,   -- no record: table entry 0 (intro.md section 3)
    body = {
        -- Body during the swirl (a level script): from the swirl start, animation 98 from its start to frame 99, held
        -- (blend 16 ticks)
        { at = 0, anim = 98, from = 0, to = 99, hold = true, blend = 16 / 60 },
    },
    start_pad = 30,
    weapon = 5,   -- the first start item (silenced PP7)
}
