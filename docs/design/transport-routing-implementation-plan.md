# Routing implementation record

Started September 9, 2026. Target: 300 TPS on the current i9-13900K, within an initial 20 GB whole-process memory budget. Changes were initially left uncommitted for manual regression review. The user subsequently reported no new regressions and authorized a commit if a real saved-city comparison demonstrated improved performance.

**Performance acceptance uses real saves.** Synthetic fixtures are design tools and correctness checks, not evidence that the implementation succeeded in the game. The implementation and publication follow-up goals were completed in commit `79e0319`; the 300 TPS target remains unmet. Further performance work must preserve functionality, accuracy, and revalidation coverage. See [the saved-city benchmark report](transport-routing-real-city-benchmark.md) for the acceptance measurements.

## Contract

Keep directed movement, transfers, independent morning/evening endpoint modes, the commute cap, source-order allocation, sparse traffic publication, and save compatibility. Shared routing uses traffic travel time with deterministic equal-cost ordering instead of additive per-request jitter; retain the old Dijkstra API as a differential reference. Incomplete candidate work must never be reported as unreachable. Recheck coverage must not be reduced to inflate TPS.

## Stages and acceptance

1. **Baseline and observability.** Build/run existing transport and simulation tests; add independent routing counters/timing and reproducible correctness/performance fixtures. Record baseline and final measurements separately from the 300 TPS target.
2. **Incremental commute state.** Cache access, track vacancies and forced work without repeated scans, index route dependencies, preserve maintained routes and loads, and refresh selected path costs. Verify edits, destination removal/capacity changes, save/import, and saturated markets.
3. **Query intent split.** Add a resumable nearest-destination iterator and exact endpoint A*, with separate scratch for return validation. Verify demand splitting, rejected returns, mixed modes, transfers, congestion, and exact results against the reference.
4. **Shared routing engine.** Build compact active forward/reverse adjacency and immutable shared cost snapshots. Use persistent bounded worker scratch and deterministic allocation/reduction. Verify worker-count independence and measure scratch, graph build/update, and query cost.
5. **Hierarchy experiment.** Prototype complete region shortcuts and capacity summaries. Measure dense grids and chokepoints, including customization and edit costs; enable only if end-to-end measurements justify it. Retain complete compact-graph fallback. Compare CCH only if overlay boundary fill warrants the additional prototype.
6. **Shared fields.** Provide directed service-distance fields over the same graph, with endpoint start charges and version checks. Benchmark reuse; enable job-field acceleration only when measured reuse exceeds update cost. Existing finite-capacity assignment continues to use the allocator.
7. **Final validation.** Run appropriate Release builds, transport and simulation regressions, deterministic replay/differential fixtures, and scalability measurements. Update this document with implementation outcomes, policy changes, experiment decisions, and remaining manual checks. Do not commit.

## Progress

- [x] Harness goal created; initial working tree clean.
- [x] Baseline and observability.
- [x] Incremental commute state.
- [x] Query intent split.
- [x] Shared engine and deterministic workers.
- [x] Hierarchy experiment and measured decision.
- [x] Shared fields and reuse measurements.
- [x] Final validation and review notes.

The stages are ordered by dependency. Shared graph structures may be introduced with the query APIs to avoid implementing two throwaway search engines. Experimental accelerators are accepted on measured total cost, not query time alone.

## Implemented result

`TransportRouter` is now the commute query engine. It prepares compact active forward/reverse adjacency from `TransportCostMap`, caches morning/evening weights, uses a bounded multi-reader journal for sparse weight updates, and retains dense edge identity for route display and traffic. Persistent stamped scratch scales with active nodes. Topology rebuilds currently rebuild the compact graph globally; chunk-owned rebuilding and chain compression have not been added.

The runtime maintains cached lot access, source/destination records, vacancy capacity, staffing totals, source/destination back-references, and 32-tile route/access dependency regions. Existing city-parameter reduction now supplies per-lot commute amounts rather than recomputing desirability and contributions again inside routing. Ordinary ticks no longer validate every route or scan every destination per source. Unchanged records and market state are reused. Completed failure results carry metric and availability versions; weak-component capacity checks and zero-vacancy checks avoid impossible searches.

Nearest-destination discovery is resumable, deduplicates destinations with multiple access points, and continues across evening failures and demand splits. Known-endpoint queries use A* with an integer optimistic Manhattan bound computed over **all** movement and transfer edges, including diagonals and nonlocal transfers. Extremely cheap transfers can reduce that bound to zero safely. The original Dijkstra API remains available, including its legacy jitter behavior, for reference tests.

