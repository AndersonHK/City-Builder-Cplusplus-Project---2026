# Transport Routing Scalability Plan

Use this guide when changing commute routing scalability, traffic load storage, route budgeting, or future network-sized topology caches.

## Implemented Checkpoint - May 2026
- Option A is implemented: `SimulationRuntime` keeps persistent `TransportPathScratch` for commute routing, so ordinary route searches no longer allocate map-sized scratch.
- The first large part of Option B is implemented without the later chunk-owned sparse topology cache. `TransportCostMap` still uses dense `(tile, layer, mode)` topology nodes for base cost/capacity/access, but traffic loads now live in separate mutable `Morning` and `Evening` load states keyed by edge identity.
- Traffic load application is sparse for ordinary commute updates. Begin/commit tracks touched movement edges and transfer edges, commits only those loads, reports touched tiles, and updates only affected traffic-overlay pixels/chunks. Full zeroing still exists for whole-network rebuilds and full commute resets.
- Commutes are now round-trip assignments. A destination is valid only when the morning home-to-job path and evening job-to-home path both succeed within the maximum commute window.
- Each simulation tick computes one commute time: even tick offsets are morning, odd tick offsets are evening. `SimulationTime::ticksPerDay()` is currently `2`, so one logical day is two simulation ticks.
- Existing route records store morning/evening path results, morning/evening coalesced segments, and per-direction medium-retry flags. Lot query route arrows and query text publish morning commute segments; road query route arrows and query text publish both morning and evening commute segments. The traffic congestion overlay displays the worst utilization across morning/evening, mode, layer, and direction.
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

## Scaling Terms
Use these symbols when comparing options:

| Symbol | Meaning |
| --- | --- |
| `A` | map area in tiles, `width * height` |
| `L` | transport layer count, currently `3` |
| `M` | transport mode count, currently `2` |
| `N_dense` | dense cost-map nodes, `A * L * M` |
| `V` | active pathable routing nodes, roughly pathable tiles per mode plus transfer nodes |
| `E` | directed movement plus transfer edges |
| `S` | commute source lots with resident demand |
| `D` | destination lots with job capacity |
| `G` | destination access nodes considered by a morning multi-goal request |
| `R` | accepted route records, not individual commuters |
| `p` | average stored steps per accepted round-trip direction |
| `q` | routine recheck fraction per tick |
| `F` | forced source lots queued by topology/source/destination invalidation |
| `B` | hard forced-source recalculation budget per tick |
| `P` | hard path-search budget per tick |
| `T` | simulation ticks per logical day |

Commuters are aggregated by route demand. A source lot with `80` residents and one accepted destination creates one `CommuteRouteRecord`, not `80` commuter objects. Therefore commuter population mostly changes edge load arithmetic and route demand, while source count, destination count, route count, route length, and failed destination attempts drive CPU cost.

## Current Cost Model
### Dense storage
The cost map's memory floor scales with `N_dense = A * L * M`, even when most layers or modes are empty. With current `1024x1024`, `L = 3`, and `M = 2`, this is `6,291,456` dense nodes. A `4096x4096` city would be `100,663,296` dense nodes. Persistent scratch removed the repeated allocation cliff, but the allocated scratch still scales with this dense count.

Topology memory and scratch therefore scale with map area even if the active road network is sparse:

```text
N_dense = width * height * 3 layers * 2 modes
scratch bytes ~= N_dense * 26
base cost cells ~= N_dense * sizeof(TransportCostCell)
```

The A* loop itself expands from start nodes through outgoing active edges; it does not scan every dense node per path. Dense storage still hurts through memory footprint, cache locality, and the size of per-worker scratch once routing becomes parallel.

### Per-path search
`TransportCostMap::findPath` is Dijkstra-like today: heap priority is accumulated cost only, with no geometric heuristic. Per search:

```text
setup ~= O(start access nodes + goal access nodes)
search ~= O(visited nodes * log(frontier))
reconstruct ~= O(path steps)
```

The maximum commute window is `600` seconds. Current base costs are roughly:

| Mode/lane | Cost per tile | Start cost | Maximum ideal tiles inside 600 s |
| --- | ---: | ---: | ---: |
| Car slow street | `111` | `60,000` | `4,864` |
| Car medium road | `90` | `60,000` | `6,000` |
| Car fast road | `76` | `60,000` | `7,105` |
| Car highway | `71` | `60,000` | `7,605` |
| Pedestrian | `500` | `0` | `1,200` |

