-- pickup_objective: completes an objective when a player picks an item up.
-- target: the mission (any target works).
-- props: item (item id), objective (objective id), message (optional, shown on the pickup).
export type Props = { item: Item, objective: Objective, message: string? }

wait(event("pickup", { item = props.item }))
objectives.complete(props.objective)
if props.message then
    message(props.message)
end
