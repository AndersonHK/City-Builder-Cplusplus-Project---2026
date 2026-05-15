# Transport Network Design Notes

Use this guide when changing road placement, lane topology, road render data, or transport layers.

## Intent
- Roads live outside `Tile` so scalar simulation data stays compact.
- `TransportNetwork` is the storage, dirty-resolution, and publication shell; it should not own lane behavior.
- Roads are containers of lanes. Lanes own type, flow, side span, traversal cost, and render-graphic triggers.
- Transport tiles self-resolve from their own lanes plus the four cardinal neighbor tiles.
- Ground and elevated roads share the same lane and resolved-cell model, even though they render through different upload paths.
- Pathfinding uses directional `(tile, layer, mode)` cost-map nodes. Layers and modes connect only through explicit transfer edges such as future ramps, stations, or parking.

## Current Shape
- `TransportTypes.h` owns shared enums, direction bits, masks, and snapshot structs such as `ResolvedRoadCell`.
- `RoadLane` owns lane behavior and emitted lane placements. Each placement carries a road axis, tile-local side span, lane type, flow, and lane-owned graphic/divider hints.
- `Road` owns road-template construction and stroke expansion. It converts the current tool inputs into clipped per-tile lane placements.
- `TransportTile` owns the authored lanes on one tile/layer and validates merge/replay rules locally, including replayed stroke identity updates during upgrades.
- `RoadRenderState` owns base glyph, arrow glyph, lane graphic mask, and divider packing.
- `TransportCostMap` owns dense outgoing directional costs, capacities, old/new load buffers, sparse transfer edges, A* scratch reuse, and traffic-overlay generation.
- `TransportNetwork` owns layer storage, lot-occupancy rejection, dirty tile neighborhoods, chunk revisions, resolved-cell publication, cost-map rebuilding, traffic-overlay publication, and packed ground-road bytes.

## Lane Rules
- Sidewalks are pedestrian lanes. Crosswalks are not authored lanes; they are pedestrian lane graphics chosen during tile resolution.
- `RoadLaneSurface` is a default graphic surface, not pathing truth. Lane type and flow decide traversal.
- Pedestrian lanes are candidate-authored and active only when they either border empty terrain or continue as the same side/span on both ends of their axis. Inactive candidates stay stored so later road strokes can make first-built sidewalks valid without order dependence.
- Lane connections across an occupied perpendicular road body require matching stroke identity; two opposite stubs may touch opposite halves of the road body without becoming one through lane. When a later stroke both adds new lanes and exactly replays old lanes, the contiguous lanes from that replayed old stroke adopt the later stroke identity so corner-to-four-way upgrades resolve the same as directly built four-way crossings, even when the player draws only the missing arms.
- Same-axis overlap is lane-span validated. Exact replay is accepted; incompatible shifted road bodies are rejected.
- Perpendicular overlap is allowed as lane coexistence inside the same transport tile. Intersection behavior is resolved afterward from lane adjacency.
- A resolved tile aggregates lane type masks, surface masks, costs, travel, exits, junction glyphs, lane graphics, and dividers for renderer/query consumers.
- A pathing lane contributes only the outgoing directions it actually permits. If multiple lanes contribute to the same tile/layer/mode/direction, the cost map keeps the lower cost and accumulates capacity.
- Ground local sidewalks expose adjacent building access for pedestrian and car spawning. Highways, elevated lanes, underground lanes, and through-only lanes do not expose adjacent building access by default.

## Pathfinding And Traffic Loads
- Cost-map nodes use `tile + totalTiles * (mode + modeCount * layer)` so A* can use compact scratch arrays.
- The cost map stores eight outgoing direction slots. Current road lanes populate cardinal directions; diagonal slots are reserved for future connectors.
- A* expands movement edges within one layer/mode and sparse transfer edges for mode/layer changes. There is no implicit connection between overlapping layers.
- Congestion reads immutable old load and writes reassigned traffic into a separate new-load buffer so future per-building path recalculation can run in parallel and reduce deltas afterward.
- Route tie-breaking uses tiny deterministic jitter from the route seed so equivalent alternatives can distribute statistically over repeated sampled updates.

## Crosswalk Rule
A pedestrian lane renders as a crosswalk only when all of these are true:

- The pedestrian lane has a sidewalk graphic edge on this tile.
- The pedestrian lane overlaps a perpendicular car lane in the same tile.
- The perpendicular car lane has car-road continuation on both cardinal ends of its axis.
- The pedestrian lane has pedestrian continuation on both cardinal ends of its own axis.

Otherwise the same pedestrian lane remains a sidewalk. This keeps T-section endpoints from painting half-crosswalks and prevents isolated intersection tiles from inventing crosswalks where pedestrian lanes do not continue through the crossing.

## Render Contract
- Ground road channel 0 is the base glyph.
- Ground road channel 1 is the arrow glyph.
- Ground road channel 2 is the lane graphic mask: sidewalk edges in the low nibble, crosswalk edges in the high nibble.
- Ground road channel 3 is the divider mask: same-direction dividers in the low nibble, opposing-flow dividers in the high nibble.
- Elevated roads consume the same resolved glyph and mask fields through chunked instances.
- Renderer code must consume these masks as presentation data. Lane/crosswalk policy belongs in transport resolution, not shaders.

## Checks
- Build `x64 Release`.
- Build and run `TransportNetworkTests.vcxproj`.
- Verify straight one-way and two-way local streets, elevated highways, corners, tees, crosses, same-axis overlap rejection, exact replay revision stability, and lot-road occupancy rejection.
- Verify directional one-way costs, lower-cost merge behavior, capacity accumulation, no implicit layer connection, explicit transfer edges, load add/subtract, congestion rerouting, and traffic-overlay colors.
- In game, pan away and back after road edits to confirm deferred chunk uploads still catch up when visible.

## Related Guides
- `README.md` indexes the project and controls.
- `docs/design/renderer.md` covers packed road-state texture upload and elevated-road instance consumption.
- `docs/design/simulation-threading.md` covers command application and snapshot publication.