That means a `1024x1024` connected car network can be searched nearly end-to-end within the commute cap. A `4096x4096` network is not always fully reachable from a corner, but a fast or highway route can still cover a very large fraction of the active graph. Congestion can reduce the effective radius, but congestion is read from previous committed loads, so it does not bound worst-case work deterministically.

Because car and pedestrian starts can both be seeded, short walking expansions can also compete with car routes before the car start penalty is amortized. That is good behaviorally, but it means a search may visit meaningful parts of both mode graphs when both access modes are available.

### Per-tick commute assignment
`SimulationRuntime::runCommuteAssignment` currently rebuilds source and destination records by scanning all lots every time it runs. For each lot it recollects transport access nodes from current cost-map cells. After that, the selected work is:

```text
selected sources this tick =
    all sources if commutesDirty_
    else forced sources F + ceil(S * q)

current q = 1 percent of source lots per tick
```

For selected sources that keep existing short valid routes, CPU still touches old and new path loads because the source route list is cleared, old loads are subtracted, the maintained route is re-added, and loads are applied again. For selected sources that need new assignment, the current morning request rebuilds a goal-node vector by scanning all open destinations:

```text
new morning request setup ~= O(D + G)
new accepted route ~= 1 morning path + 1 evening path
failed destination ~= another morning path, usually another evening path, then exclusion
source with k accepted destination splits ~= about 2k path searches plus failures
```

So the ordinary selected-source pathfinding trend is approximately:

```text
tick cost ~= O(all lots for source/destination/access refresh)
          + O(selected maintained route steps)
          + O(new morning attempts * (D + G))
          + O(path searches * visited * log(frontier))
          + O(changed route steps for sparse load deltas)
          + O(touched overlay tiles * L * M * directions * commuteTimes)
```

The dangerous term is `selected sources * D` for goal-list rebuilding. If source and destination lots both scale with city area and `q` stays constant, this term trends toward `O(A^2)` per tick even when the actual nearest destination is nearby. The A* work can also trend toward `O(A^2 log A)` per tick in worst cases, because `selected sources` scales with area and a single search can visit most of the connected active graph.

### Topology-change invalidation
Road edits now rebuild cost-map and overlay data only for dirty affected tiles, but invalidation still has broad scans:

```text
road dirty expansion ~= O(A) for temporary bool arrays + O(dirty connected road area)
cost-map rebuild ~= O(dirty tiles * L * M + dirty lane placements)
route invalidation scan ~= O(lots * access footprint checks + R * p * log(dirty tiles))
forced queue dedupe today ~= O(number of queued forced lots) per enqueue
destination back-reference queueing today ~= O(R) per affected destination lot
```

The last two lines matter for large road removals. `queueCommuteRecalculationForLot` uses a vector search for dedupe, and `queueCommuteSourcesForDestination` scans all routes to find sources assigned to one destination. If many lots or routes are touched by a highway deletion, the scheduling work can become expensive before any A* search starts.

Once forced lots are queued, the current selection policy processes all forced sources in the same tick. This is the freeze risk the next design should remove.

## Concrete Trend Examples
### Active topology vs dense topology
Using the current count probes and assuming about `8` directed car+ped movement edges per pathable road tile:

| Case | Dense nodes | Active ground car+ped nodes | Approx directed active edges | Dense scratch |
| --- | ---: | ---: | ---: | ---: |
| Residential 1024 | `6,291,456` | `611,664` | `2,446,656` | `156 MiB` |
| Industrial 1024 | `6,291,456` | `448,224` | `1,792,896` | `156 MiB` |
| Residential 4096 | `100,663,296` | `9,705,152` | `38,820,608` | `2,496 MiB` |
| Industrial 4096 | `100,663,296` | `7,069,792` | `28,279,168` | `2,496 MiB` |

The `4096` stress case is a `16x` area jump. Dense scratch also jumps `16x`. Active routing nodes jump roughly `15.9x` under the smart-grid probe because the road pattern density stays similar.

### Routine recheck rate
Current `q = 1%` per tick and `T = 2` ticks per day means about `2%` of source lots are selected per logical day, so a routine sweep takes roughly `100` ticks or `50` days.

