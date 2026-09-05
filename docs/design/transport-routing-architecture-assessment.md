# Routing architecture assessment — September 2026

## Recommendation and target

Target: **300 simulation ticks/second on the user's current i9-13900K, with an initial 20 GB RAM budget**. The CPU name was read from the local Windows processor registry; 32 logical processors are visible to the process. Treat 20 GB as the whole-process planning budget, including snapshots and rebuild peaks, rather than spending it all on routing.

300 TPS allows **3.33 ms for the entire tick**. Start by testing a 2 ms routing allowance, leaving 1.33 ms for other simulation work; this is a provisional allocation, not a measured fit. Rendering must run concurrently in the final benchmark. Test uncapped fast-forward: the ordinary fast mode intentionally synchronizes with presentation.

The most promising direction is a **compact, partitioned routing graph with customizable shortcuts, a separate vacancy index, and shared fields for suitable queries**. Preserve the authoritative tile network, round-trip semantics, traffic accounting, and simulation/presentation boundary. Replace the routing execution layer incrementally.

The previous plan's flat-search improvements remain useful, but are insufficient as the long-term architecture. In particular, move vacancy indexing, incremental validity, and shared cost snapshots forward rather than deferring all of them until after bidirectional A*. An order-of-magnitude reduction in routing CPU work is a reasonable experiment target, not an established result. This assessment is based on source inspection and algorithm research; no current-city profile or routing benchmark was run.

## 1. What the current implementation actually does

References below are to the source inspected on September 5, 2026; line numbers can move.

| Evidence | Consequence |
| --- | --- |
| `SimulationRuntime.cpp:2263`, `:2306`: commute assignment scans lots, computes access, and creates source/destination records each pass | Even a short search pays recurring city-wide setup |
| `SimulationRuntime.cpp:2372` and `:5973`: every ordinary pass checks all stored routes, visiting both directions' steps | The routing envelope includes **O(R × p) every tick**, beyond the selected 1% of sources |
| `SimulationRuntime.cpp:2456` onward: selected routes are copied, subtracted, cleared, then maintained routes are copied/re-added | Unchanged assignments still incur path and load work |
| `SimulationRuntime.cpp:2708` onward: each accepted destination starts another goal-building loop and Dijkstra; evening failure also restarts morning search | The single-pass nearest-destination plan has not been implemented |
| `TransportCostMap.cpp:635`: heap priority is accumulated cost | Point-to-point evening checks and repairs are still Dijkstra |
| `SimulationRuntime.h:652`, `SimulationRuntime.cpp:2562` | Commutes use one persistent scratch object and execute serially; the existing tile worker pool does not parallelize these searches |
| `TransportCostMap.cpp:827–847`: each relaxed edge evaluates congestion and request-seeded jitter | Searches repeat cost arithmetic; different requests do not have exactly the same metric |
| `SimulationRuntime.cpp:2522–2639`: maintenance classifies the saved `totalCost` | Structural validation does not refresh travel time against current traffic. Do not mistake present maintenance cost for the cost of proper congestion revalidation |
| `SimulationRuntime.cpp:1744–1748`: the lot-effects timer includes commutes and RCI construction | That timer alone cannot distinguish graph search from bookkeeping or other lot work |

Two corrections to the older memory discussion:

* `TransportPathScratch::reset` is ordinarily a stamp increment and heap clear. It does not clear all map-sized arrays per search; initialization/resizing and stamp wraparound do.
* Traffic **updates** are sparse, but `TransportTrafficLoadState::cells` is still densely allocated for both directions of the day. Moving load fields out of `TransportCostCell` did not make load storage sparse.

At 1024², there are 6,291,456 dense mode/layer nodes. From the current member layouts, using the expected native alignment:

| Dense allocation | Approximate MiB |
| --- | ---: |
| Base cells, 34 bytes/node | 204 |
| Morning + evening old/new movement loads, 64 bytes/node combined | 384 |
| Transfer offsets, approximately 4 bytes/node | 24 |
| Movement touched flags, 16 bits/node combined | 12 |
| One scratch, 26 bytes/node excluding heap | 156 |
| Subtotal with one scratch | **780** |

These are layout estimates, not process-memory measurements; they exclude transfers, heaps, routes, other network representations, overlays, and tile/render snapshots. Eight dense scratches alone take about 1.22 GiB; 30 take about 4.57 GiB. Having 20 GB makes that possible, but does not make the working set cache-friendly. At 4096² these dense allocations multiply by 16.

## 2. Worker surplus should become a cheap answer

