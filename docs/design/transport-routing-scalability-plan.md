# Transport Routing Scalability Plan

Use this guide when changing commute routing scalability, traffic load storage, route budgeting, or future network-sized topology caches.

## Implemented Checkpoint - May 2026
- Option A is implemented: `SimulationRuntime` keeps persistent `TransportPathScratch` for commute routing, so ordinary route searches no longer allocate map-sized scratch.
- The first large part of Option B is implemented without the later chunk-owned sparse topology cache. `TransportCostMap` still uses dense `(tile, layer, mode)` topology nodes for base cost/capacity/access, but traffic loads now live in separate mutable `Morning` and `Evening` load states keyed by edge identity.
- Traffic load application is sparse for ordinary commute updates. Begin/commit tracks touched movement edges and transfer edges, commits only those loads, reports touched tiles, and updates only affected traffic-overlay pixels/chunks. Full zeroing still exists for whole-network rebuilds and full commute resets.
- Commutes are now round-trip assignments. A destination is valid only when the morning home-to-job path and evening job-to-home path both succeed within the maximum commute window.
- Each simulation tick computes one commute time: even tick offsets are morning, odd tick offsets are evening. `SimulationTime::ticksPerDay()` is currently `2`, so one logical day is two simulation ticks.
- Existing route records store morning/evening path results, morning/evening coalesced segments, and per-direction medium-retry flags. Query route arrows and query text publish morning-only commute segments; the traffic congestion overlay displays the worst utilization across morning/evening, mode, layer, and direction.
- Medium-route repair is direction-local. If only the evening path is medium, only the evening path is rerouted to the same destination and the evening retry flag is set. A route is reassigned after invalid/long paths or after a direction remains medium once its retry flag is already set.
- User-observed performance on the test city improved from about `10` TPS before route/load work, to about `14` TPS after persistent scratch, then to about `200` TPS after sparse parallel morning/evening load states and touched overlay updates. A city with no pathfinding reaches about `2000` TPS, indicating tile-based updates are now the next broad ceiling when pathfinding is absent.

## Current Findings
- Current city simulation maps are `1024x1024`; the `4096x4096` constants are region preview texture dimensions, not the live city runtime map size. Code references: `City Builder/SimulationRuntime.h:381`, `City Builder/SimulationRuntime.h:382`, `City Builder/City.h:162`, `City Builder/City.h:163`.
- The current transport cost map sizes path nodes as `map tiles * transport layers * transport modes`, then allocates dense base topology cells and transfer offsets for that whole space. Traffic loads are no longer stored in the base cells; they are stored in morning/evening `TransportTrafficLoadState` records.
- The current transport modes are car and pedestrian, and the current layers are ground, elevated, and underground. Code references: `City Builder/TransportTypes.h:9`, `City Builder/TransportTypes.h:15`.
- Every path search still resets stamp metadata against `totalNodeCount_`, but commute routing reuses persistent scratch instead of constructing it locally per pass.
- Ordinary load add/subtract/commit and traffic overlay updates are sparse touched-edge/touched-tile operations. Full load clearing remains for full commute resets, imports, and graph rebuilds.
- RCI smart-grid planning uses a two-tile local-road footprint. Residential prefers 4-tile lot depth, industrial prefers 8-tile lot depth, and both prefer 16-tile block width. Code references: `City Builder/RciTool.cpp:26`, `City Builder/RciTool.cpp:471`, `City Builder/RciTool.cpp:472`, `City Builder/RciTool.cpp:474`, `City Builder/RciTool.cpp:475`, `City Builder/Data/RCI/rci_tools.xml:2`, `City Builder/Data/RCI/rci_tools.xml:3`.
- RCI planned roads become one-lane, two-way ground local streets. Ground local streets have pedestrian edge lanes and expose both pedestrian and car building access from sidewalk edges. Code references: `City Builder/AppController.cpp:124`, `City Builder/AppController.cpp:131`, `City Builder/AppController.cpp:134`, `City Builder/AppController.cpp:135`, `City Builder/AppController.cpp:136`, `City Builder/Road.cpp:361`, `City Builder/TransportNetwork.cpp:1123`, `City Builder/TransportNetwork.cpp:1124`.
- The simulation order is commands, construction, lot effects and city parameters, commute assignment, then RCI construction. This is useful because demand is current before constructor work, while new construction can stay inactive until complete.