If `T` is raised to `24` and `q` is not changed, the same `1%` per tick becomes `24%` per day, so the routine sweep falls to about `4.2` days and path work per day rises `12x`. To keep the current roughly `50`-day sweep:

```text
q_tick = 1 / (targetSweepDays * T)
q_tick for 50 days at T=24 = 1 / 1200 = 0.0833% per tick
```

| Case | Sources if all lots are sources | Current `1%` per tick | Current sources per day at `T=2` | Same `1%` per tick at `T=24` | `50`-day sweep at `T=24` |
| --- | ---: | ---: | ---: | ---: | ---: |
| Residential 1024 | `92,208` | `923/tick` | `1,846/day` | `22,152/day` | `77/tick`, `1,848/day` |
| Industrial 1024 | `51,528` | `516/tick` | `1,032/day` | `12,384/day` | `43/tick`, `1,032/day` |
| Residential 4096 | `1,485,488` | `14,855/tick` | `29,710/day` | `356,520/day` | `1,238/tick`, `29,712/day` |
| Industrial 4096 | `828,096` | `8,281/tick` | `16,562/day` | `198,744/day` | `691/tick`, `16,584/day` |

This shows why increasing ticks per day is useful only when paired with a lower per-tick routine fraction or a hard route budget. At large city sizes, even the same per-day sweep can still imply too many sources per tick unless there is a hard cap.

### Forced invalidation drain
For road deletion, the key user-facing question is not "can all routes be fixed this tick?" but "how many deterministic ticks should the city spend absorbing this disruption?"

At `T = 24`, a hard forced-source budget drains like this:

| Forced sources `F` | `B=50` | `B=100` | `B=250` | `B=500` | `B=1000` |
| ---: | ---: | ---: | ---: | ---: | ---: |
| `5,000` | `4.2 days` | `2.1 days` | `0.8 days` | `0.4 days` | `0.2 days` |
| `20,000` | `16.7 days` | `8.3 days` | `3.3 days` | `1.7 days` | `0.8 days` |
| `92,208` | `76.9 days` | `38.5 days` | `15.4 days` | `7.7 days` | `3.9 days` |
| `1,485,488` | `1,237.9 days` | `619.0 days` | `247.6 days` | `123.8 days` | `61.9 days` |

This table is intentionally blunt: a source budget that feels smooth for a `1024` city will not keep a fully developed `4096` city responsive after a whole-network invalidation unless the budget rises, the invalidation is more selective, or routing becomes much cheaper per source. It also argues for a visible "commute repair backlog" counter during disruptive edits.

## Alternative Scaling Shapes
| Design | Per-tick shape | What it fixes | What it does not fix |
| --- | --- | --- | --- |
| Lower `q` only | Multiplies routine selected sources by a smaller constant | Routine recheck cost | Mass forced invalidation; destination scan shape; dense memory |
| More ticks per day plus lower `q` | Preserves or lowers daily routine churn while shrinking per-tick slices | Smoother daily work distribution | Needs commute phase fix; does not cap forced queues alone |
| Hard forced/routine source budgets | `O(B * sourceCost)` plus backlog drain `ceil(F / B)` | Highway deletion freezes; deterministic CPU-independent scheduling | Source cost can still spike if one source tests many destinations |
| Hard path-search budget | `O(P * averageSearchCost)` | Bounds A* attempts directly | Requires resumable or conservative source processing |
| Route-to-edge or route-to-chunk invalidation index | `O(dirty edges + affected routes)` | Expensive global route scans after road edits | Recalculation cost for actually affected routes |
| Cached lot access records | `O(changed lots/topology)` instead of recollecting every lot every tick | All-lot access scan overhead | A* cost |
| Destination spatial buckets | `O(nearby/open buckets)` instead of scanning every destination per source | `selected sources * D` goal-list setup | Worst-case far/unreachable searches |
| Reverse destination fields | `O(fields * V log V + selected sources)` | Duplicate search from many origins to same destination market | Capacity allocation, round-trip validation, dynamic congestion |
| Sparse chunk-owned topology | Memory `O(V + E)`; better cache locality | Dense memory and per-worker scratch size | If road density scales with area, worst-case search can still scale with active network size |
| Hierarchical routing | Local search plus chunk/portal graph | Long-route search radius on huge maps | Dynamic congestion accuracy; implementation complexity |
| A* heuristic or bucket queue | Lower constants and fewer visited nodes in favorable cases | Dijkstra heap overhead; some over-expansion | Multi-goal all-destination setup; mass invalidation scheduling |

