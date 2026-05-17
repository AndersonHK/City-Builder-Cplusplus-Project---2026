# Simulation Threading Design Notes

Use this guide when changing tile update passes, buffer ownership, chunk scheduling, or publish behavior.

## Intent
- The simulation is tile-first, statistical, and authoritative.
- Player input is queued and applied at simulation boundaries.
- Triple buffering keeps rendering from reading mutable simulation data.
- Chunking exists for cache behavior first and thread parallelism second.

## Current Shape
- `SimulationRuntime` owns map dimensions from `RuntimeOptions` and three tile buffers: published/read/write roles rotate after publish. Normal gameplay defaults to 1024x1024; integration tests can request smaller maps.
- Worker passes use an enum task type and an atomic chunk cursor.
- The simulation thread participates in chunk work instead of only dispatching.
- Chunk layout is derived from L2 cache budget, map divisibility, and minimum job count.
- Published snapshots expose pointers to immutable tile, lot, road render/query, and tile-overlay data.

## Rules
- Do not copy whole tile buffers between ticks; use role swaps and write into the chosen write buffer.
- Keep chunk geometry rectangular and evenly dividing the map.
- Keep render-topology revisions separate from scalar tile updates.
- Mark render chunks dirty only when topology or render masks change.
- Transport route recalculation should preserve valid existing round-trip routes and force recalculation only when a route, source, destination, or topology becomes invalid. Routine congestion rebalancing should use a deterministic rolling queue, currently about 1 percent of source lots per tick, so all source lots are visited over roughly 100 ticks without random repeats.
- Commute traffic has two parallel load states, `Morning` and `Evening`, over one stable base transport graph. Each tick computes only the active commute time and writes sparse touched-edge load deltas for that time of day.
- A future parallel route assignment pass should read committed loads and write new loads through worker-local deltas, then reduce after the batch. Path searches must not mutate shared load arrays directly.
- City parameters use dense old/new vectors and per-worker-shaped delta buffers so future lot batches can aggregate drivers and satisfactions without hot shared writes.
- The tick order is queued player commands, lot construction advancement, lot effects/city-parameter reduction, commute assignment, then RCI construction. This lets constructor demand use the latest completed buildings while newly placed construction does not affect parameters until a later tick after its timer finishes.
- Queued RCI area zoning fits empty parcel records immediately during the command pass, and city-save import recovers legacy zoned-but-unparcelled tiles before publishing. The parcel fitter uses the smart RCI tool dimensions, prefers candidates facing existing ground roads, and skips live lots, existing parcel records, and roads.
- The RCI constructor attempts residential and industrial parcels separately, up to the XML-configured attempt count per type. Under-construction RCI capacity reserves the demand budget immediately so repeated ticks do not keep filling demand that is already being built.
- City population is a reducer over the resident wealth city parameters (`$`, `$$`, and `$$$`), then copied into the published snapshot with the simulation tick. Buildings update population by changing their parameter contributions, not through a separate population counter path.
- Logical days are not the same as simulation ticks. `SimulationTime::ticksPerDay()` is currently `2`, `SimulationTime::tickToDay()` drives date display, and `SimulationTime::daysToTicks()` should convert authored day durations at load/setup boundaries. `SimulationDate` converts logical day offsets from the configured start date defines, which default to January 1, 1900, and formats them through `SimulationDateSettings`.
- Runtime game speed is explicit: paused waits without dated ticks but publishes command-only frames when player tools mutate state, play limits to one dated tick per second, fast waits for the renderer to consume each published generation, and fast-forward runs uncapped.
- All player tools must enqueue commands instead of mutating world state directly. The simulation thread drains those commands only inside the live-state gate, either during a dated tick or during a command-only frame while paused or waiting for the play-speed limiter.
- Renderer preview validation may read live placement state, but it must take the live-state gate and must not be called while a published snapshot is pinned. Published snapshots remain read-only render inputs; live validation and snapshot rendering are separate phases.
- Sample subsets of commuter/building routes over time by queue/cursor, not random choice, so stale buildings are revisited predictably while congestion feedback still distributes statistically.
- Preserve fast-forward behavior: simulation may outrun presentation only in the explicit fast-forward mode.
- Paused command-only frames publish direct command mutations first, then run commute/city-parameter recalculation and publish again if that work changes overlays or summaries. They must not advance construction, run the RCI constructor, or increment `simulationTick`.

## Checks
- Build `x64 Release`.
- Watch simulation timing for neighbor, command, lot, local, publish, and write-buffer wait.
- Verify no renderer path can hold a mutable simulation buffer.
- Confirm command timing stays deterministic at simulation boundaries.
- Confirm paused city loads do not advance `simulationTick` until a non-paused speed is selected.
- Confirm paused tool commands such as bulldoze publish visible state changes without advancing `simulationTick`.
