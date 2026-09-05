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
- `RoadTemplateKind` selects the template family: `Street`, `Road`, `Avenue`, or `Highway`. Compatibility shims still migrate legacy local two-way lane-count-two input to `Avenue`.
- `RoadTemplateDefinition` owns template semantics. It normalizes tool input, builds template elements, and recreates `RoadLaneCell`s from authored placements during dirty resolution.
- `RoadLane` owns lane behavior and emitted lane placements. Each placement carries a template id, road axis, tile-local side span, lane type, flow, and lane-owned divider hints.
- `RoadLaneCell` is the small resolved unit: one required primary car-like lane plus an optional deterministic secondary lane such as a sidewalk or median.
- `RoadGraphic` is lane-owned render intent. It packs primitives into `RoadRenderState`; it does not decide pathing or infer topology.
- `Road` owns template construction and brush expansion. It converts the current tool inputs into clipped per-tile lane placements.
- `TransportTile` owns the authored lanes on one tile/layer and validates local merge/replay rules. Save state stores these authored tile lanes, not stroke objects.
- `RoadRenderState` owns base glyph, arrow glyph, lane graphic mask, and divider packing. `RoadAtlas` generates the finite road-tile atlas used by the renderer.
- `TransportCostMap` owns dense outgoing directional costs, capacities, building access, sparse transfer edges, morning/evening traffic load states, reusable uniform-cost search scratch, and traffic-overlay generation.
- `TransportNetwork` owns layer storage, lot-occupancy rejection, dirty tile neighborhoods, chunk revisions, resolved-cell publication, affected-tile cost-map rebuilding, traffic-overlay publication, and packed ground-road bytes.
- `Data/TransportNetwork/congestion.xml` owns the utilization-to-speed-multiplier table used when old load turns base lane travel time into congested routing cost.
- `Data/TransportNetwork/lane_capacities.xml` owns static lane capacities for slow, medium, fast, and pedestrian lanes.

## Lane Rules
- Templates emit lane cells. The cost map, building access, sidewalk graphics, crosswalk graphics, median graphics, dividers, and packed render state all derive from those lane cells.
- `Street` emits slow car lanes with sidewalks. Two-way street lanes use the suburban concrete-grey slow lane.
- `Road` emits medium car lanes with sidewalks. Medium lanes use the darker asphalt road surface.
- `Avenue` emits four car lanes total. Outer avenue tiles are medium lanes with sidewalks; inner avenue tiles are fast lanes with median secondaries. Medians are non-commuter lanes and add a small land-value/park effect.
- `Highway` emits fast car lanes only unless a future template explicitly adds secondaries.
- The one-way tool uses fast car lanes. It emits a normal sidewalk side when the lane has a one-way neighbor on the left side of travel, otherwise it emits the mirrored sidewalk side.
- Sidewalks are pedestrian secondary lanes. Crosswalks are not authored lanes; they are pedestrian secondary graphics chosen during tile resolution.
- `RoadLaneSurface` is a default graphic surface, not pathing truth. Lane type and flow decide traversal.
- There are no hidden generated pedestrian cap lanes. Pedestrian pathing exists only when a template emits a pedestrian secondary lane.
- Same-axis overlap is lane-span validated. Exact replay is accepted; incompatible shifted road bodies are rejected.
- Perpendicular overlap is allowed as lane coexistence inside the same transport tile. Intersection behavior is resolved afterward from lane adjacency.
- A resolved tile aggregates lane type masks, surface masks, costs, travel, exits, junction glyphs, lane graphics, and dividers for renderer/query consumers.
- A pathing lane contributes only the outgoing directions it actually permits. If multiple lanes contribute to the same tile/layer/mode/direction, the cost map keeps the lower cost and accumulates capacity.
- Transport costs are fixed-point seconds with 1000 cost units per second. Current tile-speed placeholders are slow 9 tiles/second, medium 11 tiles/second, fast 13 tiles/second, highway fast 14 tiles/second, and pedestrian 2 tiles/second. The art/world scale is now 6 metres per tile; these legacy tile-speed values have not been recalibrated to physical travel speeds. See [metric art standard](metric-art-standard.md).
- Current lane capacities are slow 240, medium 560, fast 840, and pedestrian 2400. These numbers are loaded from XML during asset load and applied to `RoadTemplateDefinition`.
- Ground local sidewalks expose adjacent building access for pedestrian and car spawning. Highways, elevated lanes, underground lanes, and through-only lanes do not expose adjacent building access by default.