The best near-term path is not a single heroic pathfinding algorithm. It is a queue and budget layer that makes route repair deterministic and bounded, plus enough indexing/caching to stop scheduling work from becoming its own freeze.

## Recommended Budgeted Route Design
### Separate date ticks from commute repair phase
`SimulationTime::ticksPerDay()` currently also controls active commute time through `simulationTick_ % ticksPerDay() == 0 ? Morning : Evening`. If `ticksPerDay()` becomes `24`, that expression makes only one tick morning and twenty-three ticks evening.

Before increasing `T`, split these concepts:

```text
dateTickInDay = simulationTick % ticksPerDay
routeRepairPhase = simulationTick % 2
```

or define explicit morning/evening phase windows. The route repair phase should stay balanced and deterministic regardless of date granularity.

### Use fixed deterministic budgets
Add commute routing budget knobs with defaults chosen from profiling:

```text
targetRoutineSweepDays = 50
routineFractionPerTick = 1 / (targetRoutineSweepDays * ticksPerDay)
maxForcedSourcesPerTick = B_forced
maxRoutineSourcesPerTick = B_routine
maxPathSearchesPerTick = P
maxDestinationRejectsPerSource = K
```

The selected source count should be:

```text
forcedThisTick = min(forcedQueueCount, B_forced)
routineThisTick = min(ceil(S * routineFractionPerTick), B_routine)
```

All selection must come from stable queues/cursors sorted or inserted deterministically by lot id and reason. Do not use wall-clock time, worker throughput, or "process until frame budget is used"; that would make different CPUs produce different route states on the same tick.

### Replace vector forced-queue dedupe
Current forced lot queueing uses a vector plus linear search. Replace it with one of:

- `queuedCommuteLotEpochByLotIndex`, with a current queue epoch.
- A dense `std::vector<bool>` or byte flag indexed by lot index.
- A sparse `unordered_set<int>` for ids plus a vector for deterministic iteration, sorted before processing if needed.

The goal is:

```text
enqueue forced lot ~= O(1)
drain next forced lot ~= deterministic O(1) or O(log n)
```

### Keep old routes until a source is actually processed
For smooth highway deletion, avoid clearing a source route just because it entered the backlog. A deterministic flow:

1. Road edit commits topology and overlay dirtiness immediately.
2. Invalidation queues affected route sources and marks their routes `staleTopology`.
3. Existing stale routes remain in load state until their source is processed, so traffic shifts gradually.
4. Each tick processes up to the forced budget.
5. When a source is processed, subtract its old route loads, try to produce replacement round-trip routes, then add accepted replacement loads in the same sparse load transaction.
6. If no valid replacement exists, clear that source's stale route and let commute satisfaction fall on that processing tick.

This creates the intended "smooth background repair" behavior. A deleted road tile can still disappear from the visual traffic overlay immediately because overlay relevance is based on current capacity; stale route loads on removed-capacity edges should not make deleted road graphics reappear.

### Bound one source's worst case
A source can currently test destinations until it either satisfies demand or runs out of candidates. Add a per-source reject cap and a path-search budget. Two implementation shapes are viable:

- Conservative source-atomic: only start a source when enough path-search tokens remain for `K` rejects plus one accept. Simple, deterministic, but may underuse the tick budget.
- Resumable source work item: store `sourceLotId`, remaining demand, excluded destination cursor/set, and route seed salt. More work, but lets a large source span ticks without being cleared half-way.

For the first implementation, source-atomic is probably enough if source demand is lot-aggregated and destination capacities are not tiny. Revisit resumable work only if profiling shows individual sources have long rejection chains.

### Make topology invalidation indexed
The scalable invalidation target is:

```text
road edit -> dirty edge ids or dirty chunk generations
dirty ids -> affected route ids/source lot ids
affected source ids -> forced queue
```

Two staged designs:

