on('guard_spotted', function(e) message('spotted ' .. e.chr .. ' ' .. tostring(e.prop) .. ' ' .. e.player) end)
on('guard_heard', function(e) message('heard ' .. e.chr .. ' ' .. e.player) end, { chr = 5001 })
on('guard_near_miss', function(e) message('miss ' .. e.chr .. ' ' .. e.player) end)
on('guard_witnessed', function(e) message('saw ' .. e.chr .. ' ' .. e.what .. ' ' .. e.other) end, { what = 'death' })
