# Transport Routing Scalability Plan

Use this guide when changing commute routing scalability, traffic load storage, flat route-search behavior, route budgeting, or future hierarchical routing/topology caches.

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

The current uniform-cost loop expands from start nodes through outgoing active edges; it does not scan every dense node per path. Dense storage still hurts through memory footprint, cache locality, and the size of per-worker scratch once routing becomes parallel.

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

For selected sources that keep existing short valid routes, CPU still touches old and new path loads because the source route list is cleared, old loads are subtracted, the maintained route is re-added, and loads are applied again. For selected sources that need new assignment, the current morning loop rebuilds a goal-node vector by scanning all open destinations once per accepted route attempt. Slice 1 should replace that repeated root exploration with one source-root nearest-destination Dijkstra that collects enough reachable candidates, then batch-assigns route records:

```text
current repeated morning request setup ~= O(accepted destinations * (D + G))
slice-1 morning setup ~= O(D + G) once per selected source
new accepted route ~= stored morning path + 1 evening validation/repair path
failed destination ~= another morning path, usually another evening path, then exclusion
current source with k accepted destination splits ~= about 2k path searches plus failures
slice-1 source with k accepted destination splits ~= 1 morning Dijkstra plus evening point-to-point validations
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

The dangerous term is `selected sources * accepted destination count * D` for repeated goal-list rebuilding and repeated root exploration. If source and destination lots both scale with city area and `q` stays constant, this term trends toward `O(A^2)` per tick even when the actual nearest destinations are found early. The flat search work can also trend toward `O(A^2 log A)` per tick in worst cases, because `selected sources` scales with area and a single source-root search can visit most of the connected active graph.

### Topology-change invalidation
Road edits now rebuild cost-map and overlay data only for dirty affected tiles, but invalidation still has broad scans:

```text
road dirty expansion ~= O(A) for temporary bool arrays + O(dirty connected road area)
cost-map rebuild ~= O(dirty tiles * L * M + dirty lane placements)
route invalidation scan ~= O(lots * access footprint checks + R * p * log(dirty tiles))
forced queue dedupe today ~= O(number of queued forced lots) per enqueue
destination back-reference queueing today ~= O(R) per affected destination lot
```

The last two lines matter for large road removals. `queueCommuteRecalculationForLot` uses a vector search for dedupe, and `queueCommuteSourcesForDestination` scans all routes to find sources assigned to one destination. If many lots or routes are touched by a highway deletion, the scheduling work can become expensive before any path search starts.

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

## Roadmap Split
The next routing work should be divided into two parts. Part 1 stays flat and non-hierarchical. Part 2 is a separate hierarchical-routing research and architecture slice. Do not mix destination buckets, chunk portals, station fields, or sparse topology caches into the first slice unless profiling after Part 1 proves the flat graph is still the limiting shape.

### Part 1: Flat Routing Slice
Keep the current dense `(tile, layer, mode)` topology and sparse morning/evening load states. This slice is complete when these three changes are done:

1. **Single-pass nearest-destination Dijkstra.** For one selected source lot, run one outward uniform-cost/Dijkstra search from the source access nodes. As compatible destination access nodes are popped in increasing cost order, reconstruct/store those candidate paths and accumulate reachable destination capacity. Stop when enough candidate capacity can plausibly satisfy the source demand, or when the frontier is exhausted or exceeds the maximum commute cost. Then validate/assign the accepted round-trip destination routes as one deterministic batch. Do not restart from the source once per accepted destination.
2. **Split route APIs by intent.** The flat graph has two different jobs and should expose them separately: nearest-destination demand fill and point-to-point route repair.
3. **Bidirectional A* for point-to-point repair.** Implement the point-to-point API with bidirectional A* over the same directed costs, congestion reads, transfer edges, and mode start rules. It needs reverse movement and transfer adjacency, an admissible lower-bound heuristic based on the fastest uncongested movement, and a correct stop condition for directed graphs. The nearest-destination demand-fill search can remain unidirectional Dijkstra because it is intentionally exploring outward to discover the closest usable destinations.

If a building has `300` unemployed workers, the nearest-destination search should discover enough reachable destinations for those workers in one outward pass where possible, then create route records after the candidate set is known. If reachable capacity before the max-cost cutoff is only `180`, create routes for that `180` and leave the remainder unsatisfied.

Round-trip validation still matters. A practical first implementation can collect morning candidates in one outward pass, then validate evening paths for candidates in morning-cost order using the point-to-point repair API. If evening validation rejects too many candidates, the nearest-destination search should be resumable or should collect enough extra candidate capacity during the first pass so it still avoids repeatedly re-exploring from the source.

### Later Flat-Graph Optimizations
After the three-item slice above, keep flat-graph improvements separate from hierarchy:

- **Route splitting with virtual local load.** For large source lots, split demand into a small deterministic number of route batches. During one source assignment, layer a temporary local load delta over previous committed load so early batches can make a fast avenue more expensive and nearby parallel roads can become competitive. Commit accepted batches together through the existing sparse load pipeline.
- **Deterministic route budgets.** Forced and routine route repair should eventually use fixed source/path budgets derived from simulation settings, not wall-clock time.
- **Queue dedupe and invalidation indexes.** Replace vector forced-queue dedupe with queued flags/epochs, and eventually maintain destination back-references plus route-edge or route-region indexes so topology edits queue affected routes without scanning every route.
- **Access and market caches.** Cache per-lot access nodes and source/destination market records by topology and lot revisions so every tick does not need to rebuild all access data from scratch.

These are still non-hierarchical. They should not introduce destination buckets or a portal graph.

### Part 2: Hierarchical Routing Research
Hierarchical pathfinding is a future design slice. It should be treated as a first-class research topic because the right hierarchy may come from several different structures:

- **Chunk portal graph.** Local routes enter/exit chunks through portals; long routes run through a coarser portal graph, then refine locally.
- **Transport-tier graph.** Local streets feed roads, avenues, highways, ramps, train stations, or subway stops. Long routes can prefer faster/higher-throughput tiers when they are actually useful.
- **Composed mini-routes.** Cache reusable fragments such as house block to arterial, arterial to ramp, station to station, ramp to industrial access road, then compose longer trips from these fragments and validate/refine with flat search.
- **Chokepoint or cut-set caching.** If a neighborhood has one access point, every commuter must pass through that point. Cache local costs from origins to that chokepoint and reuse the downstream path work. Dense grids often do not have such a cut, so this must be opportunistic rather than forced.
- **Sparse or chunk-owned topology.** A sparse active-node graph may support hierarchy, reverse adjacency, and memory locality, but it should not be assumed to be the final hierarchy by itself. Traffic load changes should remain separate from topology rebuilds.

The hierarchy must remain congestion-aware enough to avoid hard-preferring high-tier routes. In a well ordered city, long trips should often use highways or trains, but a closer local road should still receive traffic when congestion or geometry makes it competitive. The flat graph remains the correctness fallback for dense grids, one-way details, ramps, transfers, and cases where the hierarchy has no useful compression.

## Alternative Scaling Shapes
| Design | Per-tick shape | What it fixes | What it does not fix |
| --- | --- | --- | --- |
| Lower `q` only | Multiplies routine selected sources by a smaller constant | Routine recheck cost | Mass forced invalidation; destination scan shape; dense memory |
| More ticks per day plus lower `q` | Preserves or lowers daily routine churn while shrinking per-tick slices | Smoother daily work distribution | Needs commute phase fix; does not cap forced queues alone |
| Hard forced/routine source budgets | `O(B * sourceCost)` plus backlog drain `ceil(F / B)` | Highway deletion freezes; deterministic CPU-independent scheduling | Source cost can still spike if one source tests many destinations |
| Single-pass nearest-destination Dijkstra | One source-root search can produce several destination routes | Repeated root re-exploration during source demand fill | Point-to-point repair search radius; all-lot access rebuilds |
| Bidirectional A* for known destinations | Two directed frontiers guided by an admissible heuristic | Same-destination repair and medium-route retry | Nearest-goal discovery; multi-destination capacity assignment |
| Hard path-search budget | `O(P * averageSearchCost)` | Bounds path attempts directly | Requires resumable or conservative source processing |
| Route-to-edge or route-to-chunk invalidation index | `O(dirty edges + affected routes)` | Expensive global route scans after road edits | Recalculation cost for actually affected routes |
| Cached lot access records | `O(changed lots/topology)` instead of recollecting every lot every tick | All-lot access scan overhead | Path search cost |
| Destination spatial buckets | `O(nearby/open buckets)` instead of scanning every destination per source | Goal-list setup and early candidate filtering | Worst-case far/unreachable searches; non-trivial deterministic widening |
| Reverse destination fields | `O(fields * V log V + selected sources)` | Shared exploration when many origins target the same market | Capacity allocation, round-trip validation, dynamic congestion |
| Route splitting with virtual local load | Small bounded number of deterministic route alternatives per source | Avenue/parallel-road load distribution | Structural bottlenecks; hierarchy-level reuse |
| Sparse chunk-owned topology | Memory `O(V + E)`; better cache locality | Dense memory and per-worker scratch size | If road density scales with area, worst-case search can still scale with active network size |
| Hierarchical routing | Local search plus portal/tier/chokepoint graph | Long-route search radius and repeated shared subpaths | Dynamic congestion accuracy; implementation complexity |
| A* heuristic, landmarks, or bucket queue | Lower constants and fewer visited nodes in favorable cases | Heap overhead and over-expansion | Multi-goal demand fill; mass invalidation scheduling |

The best near-term path is the flat slice above: one nearest-destination Dijkstra per selected source, an explicit split between nearest-destination and point-to-point routing, and bidirectional A* only for known-destination repair. Queue budgeting, route splitting, invalidation indexes, and hierarchy remain later work after that slice is measured.

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

### Avoid repeated source-root exploration
The current morning assignment pushes access nodes for every open destination, runs a nearest-goal search, accepts one destination, then rebuilds the request and repeats from the same source root while demand remains. Slice 1 should keep the all-destination semantics but change the search shape:

- Build the open destination goal set once for the selected source.
- Run one outward Dijkstra/uniform-cost search from the source access nodes.
- When compatible destination nodes are reached, store their path results in increasing cost order.
- Continue until enough reachable candidate capacity exists to satisfy demand, the frontier is exhausted, or the max commute cost is exceeded.
- Batch-assign the accepted routes after the candidate set is known.

Do not use spatial destination buckets in Slice 1. Bucket, portal, station, or tier-based filtering belongs in deferred alternatives and the later hierarchical-routing research slice.

## Deferred Alternative Plans
This section keeps the ideas that are not part of Slice 1. They are not rejected; they are parked so the first implementation stays small enough to finish and measure.

### Destination candidate buckets
A non-hierarchical candidate-filtering alternative would keep open destinations in deterministic spatial buckets, either by transport chunk or by a fixed coarse tile grid. For a source, it would gather buckets in expanding Manhattan rings until it has enough open capacity, enough candidate lots, or reaches a configured search radius. Candidate lots and access nodes must be ordered deterministically by bucket, lot id, and access node id.

This could reduce goal setup from "all open destinations in the city" to "nearby open destinations first". It is not in Slice 1 because it is easy to make subtly wrong: a nearby bucket can be disconnected, a farther bucket can contain the first valid route, and a too-small candidate cap can change assignment outcomes. If it returns later, it should be framed as a candidate provider for the nearest-destination API, with a widening fallback that eventually becomes equivalent to all destinations.

### Reverse destination fields
Reverse fields are a shared-work alternative for moments when many nearby sources are trying to reach the same broad vacancy market. Build one reverse shortest-path field from destination access nodes, then let many sources read distances from that field instead of each source exploring independently.

The field should be short-lived and market-specific, not a global "jobs field". It should be built from current vacancies or from a high-volume market group, using previous committed congestion if congestion is included. It still cannot assign capacity by itself: the reduce phase must deterministically accept candidates, validate round trips, and handle destinations filling during the same tick. This makes reverse fields promising as a later batch accelerator, but not a substitute for the Slice 1 nearest-destination semantics.

### Sparse or chunk-owned topology cache
A sparse topology cache remains useful for memory locality, reverse adjacency, and future hierarchy, but it should not be confused with traffic load storage. Dense road/render/query data can remain authoritative while routing topology is rebuilt from active pathable tiles after topology edits:

- Store active node and edge arrays by transport chunk.
- Rebuild dirty chunks wholesale, then reconnect borders to neighbors.
- Use route node handles such as `(chunkId, localNodeId, generation)` so routes crossing changed chunks become stale without surgical edge patching.
- Keep traffic loads in separate mutable morning/evening states keyed by stable movement edge identity; load changes must not rebuild topology.
- Let broad modes such as car and pedestrian use dense tile-to-local-node lookup inside chunks, while sparse modes such as trains and subways use compact per-mode lookup tables.

This is a good support layer for hierarchy and bidirectional search, especially for reverse adjacency. It is still future work because the current dense topology is acceptable for Slice 1, and because the harder question is semantic: how to exploit highways, ramps, stations, and chokepoints without breaking dense-grid correctness.

### Heuristic and queue variants
Plain bidirectional A* belongs only to point-to-point repair in Slice 1. Additional variants are worth preserving as alternatives:

- A standard A* heuristic can guide one known destination, but it is weak for nearest-destination demand fill because there are many possible goals and the desired behavior is outward discovery.
- Landmark or ALT-style lower bounds could improve point-to-point repair on large flat graphs, but landmarks must be topology-aware and remain admissible under congestion by using lower-bound, uncongested costs.
- Dial, radix, or bucket queues could reduce Dijkstra overhead if costs can be represented as bounded integers. Current costs include congestion multipliers and deterministic jitter, so this needs measurement before replacing the binary heap.

These are constant-factor or point-to-point accelerators. They do not remove the need for the route-intent split, candidate batching, invalidation indexes, or hierarchy research.

### Precomputed hierarchy and chokepoints
The future hierarchy should precompute stable lower-bound structure, not final traffic-adjusted routes. Traffic changes constantly; topology and access structure change much less often. Good candidates for precomputation are:

- local costs from lots to arterial, ramp, station, or chunk-portal nodes;
- portal-to-portal or station-to-station lower-bound distances;
- reusable mini-routes that can be composed and then refined against current traffic;
- opportunistic cut sets, such as a neighborhood with one access road where every commuter must pass through the same chokepoint.

The city can be a dense grid or a set of isolated neighborhoods, and the same algorithm must behave well in both. A chokepoint cache should activate when the graph actually has a small separator; a dense grid should fall back to flat or portal search without pretending that one access point exists. High-throughput routes such as highways and trains should bias long trips because of lower cost and capacity, but local roads must remain viable when they are closer or when the high-tier route is congested.

## Older Options And Where They Land Now
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
Option A and the sparse traffic-load portion of Option B are implemented. Continue from the current dense-topology, sparse-load architecture for Slice 1. The next concrete work is not destination bucketing or a chunk-owned topology cache; it is the flat routing split: one nearest-destination Dijkstra per selected source, separate nearest-destination and point-to-point APIs, and bidirectional A* for point-to-point route repair. Defer deterministic route queues, route splitting, invalidation indexes, destination fields, chunk portals, and chunk-owned sparse topology until after this slice is measured.

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

Flat nearest-destination policy:
- Source demand fill should run one outward Dijkstra/uniform-cost search from source access nodes and collect enough compatible destination candidates before assigning route records.
- Do not re-run the source-root search once per accepted destination.
- Batch route creation after candidate collection so one source's accepted routes can be reduced/applied together through the sparse load pipeline.
- If morning candidate collection succeeds but evening validation rejects too much capacity, continue/resume candidate collection if possible rather than restarting from the source root.

Point-to-point repair policy:
- Existing-route repair, same-destination medium retry, and destination-specific replacement should use the point-to-point routing API.
- The point-to-point API should become bidirectional A* with explicit reverse adjacency for movement and transfer edges.
- The heuristic must be admissible under congestion by using fastest uncongested lower-bound movement, not current congested cost.

Later lazy route policy:
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
2. **Flat slice 1A, single-pass nearest-destination search:** change demand-fill assignment so one source-root Dijkstra gathers enough reachable destination candidates, then batch-assigns route records without re-running from the source for each accepted destination.
3. **Flat slice 1B, route API split:** separate nearest-destination demand fill from point-to-point route repair in the public/internal routing surface.
4. **Flat slice 1C, bidirectional A* route repair:** implement point-to-point repair with bidirectional A*, reverse adjacency, admissible heuristic, and equivalence tests against the current Dijkstra behavior.
5. **Later flat optimization, route splitting:** add deterministic source-demand splitting with virtual local load so parallel routes can share demand under congestion before committing sparse load deltas.
6. **Later flat optimization, route work budgeting and invalidation indexes:** add deterministic forced/routine budgets, O(1) forced-queue dedupe, destination back-references, and route-edge or route-region invalidation.
7. **Hierarchical research slice:** design and prototype chunk portals, transport-tier nodes, composed mini-routes, chokepoint caching, and possible sparse/chunk-owned topology after the flat routing slice is measured.

## Test Plan
- Add tests proving a source with demand larger than one destination capacity can satisfy multiple destinations from one nearest-destination search and produces the same accepted route order as repeated Dijkstra on simple networks.
- Add a test where reachable destination capacity before max cost is insufficient; the source should create routes only for reachable capacity and leave remaining demand unsatisfied.
- Add round-trip tests where the morning nearest destination fails evening validation, proving batch assignment selects later valid candidates without restarting from the source per candidate.
- Add API tests covering the distinction between nearest-destination demand fill and point-to-point repair.
- Add point-to-point bidirectional A* equivalence tests against the current Dijkstra search on straight roads, corners, intersections, disconnected roads, one-way roads, mixed car/pedestrian access, and transfer edges.
- Add congestion tests proving bidirectional A* uses previous committed load and keeps the heuristic optimistic rather than congestion-overestimating.
- Extend sparse load-delta tests beyond the current unit coverage: removing one route, adding one route, and rerouting many routes should touch only the expected edge ids and overlay chunks.
- Add budget tests proving a forced queue of `N` invalidated source lots processes exactly `min(N, budget)` sources on a given tick and leaves the rest queued deterministically.
- Add a `ticksPerDay > 2` test proving date length does not skew morning/evening medium-route repair.
- Add topology-deletion tests proving stale routes remain active until processed, then subtract old load and add replacement load in one sparse transaction.
- Add invalidation-index tests proving a dirty road edge/chunk queues only routes crossing that edge/chunk, plus affected destination back-references, without scanning every route.
- Add route-splitting tests later: a fast avenue plus slower parallel roads should concentrate more load on the avenue when uncongested, then assign some load to parallel roads when virtual local load makes them competitive.
- Keep the deterministic smart-RCI count test for a full `1024` residential grid: `305,832` car pathable tiles, `305,832` pedestrian pathable tiles, `92,208` lots, and `278,484` frontage points.
- Keep the same count test for full `1024` industrial: `224,112` car pathable tiles, `224,112` pedestrian pathable tiles, `51,528` lots, and `207,024` frontage points.
- Keep a performance regression test that recalculates at least `500` source routes on the `1024` residential ideal grid without allocating map-sized scratch or scanning all map nodes in ordinary load commit.
- In the sparse/chunk topology slice, add route equivalence tests comparing dense current route results with the chunk-owned cache on straight roads, corners, intersections, disconnected roads, one-way roads, and mixed car/pedestrian access.
- In the sparse-mode slice, add a tiny rail/subway line test proving node count scales with track/station tiles rather than map size.
- In the hierarchical slice, add dense-grid fallback tests and single-chokepoint neighborhood tests before accepting any portal/tier/chokepoint cache as authoritative.

## Open Questions To Resolve During Implementation
- For single-pass nearest-destination search, should evening validation happen during candidate collection, after candidate collection, or through a resumable search that continues if later round-trip validation rejects too much capacity?
- How much extra destination capacity should the nearest-destination pass collect to avoid resuming after evening rejects?
- What is the exact point-to-point bidirectional A* stop condition for this directed graph with transfer edges and multiple start/goal access nodes?
- Should reverse movement adjacency be generated on the fly from dense neighbor cells, or should the cost map maintain explicit reverse edge lists for movement and transfer edges?
- What lower-bound heuristic should be used across mixed car/pedestrian modes and future transfers? Default: fastest uncongested tile movement with zero transfer optimism, never current congested cost.
- Whether to keep `(fromNodeId, roadDirection)` as the long-term dense movement edge identity or introduce explicit edge ids before chunk-owned topology work.
- Whether traffic overlay should update when hidden. Default: do not rebuild overlay pixels while hidden; mark touched chunks stale and rebuild lazily when the overlay is requested.
- Whether route jobs should read old load or old load plus same-source virtual local deltas. Default: nearest/repair searches read previous committed load; later route splitting may layer a per-source virtual delta before commit.
- Whether the later hierarchy should use chunk portals, transport-tier nodes, composed mini-routes, chokepoint/cut-set caches, sparse/chunk-owned topology, or a mix of those.
- Whether destination candidates should ever be bucketed separately from hierarchy. Default: not in Slice 1; evaluate alongside the hierarchy slice, and if revived, use deterministic coarse transport chunks or fixed grid cells with widening fallback to all destinations.
- Whether future destination fields should be congestion-aware. Default: use previous-tick congestion in edge costs when building the field, then still run candidate validation in reduce.
- What forced-source budget feels right at `T = 24`. The table above suggests `B = 250` drains `20,000` invalidated sources in about `3.3` logical days, while `B = 100` takes about `8.3` days.
- Whether stale invalid routes should continue contributing commute satisfaction until processed. Default: yes for smooth background repair, with a visible pending repair count; clear satisfaction only when that source is processed and no replacement route is found.
- Whether route budgeting should be source-atomic or resumable mid-source. Default: source-atomic with a conservative destination-reject cap until profiling shows individual sources dominate.
