-- facility_intro_data: the level intro as data, from specs/facility.md (Cinematics: Intro), in the units of
-- specs/intro.md (world units, API degrees, seconds); played by intro.play() (scripts/intro.luau).
-- A data module: nothing runs at its top level.

return {
    lines = { 0x0C46 },   -- the location title; no second line: a 5 s shot
    cameras = {   -- 4, one picked at random: position, API yaw / pitch (fov 60)
        { pos = { 8669.05, -359.25, -3541.46 }, yaw = -17.1, pitch = 12.0, pad = 104 },   -- camera 0, room of pad 104
        { pos = { 8447.07, -419.25, -3962.70 }, yaw = 12.8, pitch = 18.0, pad = 104 },   -- camera 1, room of pad 104
        { pos = { 524.66, -397.93, 565.66 }, yaw = -7.1, pitch = 10.0, pad = 2 },   -- camera 2, room of pad 2
        { pos = { 8270.73, 280.74, -3480.91 }, yaw = -27.2, pitch = -14.0, pad = 104 },   -- camera 3, room of pad 104
    },
    swirl = {   -- P0..Pn: offset from the eye point, duration and tension of the leg after the point,
        -- flags (2: player frame, x left, y up, z forward; 4: look at the eye); the camera starts at P1
        -- and stops at P(n-1); swirl length 3.336 s (the durations of P1 to P(n-2))
        { off = { 0, 595, -40 }, time = 0.417, tension = 0.75, flags = 6 },   -- P0 (only the previous control point of the first leg: its leg never plays)
        { off = { -40, 510, 0 }, time = 0.417, tension = 0.75, flags = 6 },   -- P1
        { off = { 0, 425, 40 }, time = 0.417, tension = 0.75, flags = 6 },   -- P2
        { off = { 45, 340, 0 }, time = 0.417, tension = 0.75, flags = 6 },   -- P3
        { off = { 0, 255, -45 }, time = 0.417, tension = 0.75, flags = 6 },   -- P4
        { off = { -45, 170, 0 }, time = 0.417, tension = 0.75, flags = 6 },   -- P5
        { off = { 0, 85, 40 }, time = 0.417, tension = 0.75, flags = 6 },   -- P6
        { off = { 35, 20, 5 }, time = 0.417, tension = 0.75, flags = 6 },   -- P7
        { off = { 10, 0, -25 }, time = 0.417, tension = 0.75, flags = 6 },   -- P8
        { off = { 0, 0, 0 }, flags = 2 },   -- P9 (final)
        { off = { 0, 0, 0 }, flags = 2 },   -- P10 (shape only)
        { flags = 1 },   -- the end marker
    },
    intro_anim = 0,   -- no record: table entry 0 (intro.md section 3)
    start_pad = 167,
    weapon = 5,   -- the first start item (silenced PP7)
}