There are three different cases, and they need different remedies.

### All compatible jobs are occupied

Today the goal vector ends up empty and no Dijkstra runs. However, every unsatisfied selected source still scans all destinations to discover this. Selected assignments are also released and reclaimed during the pass, temporarily reopening capacity.

Maintain incremental available-capacity totals per market, plus a list/index of destinations with positive vacancy. If the eligible market has zero capacity, answer immediately without constructing goals or entering the graph. Count a destination's capacity once, not once per access node or mode. Retain incumbent reservations where the chosen assignment policy permits it.

Unsatisfied sources should wait on relevant availability changes, rather than repeat an identical search on every routine visit. Capacity release, a compatible job opening, source-access changes, or an applicable topology/metric change can wake them. Negative results must have explicit dependencies; a missing destination in a small candidate cache is not a proof that no job exists.

### Vacancies exist, but this source cannot use them

This is the genuine graph-exhaustion case: disconnected components, asymmetric one-way access, evening failure, or a travel-time cutoff. Maintain conservative component/region vacancy summaries. An empty superset of possible destinations proves failure; a nonempty summary does not prove a valid commute.

Use weak components for safe coarse exclusion and directed reachability information for stronger pruning. Do not require one common strongly connected component indiscriminately: current morning/evening searches can use different access nodes and different modes. A pedestrian return and car outward trip can be legal under the current model even though the two mode graphs are separate. Any stronger test must model those endpoint semantics.

A completed search can cache an exact failure for its topology, endpoint policy, metric, cap, and eligible target set. Removing targets preserves a failure certificate. Adding targets or reducing relevant travel costs can invalidate it. A topology-only disconnection proof survives congestion changes. Conservative invalidation is preferable to incorrectly keeping a source unemployed.

### Many destinations are reachable

Reachability alone is easy; exact nearest-first allocation can still be expensive. We need not enumerate every reachable job to prove that a source's demand cannot be fully met. Available-capacity totals bound how much assignment is possible, and once that capacity has been allocated the remaining demand is immediately unsatisfied.

For a region where reachability is nearly universal, explicit legal-path upper bounds can certify broad eligibility: a source-to-hub path plus hub-to-destination path that fits the morning cap, with a separate valid evening witness. Such certificates avoid repeated feasibility searches. They do **not** prove which destination is nearest. Different hubs/modes are allowed only when the composed paths are actually legal.

There is no heuristic that guarantees tiny exploration for every exact query on an arbitrary changing graph. The scalable opportunity is to reuse proofs, avoid questions whose answer is already known, and search a smaller representation.

## 3. Stretch the existing architecture first

These changes are useful both as immediate improvements and as foundations for the replacement engine:

1. **Persist endpoint and vacancy indexes.** Update lot access from affected local topology generations and lot/module revisions. Index `accessNode -> destination IDs`, with deterministic order and deduplication by destination. Searches inspect this index when settling a node, rather than stamping every goal for every source.
2. **Resume one source search.** Keep a nearest-destination iterator alive across destination splits and evening rejects. Use separate scratch for evening validation so it cannot destroy the morning frontier. Freeze the request metric/seed for the iterator's lifetime. The current code changes seed with remaining demand, so exact legacy path ordering cannot simply be assumed.
3. **Replace unconditional route scans.** Maintain destination back-references and `routing region -> affected route IDs`. Road changes mark affected routes once. Keep structural validity separate from cost freshness. A per-route list of generations helps, but scanning all routes' generations every tick would retain an avoidable global pass.
4. **Maintain valid routes in place.** Apply load deltas only when a path or demand changes. Reprice selected routes when their metric dependencies changed; this costs path length, without necessarily requiring a new shortest-path search. Improving roads elsewhere can create better alternatives, so periodic optimization remains separate from structural validity.
5. **Accelerate known endpoints.** Start with ordinary A* and a proven lower bound; compare bidirectional A* if point-to-point work remains significant. Bidirectional stopping and mode start charges need explicit proofs/tests, rather than a stop-at-first-meeting rule.

