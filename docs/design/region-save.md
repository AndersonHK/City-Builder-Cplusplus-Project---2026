# Region And Save Design Notes

Use this guide when changing `GameSession`, `City`, `Region`, city save export/import, or region preview rendering.

## Intent
- The game boots into region mode before an editable city is active.
- A region owns coordinate-addressed cities; city names are display labels and are not unique ids.
- City simulation truth stays in `SimulationRuntime`; region saves store enough authored state to rebuild derived render/query data.
- Saves are alpha-only and versioned for fast rejection, not backward compatibility.

## Current Shape
- `GameSession` owns the current `GameMode`, the `Region`, one reusable `SimulationRuntime`, F1/F2/F3 behavior, and the executable-local autoslot at `Data\Saves\region.bin`.
- `Region` owns `std::vector<std::unique_ptr<City>>`, looks cities up by `{regionX, regionY}`, and recomputes region parameter totals from each city's latest saved city parameters.
- `City` stores name, unique region coordinates, map dimensions, `CitySaveState`, and a cached 4096x4096 RGBA preview rendered from saved city state through the normal city draw passes with a top-down orthographic camera.
- `SimulationRuntime::exportCitySaveState` writes tiles, reconstructable lot/module placement state, city parameters, and authored transport lanes.
- `SimulationRuntime::importCitySaveState` rebuilds live lots, lot occupancy, transport derived state, and published renderer snapshots from saved authored state.
- `TransportNetwork` exports/imports authored `RoadLanePlacement` records, then resolves road render/cost data after load.

## Rules
- Keep `{regionX, regionY}` as the stable city address.
- Stop the simulation thread before exporting or importing city state.
- Do not treat published renderer snapshots as save truth; regenerate them after load.
- Keep region previews disposable and regenerable from city save state through renderer-owned top-down offscreen rendering.
- Bump or invalidate renderer upload freshness when moving from region mode into city mode.
- It is acceptable for Release x64 rebuilds to delete `Data\Saves`; save compatibility is intentionally disposable during this alpha stage.

## Checks
- Build `x64 Release`.
- Launch with no save and confirm a 3x3 region appears.
- Double-click a region preview and confirm city mode starts.
- Place lots/roads, press `F1`, press `F3`, and confirm the region preview updates.
- Press `F2` from city mode and confirm the autoslot reloads the same city without returning to region mode.
- Rebuild Release x64 and confirm output saves are removed by the pre-build step.
