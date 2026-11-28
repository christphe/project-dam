-- bunker1_intro_data: the level intro as data, from specs/bunker1.md (Cinematics: Intro), in the units of
-- specs/intro.md (world units, API degrees, seconds); played by intro.play() (scripts/intro.luau).
-- A data module: nothing runs at its top level.

return {
    lines = { 0x782C },   -- the location title; no second line: a 5 s shot
    cameras = {   -- 5, one picked at random: position, API yaw / pitch (fov 60)
        { pos = { 413.31, 114.54, 969.71 }, yaw = -130.0, pitch = 12.0, pad = 51 },   -- camera 0, room of pad 51
        { pos = { -632.66, 189.56, 1674.57 }, yaw = -63.4, pitch = 0.0, pad = 80 },   -- camera 1, room of pad 80
        { pos = { -1961.07, 459.70, 571.07 }, yaw = 52.0, pitch = -22.0, pad = 49 },   -- camera 2, room of pad 49
        { pos = { 260.13, 462.26, 1087.18 }, yaw = -144.4, pitch = -22.0, pad = 51 },   -- camera 3, room of pad 51
        { pos = { -1659.36, 439.61, 1846.35 }, yaw = 153.5, pitch = -16.0, pad = 79 },   -- camera 4, room of pad 79
    },
    swirl = {   -- P0..Pn: offset from the eye point, duration and tension of the leg after the point,
        -- flags (2: player frame, x left, y up, z forward; 4: look at the eye); the camera starts at P1
        -- and stops at P(n-1); swirl length 3.001 s (the durations of P1 to P(n-2))
        { off = { 0, -100, 200 }, time = 0.667, tension = 0.75, flags = 2 },   -- P0 (only the previous control point of the first leg: its leg never plays)
        { off = { 0, -100, 200 }, time = 0.667, tension = 0.75, flags = 2 },   -- P1
        { off = { 10, -100, 170 }, time = 0.667, tension = 0.75, flags = 2 },   -- P2
        { off = { 70, -20, 0 }, time = 0.667, tension = 0.75, flags = 2 },   -- P3
        { off = { 20, 40, -100 }, time = 0.667, tension = 0.75, flags = 2 },   -- P4
        { off = { 0, 10, -30 }, time = 0.333, tension = 0.25, flags = 2 },   -- P5
        { off = { 0, 0, 0 }, flags = 2 },   -- P6 (final)
        { off = { 0, 0, 0 }, flags = 2 },   -- P7 (shape only)
        { flags = 1 },   -- the end marker
    },
    intro_anim = 1,   -- the intro-animation record: table entry 1 (intro.md section 3)
    start_pad = 102,
    weapon = 5,   -- the first start item (silenced PP7)
}