Topology-based landmark lower bounds are a useful candidate for directed A*. ALT uses landmark distances and triangle inequalities to strengthen A* while retaining optimality. This is established algorithmic support, not evidence of a particular speedup in this city. [Goldberg and Harrelson](https://www.microsoft.com/en-us/research/publication/computing-the-shortest-path-a-search-meets-graph-theory/).

Geometric bounds must account for diagonal edges and future fast/nonlocal transfers. The safest baseline is zero until a bound is demonstrated. Current congestion configuration does not clamp speed multipliers to at most one: a lower-bound metric must use the fastest *allowed* speed, not blindly assume authored base cost is always a lower bound.

## 4. Make costs shareable

Introduce an immutable routing metric snapshot for each time of day and genuinely distinct travel profile. Compute an edge's traffic-adjusted weight once when its committed load, capacity, or curve changes; ordinary searches read that value. This avoids division, curve traversal, and repeated load lookups in every relaxation.

Current jitter is small but **adds to distance**, using a seed derived from source, demand, tick, and revision. It prevents one shared exact field or customized shortcut weight from representing every request.

Recommended policy: shared travel-time weights, with deterministic route diversity handled separately. Tie-breaking among equal-cost choices is one option; choosing from a bounded set of near-optimal alternatives is another, with an explicit detour limit. Neither reproduces today's additive jitter exactly. Record this as a routing-policy change and compare load distributions, not just shortest distances.

If additive jitter must remain exact, use shared unjittered distances as optimistic bounds and cached paths as candidates; final search must optimize the request-specific metric. A small fixed number of jitter profiles could share caches, but would also change behavior. Do not build a separate field per source.

Use unambiguous cache result types:

| Result | What it authorizes |
| --- | --- |
| Current exact distance/path | Exact query result for its declared epochs and profile |
| Lower bound | Safe pruning or A* guidance; never proof of successful travel |
| Valid path witness at current costs | Success within the cap and an upper bound; not proof of shortest travel |
| Candidate hint | A destination/path worth checking; no completeness claim |

Topology, weights, access, and capacity need separate versions. A vacancy change should not rebuild road shortcuts. A road deletion should not be confused with congestion. An old traffic-adjusted distance is not automatically a lower bound when traffic improves.

## 5. Preferred routing graph: compact partitions and exact shortcuts

Keep tile/lane data authoritative. Build compact outgoing and incoming adjacency over active `(tile, layer, mode)` states, with stable external edge identities and chunk generations. Search arrays should be contiguous and indexed by compact IDs, not hash lookups per relaxation. Chunk ownership localizes road edits; immutable snapshots may use a compact global search numbering with explicit versioning.

Compress nonbranching directed chains when all transitions and endpoint offsets can be preserved. Lots along a street must remain addressable without turning every frontage tile into a mandatory top-level junction. Two-tile streets may contain lateral transitions and many actual branches: do not assume every visible street becomes one graph edge. Keep exact edge sequences for reconstruction and loads.

Above that graph, prototype a **Customizable Route Planning (CRP)-style multilevel overlay**. The established algorithm separates topology partitioning, metric customization, and queries. That separation is a good match for stable roads and changing traffic. Its published road-network results are not performance predictions for this simulation. [Delling et al., Customizable Route Planning](https://www.microsoft.com/en-us/research/wp-content/uploads/2011/05/crp-sea.pdf).

Proposed integration:

* Partition routing regions independently of renderer chunk size, initially with deterministic tile-aligned ownership and measured boundary counts.
* Preserve all legal directed boundary crossings. Summarize travel through each region with exact within-region distances for the metric epoch, then combine regions at higher levels.
* Refine source/destination regions and unpack accepted shortcuts to original edges. Intermediate travel can skip tile-level exploration.
* Update dirty metric regions and affected ancestors before publishing that metric's exact overlay. Rebuild affected topology regions after edits; repartitioning can be a separate operation.
* Fall back to exact base-graph search or valid lower-bound guidance when customized data is unavailable. A stale shortcut cannot silently be accepted as exact.

Dense grids are the important stress case: a region with `b` boundary states can require O(b²) directed shortcut entries. Measure shortcut fill, customization time, and cache misses. Avoid selecting only a few convenient portals; that can lose the best route or even connectivity. Road hierarchy alone is insufficient because local streets can be optimal and congestion can favor them.

Compare **Customizable Contraction Hierarchies (CCH)** on the same compact graph if overlay boundaries are too expensive. CCH separates topology preprocessing from metric customization and has been evaluated on both road and game maps. It is a serious alternative; frequent topology edits and actual shortcut fill decide which fits here. [Dibbelt, Strasser, Wagner](https://arxiv.org/abs/1402.0402).

## 6. Put destination availability beside the hierarchy

Shortcuts make a known journey cheap. A separate hierarchical destination index makes *finding an available destination* cheap.

Keep compatible available-capacity counts at leaves and ancestors, plus exact destination membership at leaves. Skip a zero-capacity region as a destination container, while still allowing routes to transit it. Use admissible source-to-region lower bounds to visit promising regions first. Expand a region whenever it could contain a better eligible candidate than the current result; continue after capacity conflicts or evening rejection.

This is branch-and-bound with a complete fallback, not "try the nearest 16 buildings". A small candidate list is an accelerator, not a reason to report no job. One-way detours, multiple access nodes, equal-distance ordering, and distant highway access must be represented in the bound and completion rule.

Prototype the index over the existing graph before the full overlay. The destination data model survives the migration. For research context, network nearest-neighbor studies compare incremental network expansion and hierarchical destination indexes, and show why memory layout matters alongside algorithm choice. [Abeywickrama et al.](https://www.vldb.org/pvldb/vol9/p492-abeywickrama.pdf).

## 7. Shared fields: strongest for services, conditional for jobs

Expose separate query intents rather than routing everything through `findPath`:

| Intent | Preferred work shape |
| --- | --- |
| Repair one known commute direction | Exact point-to-point query |
| Fill demand from available destinations | Resumable capacity-aware candidate iterator + round-trip validation |
| Distance/access to any interchangeable service | Shared multi-source distance field |
| Response from any fire/police station | Shared outward field on the correctly directed network |
| Assignment to finite school/healthcare/shop capacity | Availability index and allocation, optionally accelerated by fields |
| Inspect/display an accepted journey | Unpack a stored route only when needed |

For residents traveling to services, seed facilities on the reversed graph. For responders traveling from stations, propagate forward. Include origin mode charges correctly: virtual endpoint edges make it explicit where the current 60-second car start penalty belongs, including when reversing a query.

A category-wide field computes nearest distance once per metric/facility epoch, followed by cheap lookups across many lots. It does not automatically answer "all reachable facilities," sum every facility's contribution, enforce shared capacity, or prove round-trip access to the **same** facility. Two nearest fields can select different facilities. Those queries need retained identities, more labels, or candidate validation.

Job fields are less stable: when the nearest employer fills, an entire field's labels may become unusable. Keep several destination labels, repair affected areas, or resume a complete candidate search; do not rebuild a whole-city field after every filled employer. Use a measured reuse threshold to decide between a field and individual queries.

When jobs are scarce, searching from the scarce destination side to many homes can share much more work. However, job-first allocation changes the existing source-order greedy policy. Treat that as a separate simulation-policy experiment; the default migration should preserve the chosen source order and capacity rules. An all-pairs home/job distance matrix is unnecessary and potentially enormous.

## 8. Thread pool and deterministic allocation

Parallelism reduces elapsed time; removing repeated work reduces aggregate CPU cycles. Measure both.

1. Build immutable topology/metric and endpoint snapshots plus a versioned capacity view.
2. Dispatch independent candidate/repair jobs in batches, grouped for spatial reuse. Give each active worker reusable scratch and local outputs. Bound the number of live resumable frontiers; do not retain a full graph scratch for every source.
3. Reduce in a declared stable source and candidate order. Assign capacity exactly once; workers cannot reserve jobs by racing atomics.
4. If earlier sources consume a later source's candidates, resume that source's search. Reaching the end of a prefetched list does not mean no destination exists. Speculative work cannot change source priority or let a later source jump ahead of an unresolved earlier source if exact greedy order is required.
5. Apply accepted changes as sparse, deterministically merged edge deltas, then publish loads and the next metric snapshot.

Maintaining all incumbents before filling new demand is an attractive policy, but can differ from today's interleaved source-by-source maintenance and fill. Specify that choice before claiming assignment equivalence. More speculation near market exhaustion may waste cycles; shared vacancy summaries and early cancellation make that phase cheaper.

Do not automatically copy the tile pool's `logical processors - 2` worker count. Benchmark routing at 1, 2, 4, 8, and additional workers on this machine, including rendering contention. Its hybrid CPU and shared caches make equal worker throughput an unsafe assumption. Prefer scheduling changes justified by measurements over hard-coded affinity rules.

Use wider exact authoritative load counters and signed worker deltas before parallel reduction. Current saturating 16-bit accumulation cannot be reversed exactly after overflow, and applying saturation between partial reductions can change the result. Clamp only at a defined presentation/compatibility boundary, or explicitly retain and test the existing saturation semantics.

## 9. Cache invalidation and repair work

Maintain reverse indexes from destinations and topology regions to dependent routes. Accepted routes keep original edge identities plus a compressed path/shortcut representation. If a graph snapshot renumbers compact nodes, route identity must not silently refer to a different edge; transfer-vector indices need particular care across rebuilding/finalization.

For topology: invalidate affected witnesses, routes, endpoint caches, and shortcuts. For traffic: update changed weights and affected summaries. Repricing a still-legal path can be much cheaper than searching. Threshold bounds over unchanged path portions can further avoid full repricing, but require a proof that the commute classification cannot change.

Repair budgets smooth edits; they are not throughput improvements. Measure pending work and repair latency alongside TPS. The target cannot be met by letting the backlog grow indefinitely or reducing the amount of revalidation per simulated day without calling out that policy change.

If a per-tick limit is needed, count deterministic work units such as settled nodes/relaxed edges and support continuation; a limit on search count alone does not bound one pathological source. Keep incomplete distinct from unreachable. Define snapshot lifetime and restart rules for work spanning ticks, including cancellation on relevant edits.

## 10. What to benchmark before promising 10×

Add separate timings and counters for endpoint/index maintenance, all-route validation, candidate setup, morning search, evening search, path repricing/reconstruction, load deltas, overlay publication, customization, capacity conflicts, and reduction. Count settled nodes, relaxed edges, heap operations, destination rejects, route bytes copied, and cache result types. Measure total routing CPU time/cycles across workers as well as wall time.

Use reproducible saved cities and synthetic fixtures:

| Case | What it distinguishes |
| --- | --- |
| Current user's city at about 1/8 fill | Reproduces the reported problem |
| 1/4, 1/2, full 1024² with a fixed mixed-use layout | Scaling with source count, not just population |
| More jobs than workers, balanced, more workers than jobs | Success versus saturation behavior |
| Zero compatible vacancies | Should enter no graph search and do no per-source destination scan |
| Only disconnected or evening-infeasible vacancies | Negative-result reuse and directed correctness |
| Dense grid versus bridge neighborhoods | Portal density versus actual chokepoint benefits |
| Heavy traffic changes and a highway deletion | Customization cost and repair backlog |
| Multiple service categories and scarce finite capacity | Field reuse versus candidate/allocator overhead |

Hold map, simulation-day recheck coverage, assignment policy, and cost semantics fixed for each comparison. Compare reference Dijkstra results on small directed fixtures with transfers and multiple endpoints. Once jitter/tie policy changes, use the same new metric in both reference and optimized engines; retain the legacy implementation as a behavioral comparison, not an impossible exact oracle.

Report:

* complete accepted round trips and completed source allocations per million aggregate routing CPU cycles;
* settled/relaxed nodes per successful query and per exhausted query;
* median/p95/p99 whole-tick and routing wall time, mean sustained TPS;
* backlog size and age, steady-state versus edit recovery, cache/customization/rebuild cost;
* private working set and peak resident memory with old/new graph generations alive;
* cold-cache, warm steady state, and multi-worker scaling with the renderer active.

For illustration, 500 selected sources/tick at 300 TPS means 150,000 source visits/second. A 2 ms routing allowance means 4 microseconds of *amortized batch wall time* per source. That does not allow a broad independent search for every visit; many visits must become index lookups or unchanged-route checks, with expensive work shared across the batch.

Use the measured non-routing time to set expectations: `tick_new = nonrouting + routing / speedup`. A 10× routing speedup does not imply 10× TPS. Include preprocessing and ongoing customization in the routing term; faster query timings alone can hide a net regression.

## 11. Revised delivery order

1. Instrument the current city and freeze correctness/performance fixtures. Confirm how much time is search versus all-route scans and market setup.
2. Add incremental access/vacancy/reservation data, zero-vacancy early exit, indexed structural invalidation, and in-place maintenance. Establish explicit cost freshness.
3. Split APIs; implement a resumable nearest-destination iterator and exact A* endpoint repair. Define shared weights/tie policy and the differential oracle.
4. Build compact active adjacency and immutable cost snapshots. Introduce bounded parallel jobs and deterministic reduction; measure aggregate cycles as well as elapsed time.
5. Prototype the multilevel overlay and hierarchical vacancy index; compare against the compact flat engine and a CCH prototype if boundary fill warrants it. Keep only an end-to-end win that includes customization and edits.
6. Add service fields and selective job-field reuse. Test finite-capacity services separately from nearest-service coverage.

This sequence avoids spending the first large implementation entirely on bidirectional A* while leaving global work intact. It also avoids betting on a single universal field that fails as soon as jobs fill. The intended result is a reusable transport query engine whose clients choose the right query shape, with most unchanged or impossible work answered from maintained state.
