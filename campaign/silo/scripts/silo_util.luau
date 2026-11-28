-- silo_util: the Silo scripts' shared level state (the module is cached, so every Silo script sees the same
-- table). The helpers the levels share are in campaign_lib. A require()d module: nothing here waits.

local U = {}

U.state = {
    exiting = false,         -- the exit (rule 21) has begun
}

return U