- Route-edge index: every accepted route stores compact movement edge ids and registers `(edge id -> source lot id or route id)`. A road edit maps dirty cost-map edges to affected routes without scanning all route steps.
- Chunk generation route validity: every route stores the transport chunks it crosses with generation numbers. A road edit bumps dirty chunk generations. Routes crossing changed chunks are stale. This is coarser than edge ids but naturally matches the later chunk-owned topology cache.

Either design should replace `queueCommuteSourcesForDestination`'s repeated full route scans with destination back-references, for example `destinationLotId -> source lot ids/routes currently assigned`.

### Cache access and market records
Cache per-lot access nodes by:

```text
(lot id, lot access revision, transport topology revision)
```

Then build source and destination market records from dirty lot lists plus cached records instead of recollecting every lot's access nodes each tick. The full scan can remain as a validation/rebuild path after imports.

### Reduce all-destination goal setup
The current morning request pushes access nodes for every open destination. Before destination fields, use cheaper deterministic filters:

- Keep open destinations in spatial buckets by chunk or coarse tile cell.
- For a source, gather buckets in expanding Manhattan rings until enough capacity or a maximum candidate count is reached.
- Always use deterministic bucket and lot ordering.
- Fall back to wider rings over later ticks if no route is accepted.

This changes typical setup from `O(D)` per selected source to `O(nearby candidate destinations)`. It also makes "near jobs first" explicit instead of relying on Dijkstra to discover the nearest goal after receiving every goal in the city.

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

### Option D: Deterministic Route Work Queue
- Add a first-class route work queue for forced and routine source lots.
- Process a fixed number of forced sources, routine sources, and/or path searches per tick. Budgets are functions of simulation settings, not CPU time.
- Keep old routes active while they sit in the forced backlog, then atomically subtract old loads and add replacement loads when the source is processed.
- Use queue flags/epochs for O(1) dedupe and destination/edge/chunk back-references for O(affected routes) invalidation.
- Benefit: deletes and other mass topology edits repair smoothly over multiple ticks without freezing the simulation.
- Cost: temporarily stale routes remain until processed. This is a deliberate simulation smoothing tradeoff and should be visible through a pending commute repair counter.

## Recommended Design
Option A and the sparse traffic-load portion of Option B are implemented. Continue from the current dense-topology, sparse-load architecture, but make Option D the next routing scalability step. Defer chunk-owned topology-cache work until profiling proves dense topology memory/locality is still the bottleneck. Keep Option C as a later batch accelerator after deterministic budgets and invalidation indexes exist.

Immediate data shape:
- Keep the existing dense `TransportCostMap` topology for now.
- Keep route scratch persistent per simulation worker or per routing worker. Scratch arrays keep stamp semantics and should not allocate inside ordinary route searches.
- Keep topology split from mutable load state: edge base cost/capacity/topology is stable during a route batch, while morning/evening traffic loads are stored and committed separately.
- Route results keep morning/evening paths plus morning/evening tile/direction metadata. Lot query rendering consumes morning segments; road query rendering consumes both morning and evening segments.

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
- Keep the rolling rebalance queue, but compute its per-tick fraction from a target sweep in logical days: `routineFractionPerTick = 1 / (targetSweepDays * ticksPerDay)`.
- Add hard deterministic budgets for forced sources, routine sources, path searches, and destination rejects per source. These budgets should be checked by tick number and queue state, never by elapsed wall-clock time.
- If `ticksPerDay()` is increased, split date length from commute repair phase so morning/evening repair remains balanced.
- Cap forced invalidated-route recalculation. Large road removals should enqueue affected sources, leave stale routes active until their source is processed, and drain the queue over later ticks.
- Preserve existing valid routes unless the source, destination, or topology became invalid. Load-only changes should not force route invalidation.
- Preserve short routes and clear that direction's medium-retry flag. Reroute only the direction that is medium for the current commute tick; reassign destinations after invalid, long, or already-retried medium directions.

