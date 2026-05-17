# City-Builder-Cplusplus-Project - 2026

Modern C++ city-builder prototype aimed at an SC2000/SC4-style simulation core: tile-first, systems-driven, cache-aware, and staged toward richer 3D presentation without making rendering the source of truth.

## Current state
- `SimulationRuntime` owns authoritative world state, chunked simulation passes, triple buffering, published render snapshots, timing instrumentation, and a multi-layer transport network for roads.
- `GameSession` owns the boot mode, the default 3x3 `Region`, the reusable active `SimulationRuntime`, and alpha autoslot save/load.
- `Region` owns coordinate-addressed `City` records and 4096x4096 top-down preview pixels rendered from saved city state through the normal city draw passes.
- `Renderer` owns the OpenGL presentation path:
  - startup and blocking foreground save/load screens with a shared loading bar
  - region preview grid rendering and double-click city entry
  - constrained pitched perspective camera
  - world-space tile rendering
  - per-chunk persistent tile instance buffers
  - visible-chunk tile-state debug shading texture updates
  - visible-chunk lot-lift mask texture updates
  - packed ground-road overlay texture updates in the tile pass
  - lazy visible-chunk elevated-road rendering for stacked highways
  - alpha-tinted ghost road preview while a road stroke is being dragged
  - alpha-tinted ghost lot preview while a lot placement tool is active
  - red bulldoze area overlay and selected-building tint while dragging
  - persistent residential/industrial zoning tint overlays plus matching drag previews
  - separate lot prism instancing
  - top-left simulation date display and top-right city population counter sourced from published simulation snapshots
  - top-left date widget speed controls for paused, play, fast, and fast-forward simulation pacing
  - top-right region population counter sourced from summed city parameter metadata
  - screen-space in-game window, menu, button quads, and bitmap text for query inspection and tool selection
  - chunk frustum culling
  - ground-plane mouse picking
- `AppConfig` owns startup app preferences loaded from `Data/config.ini`: window mode/size, hotkeys, date display format, and query-console debug output.
- `AppController` owns pan/zoom/tool intent and queues simulation commands. Its keyboard input uses GLFW key codes from `AppConfig`.

## Controls
Default keyboard bindings, startup fullscreen mode, preferred windowed resolution, date format, and query-console debug output are loaded from `Data/config.ini` beside the executable. The list below reflects the checked-in defaults.

- Arrow keys: pan the camera-relative view
- Mouse wheel: zoom in and out across `2048 / 1024 / 512 / 256 / 128 / 64 / 32` visible-tile steps
- `Alt+Enter`: enter or exit fullscreen mode
- Left mouse in `Q` mode: continuously paint pollution while held
- `Q`: pollution brush
- `W`: place smokestack lot with a ghost preview
- `E`: place park lot with a ghost preview
- `F`: place factory lot with a ghost preview
- `G`: place house lot with a ghost preview
- `,` / `.`: rotate the active lot placement counter-clockwise / clockwise
- `R`: drag-place a ground local street with a ghost preview while dragging
- `H`: drag-place an elevated highway with a ghost preview while dragging
- `[` / `]`: decrease / increase road lane count for new road strokes
- `C`: toggle right-hand / left-hand road traffic side
- `O`: cycle road direction mode between two-way, one-way forward, and one-way reverse
- `T`: toggle the traffic capacity overlay
- `F11`: toggle road debug graphics; off by default
- `M`: add park module to an adjacent lot footprint
- `Y`: remove the module under the hovered tile
- `B`: drag a rectangular bulldoze area; selected tiles overlay red and selected buildings tint red before release. Bulldozing removes buildings and roads, but zoning and empty parcels remain for the unzone tool.
- `A`: query hovered tile; queried lots show an in-game detail window plus accepted commute routes as green car arrows and pink pedestrian arrows, road tiles summarize commuters passing through their lanes, and empty RCI lots show their zoning name
- Bottom-left `Tools` button: show or hide the left-side tool menu
- Left-side menu: select bulldoze, road, query, residential zoning, industrial zoning, or unzone
- Residential / Industrial zoning buttons: drag a rectangle to zone vacant tiles or existing RCI lots. By default the RCI tool previews and commits lots plus surrounding local roads; hold `Shift` for lots only, or hold `Ctrl` for a plain area fill. New parcel lots require an unoccupied footprint, and plain area zoning still tries to fit empty parcels onto any unparcelled RCI tiles, preferring road-facing parcels when roads are already available.
- Unzone button: drag a rectangle to clear only the selected tile zoning and rebuild affected empty RCI parcel boundaries without bulldozing buildings, roads, adjacent parcel tiles, or zoning beneath live lots.
- Region mode starts first; the top-left `Exit` button opens the same save-before-exit dialog as `Esc` -> `Exit`.
- Double-click a city preview to enter that city. If a dirty active city is cached from a previous city visit, the region view asks whether to save it before replacing it; choosing `No` discards that cached state, invalidates its dirty preview, and reloads from the city save. Double-clicking that same cached city returns to it directly from memory.
- City mode shows the current simulation date at top left and the active city's population at top right
- Date widget icon buttons set speed to paused, play (one tick per second), fast (render/simulation lockstep), or fast-forward (uncapped)
- `Esc`: open the centered game menu; `Exit` asks whether to save before closing the game
- Region mode shows total region population at top right
- `F1`: save the current region autoslot
- `F2`: load the region autoslot; in city mode, reload the current city in place
- `F3`: exit the active city back to region mode