## Count And Memory Probes
The following counts come from a small non-mutating Python probe that reimplemented the current `PartitionBlocksWithRoads`, `PartitionSegments`, and `SplitBlockIntoTwoDepths` rules from `RciTool.cpp`. Assumptions: the full map is filled with one RCI type in lots+roads mode; every two-tile road tile is pathable by both cars and pedestrians; frontage is unique road/path nodes adjacent to lot perimeter access.

| Ideal grid | Road bands | Road/pathable tiles | Car nodes | Ped nodes | Lots | Lot frontage points |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Residential 1024x1024 | 58 vertical, 103 horizontal | 305,832 | 305,832 | 305,832 | 92,208 | 278,484 |
| Industrial 1024x1024 | 58 vertical, 58 horizontal | 224,112 | 224,112 | 224,112 | 51,528 | 207,024 |
| Residential 4096x4096 stress case | 228 vertical, 410 horizontal | 4,852,576 | 4,852,576 | 4,852,576 | 1,485,488 | 4,464,824 |
| Industrial 4096x4096 stress case | 228 vertical, 229 horizontal | 3,534,896 | 3,534,896 | 3,534,896 | 828,096 | 3,311,492 |

Approximate memory pressure:

| Representation | Current 1024 map | 4096 stress case |
| --- | ---: | ---: |
| Dense nodes today, all layers and modes | 6,291,456 | 100,663,296 |
| Dense `TransportPathScratch`, about 26 bytes per node from `TransportCostMap.h:99` through `TransportCostMap.h:106` | 156 MB per scratch | 2,496 MB per scratch |
| Dense `TransportCostCell`, about 66 bytes per node from `TransportCostMap.h:20` through `TransportCostMap.h:29` | 396 MB | 6,336 MB |
| Sparse residential car+ped nodes, assuming 24 bytes per node | 14 MB | 222 MB |
| Sparse residential directed edges, assuming 16 bytes per edge and about 8 directed mode edges per road tile | 37 MB | 592 MB |

The original cliff to about 10 ticks per second was consistent with dense scratch allocation and dense load/overlay scans becoming the dominant memory-bandwidth work as soon as any pathfinding happened. After persistent scratch and sparse traffic load/overlay commits, the user's pathfinding-heavy test city reaches about 200 TPS. Traffic load changes should continue to be treated separately from topology changes: route recalculation changes edge loads constantly, while road topology changes comparatively rarely.

## Options
### Option A: Persist Dense Scratch
- `TransportPathScratch` has been moved out of `runCommuteAssignment` and is now persistent for commute routing.
- Keep current dense node ids and current `TransportCostMap` storage.
- Benefit: lowest implementation risk and likely removes the immediate per-route allocation cliff.
- Cost: still scales with map size, all layers, and all modes. It also keeps full-map load copy/commit and overlay scans.
- Implemented.

### Option B: Chunk-Owned Routing Topology Cache
- Partially implemented for traffic loads; the chunk-owned topology cache remains future work.
- Keep dense road/render/query data as the authoritative topology.
- Later, build a routing topology cache from active pathable tiles, but do not maintain it as one constantly mutating global sparse vector.
- Store topology by transport chunk. A dirty chunk rebuilds its local node/edge arrays wholesale, then reconnects borders to neighboring chunks.
- Route node handles should include chunk id, local node id, and generation, so old routes that cross changed chunks become invalid without surgical edge patching.
- Traffic load changes must not rebuild this topology cache. Path cost reads `baseCost / congestion(oldLoad, capacity)` from a separate load state.
- Benefit: avoids pathing over empty nodes without making road insert/delete operations patch the middle of old global arrays.
- Cost: more architectural work than the current likely bottleneck requires. Keep this in a later phase until persistent scratch, sparse load deltas, and lazy route budgets are measured.

