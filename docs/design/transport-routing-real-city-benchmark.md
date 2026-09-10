# Real-city routing and publication benchmark

September 9, 2026. **Acceptance is based on the user's real city (1, 1), not the synthetic fixtures.** The final implementation improves full headless simulation throughput from **16.6 to 56.6 TPS** in the controlled comparison. It does not reach the 300 TPS target.

## Input and method

- i9-13900K, Windows, x64 Release with MSVC v143 and whole-program optimization.
- Actual `city_1_1.bin`: 1024×1024, saved tick 51,243, 3,908 lots, 29,870 transport tiles. On import: 58,855 residents, 58,773 jobs, all 58,773 jobs filled.
- Save SHA-256: `5b9e8a8232341eeef0e46d838442423275429435e447d1d1b3612d7e74afc74e`.
- Old source: commit `82f41e0056b5de8bf9020712c5c377665b32ec86`. Its routing code is unchanged; the isolated build adds only the benchmark driver/project and test friend access.
- “Routing rewrite” is the implementation the user manually checked before the publication iteration. “Final” adds publication storage/geometry/summary reuse. All versions use copies of the same saved city and deployed assets. The original save hash was checked again after benchmarking and was unchanged.
- Each process imports the same save, runs 100 warm-up ticks, then measures the next 300 ticks. Three fresh processes per version, in alternating order: 900 measured ticks per version. Means below pool all measured samples; percentiles pool the individual tick samples. No outlier runs were dropped.
- Full mode follows the normal pass order: neighbor diffusion, commands, construction progress, lot effects, commute assignment, RCI construction, local tile pass, and publication. Rendering, input arrival, frame pacing, and renderer-held buffer waits are absent. **These are headless TPS, not measured interactive FPS/TPS.** Recheck coverage and simulation work are unchanged.
- The main benchmark thread is pinned to logical CPU 0, which Windows identifies as a P-core. Worker pools retain normal scheduling: 30 tile workers and, in the new router, three background routing workers plus the calling thread. This is a benchmark control, not a change to game scheduling. An unchanged unpinned binary shifted from roughly 48 to 80 ms during exploratory runs, motivating explicit main-thread placement. Desktop scheduling still causes variation; individual trial means are retained below.
- No concurrent builds or other benchmark processes ran during performance samples. Publication fingerprint verification ran separately and is excluded from these results.

## Controlled full-tick results

| Metric | Old | Routing rewrite | Final |
| --- | ---: | ---: | ---: |
| Mean complete tick | 60.29 ms | 51.96 ms | **17.68 ms** |
| Headless TPS, reciprocal of mean tick | 16.59 | 19.24 | **56.56** |
| Tick p95 | 90.06 ms | 70.96 ms | **38.10 ms** |
| Tick p99 | 126.75 ms | 82.37 ms | **51.30 ms** |
| Routing within tick | 16.66 ms | 9.85 ms | 10.51 ms |
| Publication within tick | 39.27 ms | 37.95 ms | **3.11 ms** |
| Other tick work | 4.36 ms | 4.16 ms | 4.06 ms |
| First 100 ticks, mean (excluded from main results) | 61.93 ms | 62.41 ms | 23.10 ms |
| Import, including initial commute assignment | 9.68 s | 5.35 s | 4.91 s |
| Aggregate process cycles per measured tick | 219.1 million | 245.5 million | **152.0 million** |
| Aggregate process CPU time per measured tick | 73.61 ms | 81.30 ms | **50.12 ms** |
| Maximum observed process working set | 1.36 GiB | 1.65 GiB | 1.65 GiB |

The final version delivers **3.41× the old TPS** and **2.94× the first routing rewrite's TPS**. Aggregate cycles per tick fall **30.6% versus old** and **38.1% versus the first rewrite**. CPU time/cycles include worker threads and small measurement/logging overhead; wall timings exclude CSV writes. Memory includes the headless runtime and loaded save, not a renderer. The game-with-renderer 20 GB ceiling remains unverified.

| Trial mean tick | Old | Routing rewrite | Final |
| --- | ---: | ---: | ---: |
| 1 | 57.736 ms | 52.321 ms | 16.926 ms |
| 2 | 57.501 ms | 52.111 ms | 17.771 ms |
| 3 | 65.646 ms | 51.458 ms | 18.348 ms |

