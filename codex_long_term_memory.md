# Codex long-term working memory

Snapshot: 2026-05-17

## Project identity
This project is a modern heir to SC2000 and SC4 built around tile-based statistical simulation rather than full real-time object truth.

Primary design goals:
- preserve the mayor-scale feeling of shaping city conditions instead of puppeteering units
- keep the simulation tile-first and statistics-first
- scale toward maps around `1024 x 1024`, roughly sixteen times the tile count of SC4's largest city
- leave room for much richer presentation, potentially substantial 3D, without making rendering the source of simulation truth

## Long-term engineering doctrine
- `x64 Release` is the primary build target.
- The first milestone keeps an array-of-objects tile model because that is a good clarity/performance compromise for this stage.
- Rendering and simulation should be separate systems with a defined handoff contract.
- The simulation should use true buffer swapping rather than copying the current world into an "old" array every tick.
- Player input should become queued commands applied at simulation boundaries.

## Simulation/cache doctrine
- The world should use chunked parallel passes over tile buffers.
- Chunk size should be calculated, not guessed.
- The working-set budget should come from detected L2 cache per logical thread when possible, with an easy manual override.
- A declared usable-cache fraction, currently `0.75`, should control how aggressively chunk size fills the budget.
- Chunk geometry should stay rectangular and evenly divide the map.
- There should be many more chunks than worker threads so stragglers do not dominate the frame.

## Render handoff doctrine
- Triple buffering is the target state:
  - one published buffer for rendering
  - one simulation read buffer
  - one simulation write buffer
- Rendering should read only committed world state, never mutable simulation buffers.
- The published renderer contract now includes:
  - tile buffer pointer
  - lot render snapshot pointer
  - per-chunk render revision pointer
  - resolved road-cell snapshot pointer
  - packed ground-road render-state pointer
  - traffic overlay state pointer
  - split ground/elevated road chunk-revision pointers
  - published generation
  - lot revision
  - road revision
- `fast_forward` is allowed to let simulation outrun presentation; if disabled, simulation may wait for the renderer to catch up.

## Renderer doctrine
- The project has now crossed the main seam from CPU-built screen-space quads to world-space rendering.
- Current state:
  - constrained pitched perspective camera
  - view/projection matrices
  - raycast picking onto the ground plane
  - per-chunk persistent tile instance buffers for static tile origin/UV payloads
  - frustum-cull chunks before drawing
  - stream compact scalar tile debug data only for visible stale chunks
  - stream lot lift masks only for visible stale chunks
  - stream ground-road placeholder visuals through a packed road-state texture in the tile pass
  - stream reusable per-tile overlays through a published RGBA texture, starting with traffic capacity
  - keep elevated roads in a separate lazy visible-chunk pass
  - render lots as separate placeholder world-space prisms
- The renderer should still transition to richer 3D in stages:
  - world-space projected tiles/lots first
  - instanced simple prisms/boxes next
  - richer terrain/building meshes later
- Do not jump directly to final art or complex meshes before the renderer ownership is cleaner and chunked world-space instancing is stable.

## Performance checkpoint
- After the earlier architecture refactor to chunked tile simulation, separate renderer ownership, and `x64 Release`, observed simulation throughput improved from roughly `900` updates/sec to roughly `1800` updates/sec on the user's machine.
- After the commute routing rework, the user's pathfinding-heavy test city improved from about `10` TPS before route/load work, to about `14` TPS with persistent scratch, then to about `200` TPS with sparse morning/evening load states and touched overlay updates. A city with no pathfinding can reach about `2000` TPS, which points back at tile-based updates as the broad ceiling when routing is idle.
- This renderer/runtime pass removed several obvious structural costs:
  - snapshot lot-vector copying on acquire
  - hot-path `std::function` dispatch in chunk workers
  - sleep-poll write-buffer waiting
- The cleanup pass also removed low-value codebase churn:
  - legacy unused helper/source files
  - tracked debug output artifacts
  - the empty `LotModule.cpp` stub
- Per-pass timing now exists in the runtime/renderer status print, so future optimization work should start from measured behavior instead of guesswork.
- Current renderer timing splits now expose:
  - tile upload/draw
  - ground-road upload
  - elevated-road upload/draw
  - lot upload/draw

## Next optimization doctrine
- Prefer structural wins first:
  - continue measuring visible-chunk tile-state upload and packing cost
  - cheaper lot-effects iteration
  - better renderer code organization now that `Renderer.cpp` is carrying a lot of camera/math/GPU setup logic
  - only then deeper data-layout or SIMD experiments
- Keep the tile-object model for now, but a future hot/cold field split is still on the table if profiling shows the current tile layout leaving performance behind.
- AVX2 is worth considering as an experiment, but it should use intrinsics rather than inline assembly on MSVC x64.
- SIMD work should follow profiling and probably target row-wise contiguous updates first, not blind hand-vectorization of everything.

## Compile/doctrine notes
- Current release settings include the important baseline optimization path and now also enable `/MP` in `Release|x64`.
- MSBuild is expected to be available through `PATH` in the current local environment.
- User-local Visual Studio state and build products should stay ignored rather than tracked in the repo.
- Prefer structural wins before raising the machine baseline with ISA-specific flags.

## Guardrails
- Do not drift into object-heavy "simulate everything literally" design just because modern hardware allows more brute force.
- Do not let graphics ambition erase the clarity and scalability of the tile-statistical core.
- Keep traffic simulation statistical: pathfinding assigns aggregate loads, congestion reads old loads, and future building batches should write new loads through worker-local deltas before reduction.
- Traffic load state is now split into morning and evening parallel states over one stable base transport graph. Commute destinations must be round-trip valid: morning home-to-job and evening job-to-home.
- Logical days and simulation ticks are separate. `SimulationTime::ticksPerDay()` is currently `2`; authored day durations should be converted through `SimulationTime::daysToTicks()` at load/setup boundaries.
- Prefer integration-style sandbox tests for complex systems. For roads, pathfinding, and commute assignment, exercise player-like action sequences and micro-simulations instead of relying mainly on isolated helper unit tests.
- The sandbox harness should eventually become reusable across test targets with map size, tick count, scheduled actions, lot placement, bulldoze, query, and tool-level operations.
- Do not overfit chunk sizing to one exact CPU; detect and override should coexist.
- Use comments only at load-bearing seams so future work stays explainable without drowning the code in narration.

## Focused design guides
- `docs/design/renderer.md` captures renderer upload, culling, and shader decisions.
- `docs/design/simulation-threading.md` captures tile update and triple-buffer rules.
- `docs/design/lots.md` captures lot/module placement and effect rules.
- `docs/design/xml-assets.md` captures archetype import assumptions.
- `docs/design/transport-network.md` captures road topology and render-state rules.

## Short mnemonic
Tile-first simulation, cache-aware chunking, real buffer swapping, renderer as presentation only, world-space chunk instancing, and enough architectural discipline to let the prototype grow into the game you actually want.