### Option C: Batch Reverse Search / Destination Fields
- Keep this ready for a future slice.
- Build reverse shortest-path fields from current vacancies, not from all jobs or all workers.
- Use them only as per-tick or short-lived market accelerators when many adjacent sources are trying to reach the same vacancy set.
- Benefit: nearby lots can share exploration work because many sources read from one destination-oriented field.
- Cost: vacancies are volatile, and capacity assignment still needs deterministic acceptance/reduce logic. This should not block the immediate fixes.

## Recommended Design
Option A and the sparse traffic-load portion of Option B are implemented. Continue from the current dense-topology, sparse-load architecture. Defer chunk-owned topology-cache work until profiling proves dense topology traversal is still the bottleneck. Keep Option C as a later batch accelerator.

Immediate data shape:
- Keep the existing dense `TransportCostMap` topology for now.
- Keep route scratch persistent per simulation worker or per routing worker. Scratch arrays keep stamp semantics and should not allocate inside ordinary route searches.
- Keep topology split from mutable load state: edge base cost/capacity/topology is stable during a route batch, while morning/evening traffic loads are stored and committed separately.
- Route results keep morning/evening paths plus morning/evening tile/direction metadata. Query rendering consumes morning segments only.

Sparse load policy:
- Keep ordinary `beginNextLoadFromOldLoad` and `commitNextLoad` sparse by tracking touched edge identities. `beginNextLoadFromZero` may still be full-map on commit because full resets are intentionally rare.
- A route recalculation subtracts the old accepted morning and evening routes and adds the new accepted morning and evening routes into sparse delta storage.
- Deltas are keyed by current dense path edge identity at first, for example `(node id, direction)` or a compact edge id introduced for this purpose.
- Reduce and apply only touched edges. If many routes touch the same arterial edge, aggregate all changes and write that edge once.
- Traffic overlay pixels and overlay chunk revisions update only for tiles touched by load deltas. The overlay pixel is the worst utilization across morning/evening, modes, layers, and directions.
- Full load/overlay rebuild remains available for imports, graph rebuilds, debugging, and validation.

Lazy route policy:
- Route against previous-tick committed loads during a batch, not against every immediately added commuter. This keeps route jobs parallel and avoids turning one long route into thousands of topology writes.
- Search only until enough vacancies are found to satisfy the selected unemployed demand budget, then stop.
- Keep the rolling rebalance queue, but cap route work by a configurable per-tick route budget and by remaining unsatisfied demand.
- Preserve existing valid routes unless the source, destination, or topology became invalid. Load-only changes should not force route invalidation.
- Preserve short routes and clear that direction's medium-retry flag. Reroute only the direction that is medium for the current commute tick; reassign destinations after invalid, long, or already-retried medium directions.

Cache policy:
- Cache each lot's access node list by `(topologyRevision, lotId, lot access revision)`.
- Cache commute source records and vacancy destination records by city-parameter revision plus topology revision.
- Keep accepted route records with enough edge/tile-direction identity to subtract previous load without rebuilding the path.
- When a destination fills, it stops being considered a vacancy for new route searches in that tick.

Threading policy:
- Build route jobs from selected source lots after city parameters are reduced.
- Give each worker persistent scratch sized to the current routing node space. Scratch arrays use stamps, so they are not cleared per route.
- Workers read immutable topology and previous-tick committed loads. They write route candidates and worker-local edge deltas.
- The reduce phase is deterministic and single-owner per market: assign destination capacity, accept/reject candidates, update lot commute state, and merge edge deltas.
- For hundreds of routes per tick, batch by market. Destination fields remain a later optimization, not the first implementation path.

