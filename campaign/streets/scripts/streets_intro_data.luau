-- streets_intro_data: the level intro as data, from specs/streets.md (Cinematics: Intro), in the units of
-- specs/intro.md (world units, API degrees, seconds); played by intro.play() (scripts/intro.luau).
-- A data module: nothing runs at its top level.

return {
    lines = { 0x641C },   -- the location title; no second line: a 5 s shot
    cameras = {   -- 2, one picked at random: position, API yaw / pitch (fov 60)
        { pos = { 4811.61, 136.48, 12699.27 }, yaw = -36.1, pitch = -8.0 },   -- camera 0, room of pad 59
        { pos = { 5787.50, 96.40, 6085.60 }, yaw = -139.9, pitch = -6.0 },   -- camera 1, room of pad 274
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
    start_pad = 245,
    weapon = 4,   -- the first start item (PP7)
}
