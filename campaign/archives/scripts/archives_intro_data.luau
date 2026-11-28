-- archives_intro_data: the level intro as data, from specs/archives.md (Cinematics: Intro), in the units of
-- specs/intro.md (world units, API degrees, seconds); played by intro.play() (scripts/intro.luau).
-- A data module: nothing runs at its top level.

return {
    lines = { 0x082D },   -- the location title; no second line: a 5 s shot
    cameras = {   -- 2, one picked at random: position, API yaw / pitch (fov 60)
        { pos = { -1940.73, -8.26, -1037.23 }, yaw = -36.0, pitch = 18.0 },   -- camera 0, room of pad 69
        { pos = { -494.42, 417.84, -1958.09 }, yaw = -131.4, pitch = -6.0 },   -- camera 1, room of pad 55
    },
    swirl = {   -- P0..Pn: offset from the eye point, duration and tension of the leg after the point,
        -- flags (2: player frame, x left, y up, z forward; 4: look at the eye); the camera starts at P1
        -- and stops at P(n-1); swirl length 3.944 s (the durations of P1 to P(n-2))
        { off = { -43, -118, 641 }, time = 0.528, tension = 0.7, flags = 2 },   -- P0 (only the previous control point of the first leg: its leg never plays)
        { off = { -43, -118, 641 }, time = 0.528, tension = 0.7, flags = 2 },   -- P1
        { off = { 28, -118, 648 }, time = 0.74, tension = 0.7, flags = 2 },   -- P2
        { off = { 134, -23, 501 }, time = 0.695, tension = 0.647, flags = 2 },   -- P3
        { off = { 162, 7, 246 }, time = 0.552, tension = 0.586, flags = 2 },   -- P4
        { off = { 118, 10, 71 }, time = 0.521, tension = 0.531, flags = 2 },   -- P5
        { off = { 40, 10, -18 }, time = 0.481, tension = 0.402, flags = 2 },   -- P6
        { off = { 2, 10, -30 }, time = 0.427, tension = 0.25, flags = 2 },   -- P7
        { off = { 0, 0, 0 }, flags = 2 },   -- P8 (final)
        { off = { 0, 0, 0 }, flags = 2 },   -- P9 (shape only)
        { flags = 1 },   -- the end marker
    },
    intro_anim = 0,   -- no record: table entry 0 (intro.md section 3)
    start_pad = 192,
    weapon = 1,   -- the first start item (fist)
}