The bounded persistent pool reprices/repairs selected routes and validates batches of up to eight return paths. The simulation thread reduces capacity and applies traffic in stable source/candidate order. It participates as a worker; one-job batches avoid waking the pool. `RuntimeOptions::routingWorkers` defaults to four participants and is clamped to 1–8 in the runtime. Morning source discovery remains sequential across sources to preserve greedy allocation without speculative cross-source restarts.

Maintained routes stay in place, and their traffic is untouched unless path or demand changes. The runtime refreshes selected route costs against committed traffic and reuses them when the metric snapshot is unchanged. Counters are exposed through `RuntimeTimingSnapshot`: commute total/setup/search+maintenance/commit time, searches, settled nodes, candidates, and refreshed endpoints.

Authoritative traffic loads now use 32-bit counters, retaining exact add/subtract above 65,535. Individual route demand/load serialization and display shapes remain unchanged. Overflow beyond the 32-bit aggregate limit raises an explicit error. Transfer-table identity changes conservatively reset commute routes/loads because sorted transfer indices are not stable external IDs yet.

## Measured experiment decisions

Release measurements on the current i9-13900K, with deterministic synthetic fixtures. These are local samples, not a promise about arbitrary cities or rendering load. Logs are under `Build/RoutingValidation/` (ignored build output).

| Fixture | Reference/common-weight Dijkstra, 64 queries | Compact A*, 64 queries | Other costs |
| --- | ---: | ---: | --- |
| Uniform 256² directed grid | 97.43 ms | 1.32 ms | Compact graph preparation 15.54 ms; one reverse field 4.46 ms |
| Uniform 1024² directed grid, earlier sample | 3,145.67 ms | 8.36 ms | Compact graph preparation 270.77 ms; one reverse field 92.91 ms |

The 256² single-thread query sample consumed about **291.6 million reference cycles versus 3.94 million compact-query cycles**, measured with Windows `QueryThreadCycleTime`. This unusually favorable uniform grid is a useful regression fixture, not a representative city speedup. Pool measurements report aggregate **query-kernel** thread cycles separately from batch wall time; they exclude dispatch, reduction, and preparation. Additional workers can reduce wall time while increasing aggregate cycles. No claim of 10× whole-city TPS is made.

The exact single-level region-overlay prototype preserves all boundary crossings and unpacks witnesses to base edges. At 256² it built 813,480 shortcuts using 11,283,920 witness edges: customization took 129.18 ms and 64 queries took 75.37 ms. This loses badly to compact A*. At 1024² it exceeded the benchmark's 32-million-witness-edge storage limit and fell back completely. **The overlay is not enabled in commute routing.** It remains a bounded experimental API with stale-data and memory-budget fallback tests. A CCH implementation and deeper hierarchy are deferred: the measured flat endpoint engine is already much faster, and the next hierarchy should be justified by remaining real-city query profiles rather than shipping this losing overlay.

Directed shared fields are implemented and tested for travel toward services and outward response, preserving origin mode charges and nearest target access identity. They reject use with the wrong router or stale metric. There are no school/fire/police/healthcare consumers yet, so no new service gameplay is invented. Category fields are appropriate for large reusable query batches, but the measured full-field build cost rules out blindly rebuilding them every tick. Volatile finite-capacity jobs continue through the destination iterator and allocator; a job-field cache is not enabled without a favorable measured reuse workload.

## Commute pipeline scaling sample

The headless fixture uses 1x1 lots, alternating eight residents/eight jobs, short pedestrian routes, a connected street grid, and the existing 1% rolling recheck rate. It includes city-parameter reduction, bookkeeping, maintenance, and traffic commit, but excludes tile passes, construction, publication, and rendering. After warm-up, both samples ran 120 passes with zero new searches and zero access refreshes.

| 1024² fixture | Lots | Initial assignment | Mean steady pass | p95 | p99 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1/8 developed | 16,384 | 172.19 ms | 0.296 ms | 0.347 ms | 0.499 ms |
| Fully developed | 131,072 | 422.58 ms | 2.419 ms | 2.673 ms | 6.559 ms |

The full fixture originally took 14.63 ms per steady pass during this implementation. Reusing source/destination records, parameter amounts, staffing, and satisfaction state removed the remaining repeated bookkeeping passes. This comparison is between implementation checkpoints, **not** a pre-change city benchmark.

## Behavior changes and practical limits

