# Codex long-term working memory

Snapshot: 2026-04-08

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

## Performance checkpoint
- After the architecture refactor to chunked tile simulation, separate renderer ownership, and `x64 Release`, observed simulation throughput improved from roughly `900` updates/sec to roughly `1800` updates/sec on the user's machine.
- That is a meaningful gain, but it also shows the original prototype held up better than expected for novice-era code.
- Observed CPU utilization was still high, around the `80%` range, which suggests the simulation is not purely memory-starved and likely still has meaningful CPU-side optimization headroom.

## Guardrails
- Do not drift into object-heavy "simulate everything literally" design just because modern hardware allows more brute force.
- Do not let graphics ambition erase the clarity and scalability of the tile-statistical core.
- Do not overfit chunk sizing to one exact CPU; detect and override should coexist.
- Use comments only at load-bearing seams so future work stays explainable without drowning the code in narration.

## Short mnemonic
Tile-first simulation, cache-aware chunking, real buffer swapping, renderer as presentation only, and enough architectural discipline to let the prototype grow into the game you actually want.