The final version is faster in every trial. Its measured throughput ranges from 54.5 to 59.1 TPS. The old third run has a larger wall-time tail without a corresponding cycle increase; it remains included rather than being removed as an outlier.

## What changed, and what did not

Publication previously rebuilt lot visual geometry, access decorations, module summaries, parameter summaries, and occupancy whenever commute metadata changed. It also discarded per-lot segment allocations. The final version retains these allocations and reuses unchanged geometry and summaries in each write buffer. Geometry rebuilds when lot state/revision, lot identity, or displayed zoning changes; construction progress advances the lot revision. Current staffing, capacity, costs, complaints, categories, and route displays still refresh. This does not share mutable buffer storage with the renderer or defer any required work.

In all full-mode repeats, both new versions end at tick 51,643 with 3,908 lots, 58,862 residents, and 58,773 filled jobs. They execute exactly **2,585 searches and settle 80,941,055 nodes** during each measured window. Both end with 3,879 route records, 2,651,993 path steps, and 189 lots reporting complaints. Publication changes therefore did not buy speed by doing fewer routing queries or changing allocation.

The old version also fills 58,773 jobs, but ends with 3,876 routes, 2,586,948 path steps, and zero complaints. It retains stale route costs where the new implementation reprices them. The old/new comparison measures the complete implementations as shipped, including the previously documented removal of commute jitter and refreshed-cost behavior; it is not an identical-query kernel comparison.

Before the publication iteration, three exploratory **routing-only** runs on this same save averaged about **13.83 ms old versus 7.22 ms new** after warm-up. This freezes tile development while keeping the normal routing recheck sequence. It excludes publication and other simulation passes, and did not pin the main thread. These results explain the initial routing benefit, but the controlled full-tick table is the acceptance measurement. Short early runs were mixed: new routing could be slower while it repaired congested routes, and the first rewrite increased aggregate cycles. The publication iteration is what turns this into a substantial whole-tick and cycle-efficiency gain.

## Correctness and build verification

Separate real-city verification runs hash every affected published field: lot render instances, lot metadata and strings, per-lot route segments, global route segments, occupancy, and relevant revisions. Struct padding is excluded. The routing rewrite and final version produce matching hashes after import, after 100 ticks, and after 400 ticks. No verification-run timing is used in the performance table.

- TransportCommuteTests: **86 checks passed**, including cached publication versus a forced full rebuild through route changes, removal, construction, and zoning; deterministic one/four-worker replay remains covered.
- TransportRoutingTests: **2,450 checks passed**.
- SaveLoadIntegrationTests: **38 checks passed**.
- RciLotConstructionTests: **1,615 checks passed**.
- TransportNetworkTests: **390 passed, 79 failed**; failure messages unchanged from the pre-change baseline.
- City Builder and AssetManager Release builds succeeded. The user reported no new regressions in the earlier routing build; the subsequent publication changes received the checks above.

## Reproduction

From the repository root, create fresh isolated sources and immutable inputs:

```powershell
python tools/prepare_city_routing_benchmark.py --baseline 82f41e0056b5de8bf9020712c5c377665b32ec86 --save 'Distributable/x64/Release/Data/Saves/city_1_1.bin' --assets 'Distributable/x64/Release/Data' --output Build/RealCityRoutingRepeat
python tools/msbuild.py 'Build/RealCityRoutingRepeat/old/City Builder/CityRoutingBenchmark.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
python tools/msbuild.py 'Build/RealCityRoutingRepeat/new/City Builder/CityRoutingBenchmark.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
python tools/run_city_routing_benchmark.py Build/RealCityRoutingRepeat --main-cpu 0 --tag full
```

Here `new` means the current checkout, including publication improvements. Choose a CPU appropriate to the machine; omit `--main-cpu` to measure default scheduling. Use `--mode routing` for isolated routing work. Add `--verify-publication --repeats 1 --tag verify` for a separate publication-fingerprint run.

Original raw samples, source/input manifests, and logs are in the ignored local directory `Build/RealCityRouting`. `controlled-full-old-*`, `controlled-full-new-*`, and `controlled-full-iter3-*` are the three versions in the primary table; `verify-full-*` contains the separate fingerprint checks. Synthetic grids remain available for algorithmic validation, but neither the commit decision nor the performance conclusions above depend on them.
