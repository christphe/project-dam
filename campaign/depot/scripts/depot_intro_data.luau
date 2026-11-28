-- depot_intro_data: the level intro as data, from specs/depot.md (Cinematics: Intro), in the units of
-- specs/intro.md (world units, API degrees, seconds); played by intro.play() (scripts/intro.luau).
-- A data module: nothing runs at its top level.

return {
    lines = { 0x300E },   -- the location title; no second line: a 5 s shot
    cameras = {   -- 3, one picked at random: position, API yaw / pitch (fov 60)
        { pos = { 1654.21, 567.21, -2095.90 }, yaw = -44.7, pitch = -28.0, pad = 172 },   -- camera 0, room of pad 172
        { pos = { 4074.28, -32.78, -2285.37 }, yaw = 28.8, pitch = 20.0, pad = 247 },   -- camera 1, room of pad 247
        { pos = { -464.20, -132.78, -139.85 }, yaw = -125.3, pitch = -2.0, pad = 268 },   -- camera 2, room of pad 268
    },
    swirl = {   -- P0..Pn: offset from the eye point, duration and tension of the leg after the point,
        -- flags (2: player frame, x left, y up, z forward; 4: look at the eye); the camera starts at P1
        -- and stops at P(n-1); swirl length 5.515 s (the durations of P1 to P(n-2))
        { off = { -93, -18, 2525 }, time = 0.833, tension = 0.6, flags = 2 },   -- P0 (only the previous control point of the first leg: its leg never plays)
        { off = { -253, 21, 1606 }, time = 0.917, tension = 0.6, flags = 2 },   -- P1
        { off = { -93, -59, 900 }, time = 0.917, tension = 0.6, flags = 2 },   -- P2
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
    start_pad = 272,
    weapon = 4,   -- the first start item (PP7)
}
