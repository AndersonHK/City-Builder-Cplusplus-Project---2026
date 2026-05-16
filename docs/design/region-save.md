# Region And Save Design Notes

Use this guide when changing `GameSession`, `City`, `Region`, city save export/import, or region preview rendering.

## Intent
- The game boots into region mode before an editable city is active.
- A region owns coordinate-addressed cities; city names are display labels and are not unique ids.
- City simulation truth stays in `SimulationRuntime`; region saves store enough authored state to rebuild derived render/query data.
- Saves are alpha-only and versioned for fast rejection, not backward compatibility.

## Current Shape
- `GameSession` owns the current `GameMode`, the `Region`, one reusable `SimulationRuntime`, F1/F2/F3 behavior, and the executable-local autoslot under `Data\Saves`.
- `Region` owns `std::vector<std::unique_ptr<City>>`, looks cities up by `{regionX, regionY}`, and recomputes region parameter totals from each city's latest saved city parameters. Region population is derived from those summed resident wealth parameters.
- `region.bin` stores region/city metadata only; individual `city_X_Y.bin` files store full `CitySaveState` payloads.
- `City` stores name, unique region coordinates, map dimensions, camera metadata, parameter summaries, dirty state, and optionally one loaded `CitySaveState`. The simulation date belongs to the city through its saved simulation tick; regions do not own date state.
- Region preview data is loaded/generated on background futures and rendered through the normal city draw passes with a top-down orthographic camera; CPU preview pixels are not kept on `City` after upload.
- `SimulationRuntime::exportCitySaveState` writes the current simulation tick, tiles, RCI zoning parcel rectangles, reconstructable lot/module placement state, city parameters, and authored transport lanes.
- `SimulationRuntime::importCitySaveState` rebuilds live lots, lot occupancy, transport derived state, and published renderer snapshots from saved authored state.
- `TransportNetwork` exports/imports authored `RoadLanePlacement` records, then resolves road render/cost data after load.

## Rules
- Keep `{regionX, regionY}` as the stable city address.
- Keep full city payloads out of region metadata; load or generate them on demand for enter-city, save, and preview rebuilds.
- Stop the simulation thread before exporting or importing city state.
- Do not treat published renderer snapshots as save truth; regenerate them after load.
- Treat enter-city and F2 city reload as fenced loading stages: finish disk read, runtime import, and renderer cache invalidation before drawing the next city frame.
- Keep region previews disposable and regenerable from city save state through renderer-owned top-down offscreen rendering.
- Push the active camera into the active city before F1/F3 exports, and restore the city camera after enter-city/F2 reload.
- Bump or invalidate renderer upload freshness when moving from region mode into city mode.
- Do not delete existing `Data\Saves` during build, launch, save, or load flows. Saves are still alpha-only and may be rejected when incompatible, but convenient one-time salvage is now preferred over wiping them.

## Checks
- Build `x64 Release`.
- Launch with no save and confirm a 3x3 region appears.
- Double-click a region preview and confirm city mode starts.
- Place lots/roads, press `F1`, press `F3`, and confirm the region preview updates.
- Press `F2` from city mode and confirm the autoslot reloads the same city without returning to region mode.
- Rebuild Release x64 and confirm existing output saves remain in place.