- Commutes now minimize shared traffic travel time with deterministic tie ordering. Per-request additive jitter remains only in the legacy reference API. Equal-cost route distributions can differ; inspect traffic concentration and congestion feedback manually.
- Selected paths now actually refresh costs from committed traffic. Previously their saved cost could remain stale. Complaint changes and subsequent rerouting may therefore differ even with unchanged road topology.
- A medium direction receives one retry; a still-medium retry is retained until its next selected check, then triggers reassignment. This follows the earlier documented retry policy instead of the old immediate rejection after retry.
- Road/access invalidation uses conservative 32-tile regions; affected sources may include routes adjacent to an edit. There is no new per-tick work cap or reduction in recheck coverage. Large graph rebuilds, initial assignment, and forced repair can still exceed the steady-state tick allowance.
- Dense authoritative topology and load arrays remain. The router removes dense **worker scratch**, but the change is not a fully sparse transport-storage migration. The wider load arrays increase that backing memory. No 20 GB whole-process peak measurement with rendering has been made.
- This implementation does not establish 300 whole-game TPS, particularly under constant edits, long congested routes, or future service-field rebuilds. The user's saved-city/renderer regression pass remains the acceptance check for those scenarios.

## Validation commands

Build each project with `python tools/msbuild.py 'City Builder/<project>.vcxproj' /p:Configuration=Release /p:Platform=x64 /m`, then run its executable from `Distributable/x64/Release`:

- `TransportRoutingTests`: randomized directed/multimode reference comparisons, congestion, duplicate access nodes, demand candidate continuation, weak components, sparse metric updates, field direction/epochs, pool lifecycle, exact overlay witnesses/fallback, and reversible large traffic loads. Add `--benchmark 256` or `--benchmark 1024` for deterministic kernel measurements.
- `TransportCommuteTests`: deterministic controlled runtime checks with one/four workers, splitting, saturation, in-place maintenance, endpoint reuse, unrelated graph edits, employer removal/capacity changes, road cuts/reconnection, evening rejection, and complete route replay. Add `--benchmark 1024` for the pipeline scaling fixture.
- `SaveLoadIntegrationTests`, `RciLotConstructionTests`, and the existing `TransportNetworkTests` for integration coverage.
- `City Builder` and `AssetManager` for application/editor build integration.

Before runtime edits, the existing transport suite reported **390 passing and 79 failing checks**. Its failure messages are saved in `baseline-transport.log`; compare with `current-transport.log`. These pre-existing topology/render fixture failures must not be represented as passing or attributed to this routing change. Final counts and build status are recorded below after final validation.

## Final validation and manual review

All builds and tests below used x64 Release on September 9, 2026:

| Check | Result |
| --- | --- |
| TransportRoutingTests | 2,450 checks passed; no failures |
| TransportCommuteTests | 86 checks passed, including identical complete route replay with one/four workers, live residential module removal, and cached publication versus forced rebuild through construction/zoning changes |
| SaveLoadIntegrationTests | 38 checks passed |
| RciLotConstructionTests | 1,615 checks passed |
| TransportNetworkTests | 390 passed, 79 failed; comparison of failure messages against the pre-change baseline found no new or changed failures |
| City Builder and AssetManager | Both application/editor builds succeeded |
| git diff --check | Passed |

The initial harness implementation goal is complete. The user performed the manual regression pass and reported no new regressions. The 300 TPS target has not been reached; whole-process memory with a renderer is still unverified. Deterministic shared costs replace commute jitter, and complaints respond to freshly repriced routes. Initial assignment and forced topology repair are intentionally still fully processed rather than hidden behind reduced work budgets.

## Real-save follow-up

The saved-city profile identified publication as the dominant cost. The retained iteration reuses each write buffer's lot geometry and occupancy until lot revision, lot identity, or displayed zoning changes; construction already advances the lot revision. It also retains per-lot route-segment allocations and reuses module/parameter summaries when their lot state is unchanged. Dynamic staffing, commute costs/categories, complaints, current capacity, and route-display data continue to refresh normally. No shared mutable renderer storage or reduced simulation work was introduced.

`CityRoutingBenchmark` reads an immutable copy of a real save through the existing loader and supports routing-only and full headless tick modes. The full mode executes the normal simulation pass order, including construction and publication. `tools/prepare_city_routing_benchmark.py` creates isolated baseline/current sources, input copies, and hashes; `tools/run_city_routing_benchmark.py` runs them sequentially in alternating order. Optional publication fingerprints compare the complete affected payloads field by field without hashing struct padding. Those verification runs are separate from performance samples.

## Route display follow-up

The subsequent query/preview fixes add complete road-route selection, smooth time-scaled arrow ribbons, immutable route clocks refreshed on repricing, and correct opaque/ghost depth ordering. They preserve route allocation and save data. The user visually tested this build and reported that it looks and works well. Current renderer behavior, test counts, and the separate real-save overhead measurements are in [renderer notes](renderer.md); the earlier validation table above records the routing commit, not the later display revision.
