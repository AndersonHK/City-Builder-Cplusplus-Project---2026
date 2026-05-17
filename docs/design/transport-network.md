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
- `TransportCostMap` owns dense outgoing directional costs, capacities, building access, sparse transfer edges, morning/evening traffic load states, A* scratch reuse, and traffic-overlay generation.
- `TransportNetwork` owns layer storage, lot-occupancy rejection, dirty tile neighborhoods, chunk revisions, resolved-cell publication, affected-tile cost-map rebuilding, traffic-overlay publication, and packed ground-road bytes.
- `Data/TransportNetwork/congestion.xml` owns the utilization-to-speed-multiplier table used when old load turns base lane travel time into congested A* cost.

## Lane Rules
- Sidewalks are pedestrian lanes. Crosswalks are not authored lanes; they are pedestrian lane graphics chosen during tile resolution.
- `RoadLaneSurface` is a default graphic surface, not pathing truth. Lane type and flow decide traversal.
- Pedestrian lanes are candidate-authored and active only when they either border empty terrain or continue as the same side/span on both ends of their axis. Inactive candidates stay stored so later road strokes can make first-built sidewalks valid without order dependence.
- Lane connections across an occupied perpendicular road body require matching stroke identity; two opposite stubs may touch opposite halves of the road body without becoming one through lane. When a later stroke both adds new lanes and exactly replays old lanes, the contiguous lanes from that replayed old stroke adopt the later stroke identity so corner-to-four-way upgrades resolve the same as directly built four-way crossings, even when the player draws only the missing arms.
- Same-axis overlap is lane-span validated. Exact replay is accepted; incompatible shifted road bodies are rejected.
- Perpendicular overlap is allowed as lane coexistence inside the same transport tile. Intersection behavior is resolved afterward from lane adjacency.
- A resolved tile aggregates lane type masks, surface masks, costs, travel, exits, junction glyphs, lane graphics, and dividers for renderer/query consumers.
- A pathing lane contributes only the outgoing directions it actually permits. If multiple lanes contribute to the same tile/layer/mode/direction, the cost map keeps the lower cost and accumulates capacity.
- Local car lane base cost is calibrated as 10 tiles/time at 100 percent capacity. Pedestrian paths currently move 2 tiles/time. Current alpha car-lane capacity is 200 for all car lanes.
- Ground local sidewalks expose adjacent building access for pedestrian and car spawning. Highways, elevated lanes, underground lanes, and through-only lanes do not expose adjacent building access by default.

## Pathfinding And Traffic Loads
- Cost-map nodes use `tile + totalTiles * (mode + modeCount * layer)` so A* can use compact scratch arrays.
- The cost map stores eight outgoing direction slots. Current road lanes populate cardinal directions; diagonal slots are reserved for future connectors.
- A* expands movement edges within one layer/mode and sparse transfer edges for mode/layer changes. There is no implicit connection between overlapping layers.
- A* seeds each candidate start node with a mode start cost before movement: car starts currently add 2 commute-time units, while pedestrian starts add 0. This lets multi-mode access compare the up-front cost of choosing a car against walking.
- Congestion reads immutable committed load for the requested commute time, converts `oldLoad / capacity` through the XML speed table, and writes reassigned traffic into that commute time's touched new-load edges. Morning and evening loads are parallel states over one stable base cost/capacity graph.
- Route tie-breaking uses tiny deterministic jitter from the route seed so equivalent alternatives can distribute statistically over repeated sampled updates.
- Commute assignment routes low-wealth residential demand to compatible low-wealth job destinations as a round trip. A destination is valid only when the morning home-to-job path and evening job-to-home path both succeed within the maximum commute cost.
- Each route stores morning and evening path results, coalesced segments, and medium-retry flags. Short directions are preserved and clear their retry flag. Medium directions are rerouted only for the offending commute time; invalid, long, or already-retried medium directions force destination reassignment.
- Simulation ticks alternate commute times through `SimulationTime::ticksPerDay() == 2`: morning on the first tick of a logical day and evening on the second. Gameplay durations should be authored in logical days where possible, then converted to runtime ticks at load/setup boundaries.
- Querying a lot can publish coalesced morning commute route segments; rendering turns those tile/layer/mode/direction segments into mode-colored arrows above roads, buildings, and overlays. Querying a road tile summarizes morning commuters that pass through that tile's lanes and draws those selected morning segments as route arrows.
- Runtime commute assignment preserves valid accepted round-trip routes instead of clearing every lot after ordinary building edits. Removed or invalid source/destination routes are forced back through assignment, and otherwise a deterministic rolling queue rebalances about 1 percent of source lots per tick so all source lots are visited over roughly 100 ticks without random repeats.
- Traffic overlay pixels are congestion summaries, not query-route data: each tile displays the worst utilization across morning/evening, modes, layers, and directions.

