# Simulation Threading Design Notes

Use this guide when changing tile update passes, buffer ownership, chunk scheduling, or publish behavior.

## Intent
- The simulation is tile-first, statistical, and authoritative.
- Player input is queued and applied at simulation boundaries.
- Triple buffering keeps rendering from reading mutable simulation data.
- Chunking exists for cache behavior first and thread parallelism second.

## Current Shape
- `SimulationRuntime` owns three tile buffers: published/read/write roles rotate after publish.
- Worker passes use an enum task type and an atomic chunk cursor.
- The simulation thread participates in chunk work instead of only dispatching.
- Chunk layout is derived from L2 cache budget, map divisibility, and minimum job count.
- Published snapshots expose pointers to immutable tile, lot, road render/query, and tile-overlay data.

## Rules
- Do not copy whole tile buffers between ticks; use role swaps and write into the chosen write buffer.
- Keep chunk geometry rectangular and evenly dividing the map.
- Keep render-topology revisions separate from scalar tile updates.
- Mark render chunks dirty only when topology or render masks change.
- Transport route recalculation should read old loads and write new loads through worker-local deltas, then reduce after the batch. Path searches must not mutate shared load arrays directly.
- City parameters use dense old/new vectors and per-worker-shaped delta buffers so future lot batches can aggregate drivers and satisfactions without hot shared writes.
- Sample subsets of commuter/building routes over time so congestion feedback distributes statistically without relying on sequential determinism.
- Preserve `fastForward` behavior: simulation may outrun presentation unless disabled.

## Checks
- Build `x64 Release`.
- Watch simulation timing for neighbor, command, lot, local, publish, and write-buffer wait.
- Verify no renderer path can hold a mutable simulation buffer.
- Confirm command timing stays deterministic at simulation boundaries.
