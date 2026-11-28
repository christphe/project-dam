-- frigate_intro_data: the level intro as data, from specs/frigate.md (Cinematics: Intro), in the units of
-- specs/intro.md (world units, API degrees, seconds); played by intro.play() (scripts/intro.luau).
-- A data module: nothing runs at its top level.

return {
    lines = { 0x3418 },   -- the location title; no second line: a 5 s shot
    cameras = {   -- 5, one picked at random: position, API yaw / pitch (fov 60)
        { pos = { -551.83, 282.51, 4099.82 }, yaw = 162.5, pitch = 8.0, pad = 176 },   -- camera 0, room of pad 176
        { pos = { -340.38, 611.65, -2431.48 }, yaw = 116.6, pitch = -20.0, pad = 90 },   -- camera 1, room of pad 90
        { pos = { 327.95, 283.23, -3618.31 }, yaw = -149.4, pitch = -26.0, pad = 113 },   -- camera 2, room of pad 113
        { pos = { -643.10, 390.29, -6564.73 }, yaw = 49.8, pitch = 12.8, pad = 168 },   -- camera 3, room of pad 168
        { pos = { -4006.07, 522.51, 4115.82 }, yaw = 135.4, pitch = 0.0, pad = 176 },   -- camera 4, room of pad 176
    },
    swirl = {   -- P0..Pn: offset from the eye point, duration and tension of the leg after the point,
        -- flags (2: player frame, x left, y up, z forward; 4: look at the eye); the camera starts at P1
        -- and stops at P(n-1); swirl length 3.668 s (the durations of P1 to P(n-2))
        { off = { 10, 151, 525 }, time = 0.667, tension = 0.75, flags = 2 },   -- P0 (only the previous control point of the first leg: its leg never plays)
        { off = { 10, 151, 525 }, time = 0.667, tension = 0.75, flags = 2 },   -- P1
        { off = { 20, 127, 429 }, time = 0.667, tension = 0.75, flags = 2 },   -- P2
        { off = { 80, 67, 179 }, time = 0.667, tension = 0.75, flags = 2 },   -- P3
        { off = { 80, 31, 0 }, time = 0.667, tension = 0.75, flags = 2 },   -- P4
        { off = { 20, 34, -100 }, time = 0.667, tension = 0.75, flags = 2 },   -- P5
        { off = { 0, 10, -30 }, time = 0.333, tension = 0.25, flags = 2 },   -- P6
        { off = { 0, 0, 0 }, flags = 2 },   -- P7 (final)
        { off = { 0, 0, 0 }, flags = 2 },   -- P8 (shape only)
        { flags = 1 },   -- the end marker
    },
    intro_anim = 0,   -- no record: table entry 0 (intro.md section 3)
    start_pad = 176,
    weapon = 11,   -- the first start item (silenced D5K)
}
