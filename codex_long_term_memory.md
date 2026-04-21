# Codex long-term working memory

Snapshot: 2026-04-21

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
- `fast_forward` is allowed to let simulation outrun presentation; if disabled, simulation may wait for the renderer to catch up.

## Next renderer doctrine
- The next renderer milestone should move from CPU-built screen-space quads toward world-space camera projection and chunked instance rendering.
- The shortest path is:
  - add a real camera with view/projection matrices
  - keep camera motion constrained at first rather than free-fly
  - render chunk-local instance buffers instead of rebuilding visible tile quad vertices every frame
  - rebuild chunk render data only when a chunk is dirty
  - frustum-cull chunks before drawing
- The renderer should transition to proper 3D in stages:
  - world-space projected tiles/lots first
  - instanced simple prisms/boxes next
  - richer terrain/building meshes later
- Do not jump directly to final art or complex meshes before chunked instance rendering exists, because zoomed-out performance will remain dominated by CPU-side geometry generation otherwise.

## Performance checkpoint
- After the architecture refactor to chunked tile simulation, separate renderer ownership, and `x64 Release`, observed simulation throughput improved from roughly `900` updates/sec to roughly `1800` updates/sec on the user's machine.
- That is a meaningful gain, but it also shows the original prototype held up better than expected for novice-era code.
- Observed CPU utilization was still high, around the `80%` range, which suggests the simulation is not purely memory-starved and likely still has meaningful CPU-side optimization headroom.

## Next optimization doctrine
- Before deeper optimization, add per-pass timing for:
  - neighbor pass
  - local pass
  - lot effects
  - command apply
  - publish/snapshot path
  - renderer frame time
- The current lowest-hanging simulation fruit is likely control-flow overhead rather than pure arithmetic:
  - mutex-heavy chunk dispatch
  - `std::function` overhead in the worker hot path
  - copying published lot render data every frame
  - the write-buffer fallback spin/sleep path
- Keep the tile-object model for now, but a future hot/cold field split is on the table if profiling shows the current tile layout leaving performance behind.
- AVX2 is worth considering as an experiment, but it should use intrinsics rather than inline assembly on MSVC x64.
- SIMD work should follow profiling and probably target row-wise contiguous updates first, not blind hand-vectorization of everything.

## Compile/doctrine notes
- Current release settings already include the important baseline optimization path.
- If further CPU-side tuning is needed, `x64 Release` is the place to consider targeted flags such as AVX2 support, but only once the project is comfortable treating that instruction set as part of the machine baseline.
- Prefer structural wins first:
  - cheaper chunk scheduling
  - cheaper published snapshot handling
  - chunked renderer updates
  - only then SIMD experiments

## Guardrails
- Do not drift into object-heavy "simulate everything literally" design just because modern hardware allows more brute force.
- Do not let graphics ambition erase the clarity and scalability of the tile-statistical core.
- Do not overfit chunk sizing to one exact CPU; detect and override should coexist.
- Use comments only at load-bearing seams so future work stays explainable without drowning the code in narration.

## Short mnemonic
Tile-first simulation, cache-aware chunking, real buffer swapping, renderer as presentation only, and enough architectural discipline to let the prototype grow into the game you actually want.