Cache policy:
- Cache each lot's access node list by `(topologyRevision, lotId, lot access revision)`.
- Cache commute source records and vacancy destination records by city-parameter revision plus topology revision.
- Keep accepted route records with enough edge/tile-direction identity to subtract previous load without rebuilding the path.
- When a destination fills, it stops being considered a vacancy for new route searches in that tick.
- Maintain destination back-references from `destinationLotId` to currently assigned source route ids so destination capacity changes do not scan every route.
- Add either route-edge indexes or route chunk-generation stamps before relying on background repair for large topology edits.

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
2. **Deterministic route work queue:** replace vector forced-queue dedupe with queued flags/epochs, add fixed forced/routine/path budgets, and preserve stale routes until a source is processed.
3. **Indexed invalidation:** add destination back-references and either route-edge indexes or route chunk-generation stamps so large road edits queue affected sources without scanning all routes.
4. **Destination candidate filtering:** use deterministic spatial buckets to avoid pushing every destination access node into every morning request.
5. **Parallel batches:** move route recalculation into worker batches with worker-local scratch and deltas, then deterministic reduce.
6. **Chunk-owned topology cache:** only if profiling still points at topology traversal or dense memory, introduce a chunk-owned active-edge cache rebuilt wholesale per dirty chunk after topology edits.
7. **Destination fields:** add reverse vacancy fields for high-volume markets only after route batching, invalidation indexes, and sparse loads are stable.

## Test Plan
- Add a deterministic smart-RCI count test for a full 1024 residential grid: `305,832` car pathable tiles, `305,832` pedestrian pathable tiles, `92,208` lots, and `278,484` frontage points.
- Add the same count test for full 1024 industrial: `224,112` car pathable tiles, `224,112` pedestrian pathable tiles, `51,528` lots, and `207,024` frontage points.
- Keep persistent-scratch tests or instrumentation checks proving ordinary route searches do not allocate map-sized scratch.
- Extend sparse load-delta tests beyond the current unit coverage: removing one route, adding one route, and rerouting many routes should touch only the expected edge ids and overlay chunks.
- Add commute tests for round-trip validity, one-way invalid destinations, per-direction medium retry flags, and reassignment after already-retried medium/long/invalid directions.
- Add lazy-budget tests proving route search stops after enough vacancies are found for the selected unemployed demand budget.
- Add budget tests proving a forced queue of `N` invalidated source lots processes exactly `min(N, budget)` sources on a given tick and leaves the rest queued deterministically.
- Add a `ticksPerDay > 2` test proving date length does not skew morning/evening medium-route repair.
- Add topology-deletion tests proving stale routes remain active until processed, then subtract old load and add replacement load in one sparse transaction.
- Add invalidation-index tests proving a dirty road edge/chunk queues only routes crossing that edge/chunk, plus affected destination back-references, without scanning every route.
- Add route equivalence tests before any topology cache work, comparing the dense current route results with the chunk-owned cache on straight roads, corners, intersections, disconnected roads, one-way roads, and mixed car/pedestrian access.
- Add sparse mode tests with a tiny rail/subway line to verify node count scales with track/station tiles rather than map size.
- Add a performance regression test that recalculates at least 500 source routes on the 1024 residential ideal grid without allocating map-sized scratch or scanning all map nodes in ordinary load commit.

## Open Questions To Resolve During Implementation
- Whether to keep `(fromNodeId, roadDirection)` as the long-term dense movement edge identity or introduce explicit edge ids before chunk-owned topology work.
- Whether traffic overlay should update when hidden. Default: do not rebuild overlay pixels while hidden; mark touched chunks stale and rebuild lazily when the overlay is requested.
- Whether route jobs should read old load or old load plus same-tick accepted deltas. Default: read previous-tick committed loads for parallelism and deterministic batching.
- Whether the later chunk-owned topology cache should use one shared graph or one graph per mode. Default: one cache with mode in the node key so transfers are natural.
- Whether destination fields should be congestion-aware. Default: use previous-tick congestion in edge costs when building the field, then still run candidate validation in reduce.
- What forced-source budget feels right at `T = 24`. The table above suggests `B = 250` drains `20,000` invalidated sources in about `3.3` logical days, while `B = 100` takes about `8.3` days.
- Whether stale invalid routes should continue contributing commute satisfaction until processed. Default: yes for smooth background repair, with a visible pending repair count; clear satisfaction only when that source is processed and no replacement route is found.
- Whether route budgeting should be source-atomic or resumable mid-source. Default: source-atomic with a conservative destination-reject cap until profiling shows individual sources dominate.
- How destination candidates should be bucketed. Default: coarse transport chunks or a fixed grid, searched in deterministic expanding Manhattan rings.
