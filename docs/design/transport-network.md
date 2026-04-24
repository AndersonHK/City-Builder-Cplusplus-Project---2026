# Transport Network Design Notes

Use this guide when changing road placement, topology resolution, road render data, or transport layers.

## Intent
- Roads live outside `Tile` so scalar simulation data stays compact.
- Ground and elevated roads share the same resolved road model, even though they render through different upload paths.
- Road rendering should express topology without making transport objects the simulation source of truth.

## Current Shape
- `TransportNetwork` owns authored build cells, resolved road cells, packed ground-road render bytes, and per-layer chunk revisions.
- Ground roads render as tile overlays from packed base/arrow glyph ids.
- Elevated roads render as separate chunked instance buffers.
- Road strokes are two axis-aligned legs that carry a modular road template.
- A road template is an ordered cross-section of lanes. Sidewalks and crosswalks are pedestrian lanes with different `RoadLaneSurface` values, not separate commuter policies. See `RoadLaneTypeId`, `RoadLaneSurface`, and `RoadTemplateElement` in `City Builder/TransportNetwork.h:45`, `City Builder/TransportNetwork.h:53`, and `City Builder/TransportNetwork.h:157`.
- Seams are the only relationship primitive between adjacent template members. `RoadTemplateSeamBetween` (`City Builder/TransportNetwork.cpp:277`) decides whether adjacent lanes get no seam, a same-direction divider, or an opposing-flow divider.
- `ResolvedRoadCell` stores aggregate lane type, surface, travel, exit, divider, junction, and per-lane-type cost data for tile-level pathfinding and rendering (`City Builder/TransportNetwork.h:224`).
- Visual junctions are derived from lane continuity rather than raw same-family adjacency. Parallel lanes beside each other are not intersections.
- Dirty topology resolves only the changed tiles and their neighbors.

## Road Template Pipeline
- Template construction happens in `TransportNetwork::makeRoadTemplate` (`City Builder/TransportNetwork.cpp:727`). Ground local streets currently build pedestrian edge lanes plus car lanes; elevated highways use the same template machinery but only car lanes by default.
- Layout happens in `BuildLayoutWidths` and `BuildCrossSectionTiles` (`City Builder/TransportNetwork.cpp:340` and `City Builder/TransportNetwork.cpp:421`). The pass chooses a whole-tile footprint, flexes lane widths within min/max constraints, assigns lane flow, and aggregates lane type/surface masks into tile slots.
- Stroke expansion happens in `appendLegPlacements` (`City Builder/TransportNetwork.cpp:796`). It lays the cross-section across the chosen footprint and records axis and cross-section-slot metadata so validation can distinguish true replay from shifted body overlap.
- Placement validation happens before mutation in `canMergePlacement` (`City Builder/TransportNetwork.cpp:927`). Empty tiles accept, exact same-template replay accepts, adapter-friendly perpendicular overlaps become intersections, and same-axis incompatible overlaps are rejected by lane slot/type/surface/flow compatibility.
- Resolution happens in `resolveDirtyTile` (`City Builder/TransportNetwork.cpp:963`). It computes lane exits, junction variants, lane type/surface masks, crosswalk surface conversion for eligible pedestrian edge lanes at intersections, divider suppression at intersections, per-type costs, and packed ground render bytes.
- Publication still flows through `SimulationRuntime::refreshPublishedRoadSnapshot` so rendering reads immutable road state only.

## Rules
- Keep lanes generic. Car, pedestrian, bike, bus, and future lane types should use the same template, placement, seam, resolution, and cost-table path.
- Keep surfaces semantic but not path-defining. `Sidewalk` and `Crosswalk` are pedestrian lane surface variants; `Asphalt` is a car-lane surface today.
- Reject ground-road placement on occupied lot tiles.
- Reject same-axis template body overlap unless the incoming placement is an exact replay of the same cross-section slots.
- Preserve packed render-state compatibility with `Basic.shader` and road atlas generation. Ground render channel 2 is now a surface-edge mask: sidewalk edges in the low nibble and crosswalk surface edges in the high nibble.
- Track upload freshness per chunk in the renderer so hidden stale chunks update when visible.

## Checks
- Place local streets and elevated highways, then pan away and back.
- Verify lane order, corners, tees, crosses, dead ends, transition caps, crosswalk surfaces, and arrows resolve correctly.
- Place two same-axis two-way streets within an incompatible offset; the placement should reject rather than merge lanes into bidirectional tiles.
- Query representative tiles and confirm lane type masks, surface masks, exits, junction variant, surface edges, and lane-type costs.
- Confirm ground roads block lots but elevated roads do not use ground occupancy.

## Related Guides
- `README.md` indexes the project and controls.
- `docs/design/renderer.md` covers packed road-state texture upload and elevated-road instance consumption.
- `docs/design/simulation-threading.md` covers command application and snapshot publication.