Simulation order:
- Keep commands and construction before lot effects.
- Rebuild dense transport topology after road edits as today. Later topology caches rebuild only dirty transport chunks after topology edits, never after traffic load changes.
- Apply lot effects and rebuild city parameters.
- Build source/destination batches from cached lot data.
- Run routing in parallel against immutable topology and previous loads.
- Reduce accepted routes and sparse load deltas.
- Publish updated commute satisfaction and traffic overlays.
- Run RCI construction after demand has been recalculated, preserving current construction semantics.

Later topology-cache shape:
- Use chunk-owned active node/edge arrays rather than one global vector that needs middle insert/delete maintenance.
- Node handle: `(chunkId, localNodeId, generation)` or a packed equivalent.
- Road edits dirty affected chunks plus border-neighbor chunks. Dirty chunks rebuild local arrays wholesale and reconnect boundaries.
- Existing routes crossing changed chunk generations become invalid and are queued. Routes crossing unchanged chunks remain structurally valid.
- Broad modes such as car and pedestrian may use dense tile-to-local-node lookup inside chunks. Sparse modes such as trains and subways use compact per-mode lookup tables.

## Implementation Phases
0. **Persistent scratch:** implemented for commute routing. Additional timing counters for scratch reset/path search/load begin/load commit/overlay refresh are still useful.
1. **Sparse load pipeline:** implemented for ordinary commute load changes and touched overlay updates while keeping current dense topology.
2. **Lazy route budgeting:** partially implemented through rolling source selection and stopping when selected demand cannot find capacity. A hard per-tick route budget remains future work.
3. **Parallel batches:** move route recalculation into worker batches with worker-local scratch and deltas, then deterministic reduce.
4. **Chunk-owned topology cache:** only if profiling still points at topology traversal, introduce a chunk-owned active-edge cache rebuilt wholesale per dirty chunk after topology edits.
5. **Destination fields:** add reverse vacancy fields for high-volume markets only after route batching and sparse loads are stable.

## Test Plan
- Add a deterministic smart-RCI count test for a full 1024 residential grid: `305,832` car pathable tiles, `305,832` pedestrian pathable tiles, `92,208` lots, and `278,484` frontage points.
- Add the same count test for full 1024 industrial: `224,112` car pathable tiles, `224,112` pedestrian pathable tiles, `51,528` lots, and `207,024` frontage points.
- Keep persistent-scratch tests or instrumentation checks proving ordinary route searches do not allocate map-sized scratch.
- Extend sparse load-delta tests beyond the current unit coverage: removing one route, adding one route, and rerouting many routes should touch only the expected edge ids and overlay chunks.
- Add commute tests for round-trip validity, one-way invalid destinations, per-direction medium retry flags, and reassignment after already-retried medium/long/invalid directions.
- Add lazy-budget tests proving route search stops after enough vacancies are found for the selected unemployed demand budget.
- Add route equivalence tests before any topology cache work, comparing the dense current route results with the chunk-owned cache on straight roads, corners, intersections, disconnected roads, one-way roads, and mixed car/pedestrian access.
- Add sparse mode tests with a tiny rail/subway line to verify node count scales with track/station tiles rather than map size.
- Add a performance regression test that recalculates at least 500 source routes on the 1024 residential ideal grid without allocating map-sized scratch or scanning all map nodes in ordinary load commit.

## Open Questions To Resolve During Implementation
- Whether to keep `(fromNodeId, roadDirection)` as the long-term dense movement edge identity or introduce explicit edge ids before chunk-owned topology work.
- Whether traffic overlay should update when hidden. Default: do not rebuild overlay pixels while hidden; mark touched chunks stale and rebuild lazily when the overlay is requested.
- Whether route jobs should read old load or old load plus same-tick accepted deltas. Default: read previous-tick committed loads for parallelism and deterministic batching.
- Whether the later chunk-owned topology cache should use one shared graph or one graph per mode. Default: one cache with mode in the node key so transfers are natural.
- Whether destination fields should be congestion-aware. Default: use previous-tick congestion in edge costs when building the field, then still run candidate validation in reduce.
