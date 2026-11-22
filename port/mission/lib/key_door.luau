-- key_door: a door that stays locked until a player has the key item.
-- target: a door prop.  props: key (item id), message (shown near the locked door),
--         objective (optional objective id completed when the door unlocks).
local key = props.key
local text = props.message or "Locked"
local cooldown = 0

target:lock()
while not has(key) do
    if cooldown <= 0 and target:near(160) then
        message(text)
        cooldown = 3
    end
    wait(0.25)
    cooldown = cooldown - 0.25
end

target:unlock()
message("Unlocked")
if props.objective then
    objectives.complete(props.objective)
end
