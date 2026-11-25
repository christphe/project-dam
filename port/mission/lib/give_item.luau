-- give_item: gives an item when a player reaches the target.
-- target: a zone (a player enters it) or a prop (a player comes within `radius`, default 120).
-- props: item (item id), message (optional), radius (optional, prop targets).
export type Props = { item: Item, message: string?, radius: number? }

local item = props.item
local radius = props.radius or 120

if target.kind == "zone" then
    wait(event("zone", { id = target.zone }))
else
    while not target:near(radius) do
        wait(0.25)
    end
end

give(item)
if props.message then
    message(props.message)
end