## Build
Primary target: `x64 Release`

With MSBuild on `PATH`, build from the repository root with:

```powershell
msbuild 'City Builder/City Builder.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
```

If link fails with `LNK1104` on `City Builder.exe`, stop any running copy of the game and rebuild.

Transport topology has a standalone non-graphics test target:

```powershell
msbuild 'City Builder/TransportNetworkTests.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
& 'City Builder/x64/Release/TransportNetworkTests.exe'
```

Renderer CPU packing and UI quad generation also have a standalone non-graphics test target:

```powershell
msbuild 'City Builder/RendererTests.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
& 'City Builder/x64/Release/RendererTests.exe'
```

Save/load round-trip coverage has a standalone integration target. It boots a temporary 32x32 sandbox city, mutates runtime state, saves to a temp autoslot, reloads through a fresh session, compares the exported state, and cleans up the temp files:

```powershell
msbuild 'City Builder/SaveLoadIntegrationTests.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
& 'City Builder/x64/Release/SaveLoadIntegrationTests.exe'
```

## Architecture notes
- Simulation remains tile-statistical and authoritative.
- Rendering consumes published immutable snapshots only.
- Tile chunk geometry is static after renderer setup for the current flat-tile presentation.
- Dynamic scalar tile debug color uploads only visible stale chunks through a compact texture.
- Lot occupancy lift uploads only visible stale chunks through a small mask texture.
- Roads live in a separate transport layer with their own published cell snapshot, directional pathfinding cost map, packed ground-road render state, traffic overlay state, and split ground/elevated chunk revisions so `Tile` stays compact for the scalar simulation passes.
- Committed road edits rebuild pathing costs and traffic overlay pixels for affected dirty tiles only; full transport cost/overlay rebuilds are reserved for full city imports and whole-network resets.
- Ground-road and elevated-road uploads are dirty visible-chunk only, and stale hidden chunks stay deferred until visible.
- Traffic overlays use the same visible-dirty chunk upload pattern and draw above roads and lots as a presentation tint.
- Road debug graphics start disabled and can be toggled with `F11`; disabling them hides ordinary direction arrows and car-lane connection markers while preserving turn-lane arrows, lane dividers, crosswalks, and road surfaces.
- Road drag previews are renderer-only transient instances tinted with alpha; committed road topology still arrives through published snapshots.
- Lot placement previews are renderer-only transient instances built from the same XML-backed lot candidate geometry used by committed placement.
- Bulldoze previews are renderer-only transient area overlays; committed destruction still arrives through queued simulation commands and may remove buildings or roads, but does not clear zoning or empty parcels.
- Unzone previews are renderer-only transient area overlays; committed unzoning clears tile zoning and empty parcel records through a queued simulation command.
- Residential and industrial zoning are XML-backed RCI tools. They queue simulation commands that mark zoneable tiles, optionally create empty zoning-lot parcels with boundary overlays, and can lay planned two-tile local streets before zoning. Runtime area zoning and save import also fit empty zoning-lot parcel records onto unparcelled RCI tiles using the same RCI tool sizing rules, with a road-facing pass before the fallback block partitioner. The RCI constructor runs after city parameters and commutes are recalculated, tries up to the XML-configured parcel attempts for each RCI type, and only builds lots whose capacity fits the current demand budget plus the configured overbuild allowance.
- In-game windows load from XML under `City Builder/Data/UI`; the current query window uses optional text fields, margins, and content hugging.
- Tool menus and buttons also load from XML under `City Builder/Data/UI`; the current city tool menu lives in `city_tools.xml`.
- The renderer timing print breaks out tile-state packing/upload bytes, lift uploads, ground-road uploads, elevated-road uploads, and draw costs.
- Lots are not chunk-owned yet; they still use a separate renderer path for now.
- Lot/module archetypes load from XML under `City Builder/Data`; lot XML can declare a front, a constructor-facing `zoningType`, explicit mode-specific access tiles, or compact perimeter access. RCI tool definitions load from `City Builder/Data/RCI/rci_tools.xml`.
- Transport congestion speed curves load from `City Builder/Data/TransportNetwork/congestion.xml`.
- App defaults load from `City Builder/Data/config.ini`; the checked-in defaults start fullscreen, use a 2048x2048 preferred windowed size, keep query-value console spam disabled, and expose gameplay hotkeys as editable names.
- Factory/house XML assets are the first driver-backed lots: low-wealth residents satisfy low-wealth residential demand, low-wealth jobs drive that demand, low-wealth workers drive dirty-industry demand, and dirty-industry capacity satisfies dirty-industry demand. Commute assignment writes low-wealth commute and dirty-industry staffing satisfaction from accepted routes, preserves valid existing routes while forcing invalid source/destination routes, and rebalances a deterministic rolling 1 percent source-lot queue per tick. Commute paths can run up to 300 time units and include mode start costs, currently 2 time units for car starts and 0 for walking starts.
- Constructor-built RCI lots grow their module render height from 0 percent to full height over their XML `constructionTicks`. Construction lots reserve demand budget immediately, but their population/jobs, pollution, land value, and commute demand do not enter city parameters until construction completes. Bulldozed RCI buildings expose their former empty zoning lot again after a 30-day redevelopment grace period.
- Population is derived from the reduced resident wealth parameters (`$`, `$$`, and `$$$`) and published with snapshots rather than recounted from lots.
- Simulation dates are city-owned and derive from each city's saved tick counter, where one tick is one day, starting from `SIMULATION_START_YEAR` / `SIMULATION_START_MONTH` / `SIMULATION_START_DAY` in `SimulationDate.h` (default January 1, 1900). `SimulationDateSettings` defaults display to `YYYY/MM/DD` and supports `MM/DD/YYYY` and `DD/MM/YYYY` through `Data/config.ini`.
- City loads always start paused, whether entering from the region, switching cities, or reloading the autoslot in city mode. The runtime speed modes are paused, play at one tick per second, fast at render lockstep, and fast-forward uncapped.
- Startup region loading, foreground city entry/reload, foreground return-to-region preview refresh, and foreground autoslot saves use the same loading bar, positioned about three-quarters down the screen. Future background autosaves should avoid that blocking loading stage and report separately if they need passive status.
- Paused tool commands publish command-only frames, so building/road/zoning edits appear without advancing the simulation date.
- Region preview textures stay resident while a city is open so F3 return only needs to refresh stale city previews, normally the city that was just open.
- `RuntimeOptions` defaults city maps to 1024x1024 but can shrink the runtime for non-renderer integration tests such as the 32x32 save/load sandbox.