## Crosswalk Rule
A pedestrian lane renders as a crosswalk only when all of these are true:

- The pedestrian lane has a sidewalk graphic edge on this tile.
- The pedestrian lane overlaps a perpendicular car lane in the same tile.
- The perpendicular car lane has car-road continuation on both cardinal ends of its axis.
- The pedestrian lane has pedestrian continuation on both cardinal ends of its own axis.

Otherwise the same pedestrian lane remains a sidewalk. This keeps T-section endpoints from painting half-crosswalks and prevents isolated or partially cleaned intersection tiles from inventing crosswalks where the car road does not continue through the crossing.

## Cleanup Rule
Road edits seed the immediate neighborhood and then expand dirty resolution across the connected road component. This lets validation use information from the full local road graph, including wide roads where the deciding continuation can be several tiles away from the modified slice. Same-stroke L-corner overlaps are cleaned to a valid corner configuration with one horizontal and one vertical junction leg; they must not remain as partial T/cross intersections. Road removal clears the full road cross-section for the clicked slice, so a normal two-tile two-way road removes both paired footprint tiles and wider avenue templates remove the full template width.

After removal, any remaining contiguous authored road run whose longitudinal length is shorter than that road template's footprint is invalid and should be erased too. This prevents remnants such as a two-wide road left one tile long, or a four-wide road left only three tiles long.

Committed road edits rebuild pathing costs and traffic-overlay pixels only for the dirty affected tiles and bump only the touched overlay chunks. Full cost-map and traffic-overlay rebuilds are reserved for full save import or other whole-network resets.

## Test Doctrine
Prefer scenario-style sandbox and micro-simulation coverage over narrow helper-only unit tests. Small function assertions are useful for parsing and bit packing, but road, pathfinding, and commute behavior should be tested through authored configurations that look like player actions and inspect the resulting network state.

Good examples:

- a known-good road layout where one building can path to another building
- many houses on one end of a multi-lane road and jobs on the other, simulated long enough for the rolling route queue to split traffic load across lanes
- a long four-lane road intersected at both ends by two-lane roads, with source lots on one side and destination lots on the other

The current road-tool fixture sandbox should eventually be split out of `TransportNetworkTests.cpp` into a reusable integration-test harness. That harness should accept map width/height, tick count, and scheduled actions by tick. Actions should be able to use tool-level operations such as road strokes, lot placement at exact coordinates, area bulldoze, query, and other future tools. The goal is for transport, runtime, and renderer-adjacent tests to share the same player-action language rather than each test target inventing its own mini-sandbox.

## Road Tool Semantic Cases
Road-tool tests should model small player-action sandboxes, not only individual helper return values. The fixture files under `City Builder/Data/TransportNetwork/SandboxCases/` define action sequences and expected final ASCII grids for materials, active car axes, resolved road variants, crosswalks, sidewalk masks, and junction masks. Material grids use `.` for empty terrain, `R` for road body without visible pedestrian edge, `S` for visible pedestrian edge without road body, and `B` for road body plus visible pedestrian edge. Sidewalk and junction mask grids use the same low-nibble cardinal bit table:

