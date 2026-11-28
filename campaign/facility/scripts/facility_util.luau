-- facility_util: the Facility scripts' shared level state (the module is cached, so every Facility script sees the
-- same table). The helpers the levels share are in campaign_lib. A require()d module: nothing here waits.

local U = {}

U.state = {
    exiting = false,         -- the exit (rule 36) has begun: no more spawns
    ambush = false,          -- rule 29 has begun
    ourumov_arrived = false, -- rule 31
    fight = false,           -- the fight of rules 32-33 has started
    ourumov_fighting = false,
    ourumov_fled = false,
    gas_at = nil :: number?, -- clock() when the gas leak began (rule 35)
    gas_done = false,        -- the gas has leaked 5 s (rule 35)
}

return U