## Design guides
- `docs/design/transport-network.md` - lane-owned road placement, directional cost maps, pathfinding, crosswalk graphic rules, packed road state, and layer revisions. Main code anchors: `TransportTypes.h`, `TransportCostMap.h`, `RoadLane.h`, `Road.h`, `TransportTile.h`, `RoadRenderState.h`, and `TransportNetwork.h`.
- `docs/design/transport-routing-scalability-plan.md` - plan for persistent route scratch, sparse traffic load deltas, lazy route budgeting, later chunk-owned routing topology caches, and future destination-field reuse.
- `docs/design/app-config.md` - INI-backed startup preferences, hotkeys, date display settings, and debug console gates.
- `docs/design/renderer.md` - renderer upload, culling, texture, shader decisions, packed lane graphic masks, shared ground/elevated road render data, placement ghost previews, zoning overlays, and UI draw ordering. Main code anchors: `BuildLotInstance`, `BuildRoadPreviewInstances`, `BuildRoadChunkInstances`, `BuildWindowQuads`, `RendererBuildUiMenuQuads`, `UpdateGroundRoadChunkTexture`, and `applyRoadEdgeOverlays`.
- `docs/design/simulation-threading.md` - tile passes, triple buffering, chunk worker rules, and published snapshot ownership.
- `docs/design/lots.md` - lot/module placement, occupancy, effects, and render snapshots.
- `docs/design/xml-assets.md` - strict XML archetype loading and validation.
- `docs/design/region-save.md` - region/city ownership, autoslot save/load, previews, and alpha compatibility assumptions.
- `docs/design/window-system.md` - XML-backed in-game windows, text fields, query-window flow layout, menus, buttons, and renderer UI quads.

## Repository hygiene
- The active code lives under `City Builder/`; stale tracked build outputs and legacy unused helper files were removed.
- `LotModule` is header-only right now; there is no meaningful `LotModule.cpp`.
- User-local Visual Studio files such as `.vcxproj.user` should stay untracked.

## Near-term refactor candidates
- split the growing `Renderer.cpp` support code into smaller renderer units
- move lots into chunk-owned render data
- continue replacing placeholder geometry with more intentional staged 3D presentation
- profile the remaining runtime and renderer hot spots before any SIMD experiments
