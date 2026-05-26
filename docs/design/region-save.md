# Region And Save Design Notes

Use this guide when changing `GameSession`, `City`, `Region`, city save export/import, or region preview rendering.

## Intent
- The game boots into region mode before an editable city is active.
- A region owns coordinate-addressed cities; city names are display labels and are not unique ids.
- City simulation truth stays in `SimulationRuntime`; region saves store enough authored state to rebuild derived render/query data.
- Saves are alpha-only and versioned for fast rejection, not backward compatibility.

## Current Shape
- `GameSession` owns the current `GameMode`, the `Region`, one reusable `SimulationRuntime`, F1/F2/F3 behavior, the one active/cached city slot, explicit quit-city-to-region behavior, and the executable-local autoslot under `Data\Saves`.
- `GameSession` also owns foreground save/load status for blocking startup, city entry/reload, F3 return-to-region packing, discard, and autoslot save flows. The renderer registers a presenter and draws the shared loading screen whenever those foreground stages advance.
- `Region` owns `std::vector<std::unique_ptr<City>>`, looks cities up by `{regionX, regionY}`, and recomputes region parameter totals from each city's latest saved city parameters. Region population is derived from those summed resident wealth parameters.
- `region.bin` stores region/city metadata only; individual `city_X_Y.bin` files store full `CitySaveState` payloads.
- `City` stores name, unique region coordinates, map dimensions, camera metadata, parameter summaries, dirty state, and optionally one loaded `CitySaveState`. The simulation date belongs to the city through its saved simulation tick; regions do not own date state.
- Region preview data is loaded/generated on background futures and rendered through the normal city draw passes with a top-down orthographic camera; CPU preview pixels are not kept on `City` after upload. Clean saved/default previews are cached beside the save files as `city_X_Y.preview.bin`, keyed to the matching city save file metadata, so normal startup can upload cached preview pixels instead of re-importing and re-rendering each city. Renderer-owned region preview textures stay resident while a city is open, so returning to region refreshes only stale previews instead of rebuilding the full grid.
- `SimulationRuntime::exportCitySaveState` writes the current simulation tick, tiles, RCI zoning parcel rectangles, parcel front directions, redevelopment availability ticks, reconstructable lot/module placement state, lot construction state, city parameters, and authored transport tile lanes.
- `SimulationRuntime::importCitySaveState` rebuilds live lots, lot occupancy, transport derived state, recovers empty parcels for any saved zoned-but-unparcelled RCI tiles, and refreshes published renderer snapshots from saved authored state.
- `TransportNetwork` exports/imports authored `RoadLanePlacement` records, then resolves road render/cost data after load.

## Rules
- Keep `{regionX, regionY}` as the stable city address.
- Keep full city payloads out of region metadata; load or generate them on demand for enter-city, save, and preview rebuilds. Only the active city may keep a dirty full `CitySaveState` cached while the player is back in region mode.
- Stop the simulation thread before exporting or importing city state.
- Do not treat published renderer snapshots as save truth; regenerate them after load.
- Treat enter-city and F2 city reload as fenced loading stages: finish disk read, runtime import, and renderer cache invalidation before drawing the next city frame.
- Foreground saves and loads should enter a blocking loading stage in `GameSession`, update the shared status as disk/runtime steps advance, and finish the stage before returning control. Do not use that blocking stage for future background autosaves that copy state and write on a worker thread.
- When region selection would replace a dirty cached active city, ask whether to save first. `Yes` saves the autoslot and unloads the clean cache; `No` discards the cache by reloading that city from disk/default metadata, unloading the full cached payload, and invalidating the dirty preview before importing the selected city. Selecting the same cached city skips the warning and imports the cached in-memory state.
- Keep F3 as a region-view transition that packs the active city into the cached city slot for quick re-entry. The main-menu `Exit to Region` action is different: it asks whether to save, then unloads the active city and clears the runtime back to an empty city state before returning to region mode. `Yes` persists the autoslot first; `No` discards active city changes by reloading that city's metadata from disk/default.
- Keep region previews disposable and regenerable from city save state through renderer-owned top-down offscreen rendering.
- Only persist preview cache pixels for clean saved/default city state. Dirty active-city previews may be rendered for region mode, but must not overwrite the disk preview cache until the city has been saved.
- Push the active camera into the active city before F1/F3 exports, and restore the city camera after enter-city/F2 reload.
- Bump or invalidate renderer upload freshness when moving from region mode into city mode.
- Reset runtime game speed to paused whenever importing a city for play, including double-click entry from region mode and F2 reload while in city mode.
- Do not delete existing `Data\Saves` during build, launch, save, or load flows. Saves are still alpha-only and may be rejected when incompatible, but convenient one-time salvage is now preferred over wiping them.
- City save version 11 stores RCI parcel front direction and the current tile-authored transport lanes. It intentionally rejects older alpha city saves. There is still no general backward-compatibility layer; preserve files on disk, but prefer a save-version bump and regeneration over carrying stale migration code when the authored model changes.

## Checks
- Build `x64 Release`.
- Launch with no save and confirm a 3x3 region appears.
- Double-click a region preview and confirm city mode starts.
- Place lots/roads, press `F1`, press `F3`, and confirm the region preview updates.
- Press `F2` from city mode and confirm the autoslot reloads the same city without returning to region mode.
- Press `F3` and confirm the shared loading screen appears while the active city is packed and any stale region preview is refreshed, then double-clicking the same city re-enters the cached state.
- From the city escape menu, choose `Exit to Region`, answer `No`, and confirm the city returns to region mode with the active city unloaded. Double-clicking that same city should load from the last saved/default state rather than the discarded runtime cache.
- Press `F3`, then double-click the same dirty city and confirm it re-enters without the save-before-switching-city dialog.
- Press `F3`, choose not to save before switching to a different city, and confirm the old city's stale dirty preview is rebuilt from save/default data.
- Confirm city entry and city reload both resume at paused speed before advancing the date.
- Build and run `SaveLoadIntegrationTests.vcxproj`; it should save and reload a 32x32 sandbox city through a temp autoslot and compare the full exported game state, including RCI parcel front direction.
- Build and run `RciLotConstructionTests.vcxproj` when changing RCI save-adjacent parcel or constructor behavior.
- Rebuild Release x64 and confirm existing output saves remain in place.