## Pathfinding And Traffic Loads
- Cost-map nodes use `tile + totalTiles * (mode + modeCount * layer)` so routing can use compact scratch arrays.
- The cost map stores eight outgoing direction slots. Current road lanes populate cardinal directions; diagonal slots are reserved for future connectors.
- Current `TransportCostMap::findPath` is uniform-cost search/Dijkstra, not true A*: heap priority is accumulated cost only, with no geometric heuristic.
- The current search expands movement edges within one layer/mode and sparse transfer edges for mode/layer changes. There is no implicit connection between overlapping layers.
- The current search seeds each candidate start node with a mode start cost before movement: car starts currently add 60 seconds, while pedestrian starts add 0. This represents parking/unparking overhead and makes short walking trips competitive.
- Congestion reads immutable committed load for the requested commute time, converts `oldLoad / capacity` through the XML speed table, and writes reassigned traffic into that commute time's touched new-load edges. Morning and evening loads are parallel states over one stable base cost/capacity graph.
- Route tie-breaking uses tiny deterministic jitter from the route seed so equivalent alternatives can distribute statistically over repeated sampled updates.
- Commute assignment has two flat-graph pathfinding shapes. Source demand fill should be one outward nearest-goal Dijkstra from the selected source access nodes that keeps collecting reached compatible destinations until source demand can be satisfied or no route remains within the maximum commute cost; after that single exploration, assign the accepted destination routes as one batch. It should not re-explore from the source once per accepted destination. Route repair is point-to-point: a known accepted source/destination pair is checked or recalculated directly.
- Commute assignment routes low-wealth residential demand to compatible low-wealth job destinations as a round trip. A destination is valid only when the morning home-to-job path and evening job-to-home path both succeed within the maximum commute window.
- Each route stores morning and evening path results, coalesced segments, and medium-retry flags. Short directions are preserved and clear their retry flag. Medium directions are rerouted only for the offending commute time; invalid, long, or already-retried medium directions force destination reassignment.
- The next flat-routing slice should first make nearest-goal demand fill collect all needed destinations in one Dijkstra pass, then split the API into nearest-goal demand fill and point-to-point route repair, then make point-to-point repair true bidirectional A* with reverse adjacency and an admissible lower-bound heuristic.
- Simulation ticks alternate commute times through `SimulationTime::ticksPerDay() == 2`: morning on the first tick of a logical day and evening on the second. Gameplay durations should be authored in logical days where possible, then converted to runtime ticks at load/setup boundaries.
- Querying a lot can publish coalesced morning commute route segments; rendering turns those tile/layer/mode/direction segments into mode-colored arrows above roads, buildings, and overlays. Querying a road tile summarizes morning and evening commuters that pass through that tile's lanes and draws those selected segments as route arrows.
- Runtime commute assignment preserves valid accepted round-trip routes instead of clearing every lot after ordinary building edits. Removed or invalid source/destination routes are forced back through assignment, and otherwise a deterministic rolling queue rebalances about 1 percent of source lots per tick so all source lots are visited over roughly 100 ticks without random repeats.
- Traffic overlay pixels are congestion summaries, not query-route data: each tile displays the worst utilization across morning/evening, modes, layers, and directions.

## Crosswalk Rule
Pedestrian secondaries first choose their edge from the primary lane's `centerMask()`. A sidewalk edge becomes a crosswalk edge only when the edge is opposite the primary center and the primary lane's combined incoming/outgoing mask says that edge is crossing the primary motion. Edges moving away from the center remain sidewalks. This keeps caps and ordinary corners from converting into crosswalks while allowing valid T and four-way crossings to draw pedestrian crossing graphics from the same lane-cell data that feeds pathing.

## Cleanup Rule
Road edits seed the immediate neighborhood and then expand dirty resolution across the connected road component. This lets validation use information from the full local road graph, including wide roads where the deciding continuation can be several tiles away from the modified slice. L-corner overlaps are cleaned to a valid corner configuration with one horizontal and one vertical junction leg; they must not remain as partial T/cross intersections. Road removal clears the full road cross-section for the clicked slice, so a normal two-tile two-way road removes both paired footprint tiles and wider avenue templates remove the full template width. Dragging bulldoze across every slice of a road should remove every authored lane for that road and leave no stale template/direction data in save state.

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
- Verify lane traversal costs remain fixed-point seconds, lane capacities load from `lane_capacities.xml`, car starts add 60 seconds, pedestrian starts add 0 seconds, and congestion applies over-capacity slowdowns after the base lane travel cost.
- In game, pan away and back after road edits to confirm deferred chunk uploads still catch up when visible.

## Related Guides
- `README.md` indexes the project and controls.
- `docs/design/transport-routing-scalability-plan.md` owns the flat routing roadmap: single-pass nearest-destination Dijkstra for demand fill, point-to-point bidirectional A* for route repair, later flat route-splitting/budgeting, and future hierarchical-routing research.
- `docs/design/renderer.md` covers packed road-state texture upload, generated road atlases, and elevated-road instance consumption.
- `docs/design/simulation-threading.md` covers command application and snapshot publication.
