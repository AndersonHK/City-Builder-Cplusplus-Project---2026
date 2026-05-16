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
- Transport route recalculation should preserve valid existing routes and force recalculation only when a route, source, or destination becomes invalid. Routine congestion rebalancing should use a deterministic rolling queue, currently about 1 percent of source lots per tick, so all source lots are visited over roughly 100 ticks without random repeats.
- A future parallel route assignment pass should read old loads and write new loads through worker-local deltas, then reduce after the batch. Path searches must not mutate shared load arrays directly.
- City parameters use dense old/new vectors and per-worker-shaped delta buffers so future lot batches can aggregate drivers and satisfactions without hot shared writes.
- The tick order is queued player commands, lot construction advancement, lot effects/city-parameter reduction, commute assignment, then RCI construction. This lets constructor demand use the latest completed buildings while newly placed construction does not affect parameters until a later tick after its timer finishes.
- The RCI constructor attempts residential and industrial parcels separately, up to the XML-configured attempt count per type. Under-construction RCI capacity reserves the demand budget immediately so repeated ticks do not keep filling demand that is already being built.
- City population is a reducer over the resident wealth city parameters (`$`, `$$`, and `$$$`), then copied into the published snapshot with the simulation tick. Buildings update population by changing their parameter contributions, not through a separate population counter path.
- The simulation tick is one in-game day. `SimulationDate` converts tick offsets from the configured start date defines, which default to January 1, 1900, and formats them through `SimulationDateSettings`.
- Sample subsets of commuter/building routes over time by queue/cursor, not random choice, so stale buildings are revisited predictably while congestion feedback still distributes statistically.
- Preserve `fastForward` behavior: simulation may outrun presentation unless disabled.

## Checks
- Build `x64 Release`.
- Watch simulation timing for neighbor, command, lot, local, publish, and write-buffer wait.
- Verify no renderer path can hold a mutable simulation buffer.
- Confirm command timing stays deterministic at simulation boundaries.
