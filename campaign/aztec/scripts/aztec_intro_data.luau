-- aztec_intro_data: the level intro as data, from specs/aztec.md (Cinematics: Intro), in the units of
-- specs/intro.md (world units, API degrees, seconds); played by intro.play() (scripts/intro.luau).
-- A data module: nothing runs at its top level.

return {
    lines = { 0x1417 },   -- the location title; no second line: a 5 s shot
    cameras = {   -- 3, one picked at random: position, API yaw / pitch (fov 60)
        { pos = { 1837.78, 497.69, 653.66 }, yaw = 113.6, pitch = -16.0, pad = 21 },   -- camera 0, room of pad 21
        { pos = { -1817.01, -876.52, 4073.64 }, yaw = -106.1, pitch = 24.0, pad = 63 },   -- camera 1, room of pad 63
        { pos = { -4440.56, 674.48, 2000.45 }, yaw = 121.1, pitch = -24.0, pad = 40 },   -- camera 2, room of pad 40
    },
    swirl = {   -- P0..Pn: offset from the eye point, duration and tension of the leg after the point,
        -- flags (2: player frame, x left, y up, z forward; 4: look at the eye); the camera starts at P1
        -- and stops at P(n-1); swirl length 3.001 s (the durations of P1 to P(n-2))
        { off = { 0, -100, 200 }, time = 0.667, tension = 0.75, flags = 2 },   -- P0 (only the previous control point of the first leg: its leg never plays)
        { off = { 0, -100, 200 }, time = 0.667, tension = 0.75, flags = 2 },   -- P1
        { off = { 10, -100, 170 }, time = 0.667, tension = 0.75, flags = 2 },   -- P2
        { off = { 50, -20, 0 }, time = 0.667, tension = 0.75, flags = 2 },   -- P3
        { off = { 20, 37.5, -77.5 }, time = 0.667, tension = 0.75, flags = 2 },   -- P4
        { off = { 0, 10, -30 }, time = 0.333, tension = 0.25, flags = 2 },   -- P5
        { off = { 0, 0, 0 }, flags = 2 },   -- P6 (final)
        { off = { 0, 0, 0 }, flags = 2 },   -- P7 (shape only)
        { flags = 1 },   -- the end marker
    },
    intro_anim = 0,   -- no record: table entry 0 (intro.md section 3)
    start_pad = 130,
    weapon = 4,   -- the first start item (PP7)
}