| Hex | Cardinal bits |
| --- | --- |
| `0` | none |
| `1` | north |
| `2` | east |
| `3` | north + east |
| `4` | south |
| `5` | north + south |
| `6` | east + south |
| `7` | north + east + south |
| `8` | west |
| `9` | north + west |
| `A` | east + west |
| `B` | north + east + west |
| `C` | south + west |
| `D` | north + south + west |
| `E` | east + south + west |
| `F` | north + east + south + west |

The core configurations are:

- Dead end: a single stroke has exactly one connected junction leg at each cap. It is not an intersection, has no turn arrows, and does not paint crosswalks or lane markings that imply a road beyond the cap.
- Straight road: the body continues on exactly two opposite legs. Sidewalks remain sidewalks, lane dividers follow the road body, and no crosswalk is inferred.
- L-corner: a single drag with a corner is one road bending through the overlap. Both car lanes flow in parallel around the bend. The overlap resolves as a corner, never as a T or cross, and has no crosswalk or missing-arm junction leg.
- T-section: the main road continues through the mouth, while the side road terminates at the main road. The side-road mouth may connect to the node, but it must not be corrected into a four-way crossing, must not expose the missing opposite side as a through leg, and must not paint half-crosswalks.
- Four-way: a crossing is valid only when both axes continue beyond the whole intersection body. Crosswalks and turn arrows are allowed only in this valid-through case, with turn arrows on approach tiles rather than inside the intersection body.
- Deleted approach: removing one approach from a previously valid crossing must re-resolve the remaining connected road component as a partial/T-style configuration. The center must not keep crosswalks or corrected-through junction legs that point toward the removed approach.

## Turn Arrow Rule
A car intersection node is a resolved car junction with at least three cardinal junction connections. Turn-arrow glyphs are assigned only to non-intersection car tiles that can enter the local collection of tiles around one of those nodes, using the node's available outbound exits minus the U-turn back toward the approach tile. Simple same-stroke L-corners therefore do not count as turn-arrow intersections, even when the road footprint overlaps itself at the corner.

## Render Contract
- Ground road channel 0 is the base glyph.
- Ground road channel 1 is the arrow glyph. Intersection turn arrows override ordinary lane-direction arrows on approach tiles; ordinary arrows are tagged with `kRoadArrowDebugFlag` so `F11` can hide them without hiding turn-lane arrows.
- Ground road channel 2 is the lane graphic mask: sidewalk edges in the low nibble, crosswalk edges in the high nibble.
- Ground road channel 3 is the divider mask: same-direction dividers in the low nibble, opposing-flow dividers in the high nibble.
- Elevated roads consume the same resolved glyph and mask fields through chunked instances.
- Renderer code must consume these masks as presentation data. Lane/crosswalk policy belongs in transport resolution, not shaders.

## Checks
- Build `x64 Release`.
- Build and run `TransportNetworkTests.vcxproj`.
- Verify straight one-way and two-way local streets, elevated highways, corners, tees, crosses, same-axis overlap rejection, exact replay revision stability, and lot-road occupancy rejection.
- Verify directional one-way costs, lower-cost merge behavior, mode start costs, capacity accumulation, no implicit layer connection, explicit transfer edges, independent morning/evening load add/subtract, congestion rerouting, worst-case traffic-overlay colors, and affected-only overlay chunk updates after road removal.
- Verify congestion XML keeps car cost at 10 tiles/time and pedestrian cost at 2 tiles/time at 100 percent capacity before applying over-capacity slowdowns.
- In game, pan away and back after road edits to confirm deferred chunk uploads still catch up when visible.

## Related Guides
- `README.md` indexes the project and controls.
- `docs/design/transport-routing-scalability-plan.md` owns the persistent route scratch, sparse load delta, lazy route budgeting, later chunk-owned topology cache, and destination-field plan for making commute work scale with active network size.
- `docs/design/renderer.md` covers packed road-state texture upload and elevated-road instance consumption.
- `docs/design/simulation-threading.md` covers command application and snapshot publication.
