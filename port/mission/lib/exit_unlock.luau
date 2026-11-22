-- exit_unlock: once every objective is complete, a door unlocks or a zone ends the mission.
-- target: a door prop (unlocked and opened) or a zone (entering it wins the mission).
-- props: message (optional, shown when the exit opens).
while not objectives.all_complete() do
    wait(event("objective"))
end

if props.message then
    message(props.message)
end

if target.kind == "zone" then
    while not target:near() do
        wait(0.25)
    end
    mission.win()
else
    target:unlock()
    target:open()
end
