# City-Builder-Cplusplus-Project - 2026

Modern C++ city-builder prototype aimed at an SC2000/SC4-style simulation core: tile-first, systems-driven, cache-aware, and staged toward richer 3D presentation without making rendering the source of truth.

## Current state
- `SimulationRuntime` owns authoritative world state, chunked simulation passes, triple buffering, published render snapshots, timing instrumentation, and a multi-layer transport network for roads.
- `Renderer` owns the OpenGL presentation path:
  - constrained pitched perspective camera
  - world-space tile rendering
  - per-chunk persistent tile instance buffers
  - visible-chunk tile-state debug shading texture updates
  - visible-chunk lot-lift mask texture updates
  - packed ground-road overlay texture updates in the tile pass
  - lazy visible-chunk elevated-road rendering for stacked highways
  - alpha-tinted ghost road preview while a road stroke is being dragged
  - separate lot prism instancing
  - chunk frustum culling
  - ground-plane mouse picking
- `AppController` owns pan/zoom/tool intent and queues simulation commands.

## Controls
- Arrow keys: pan the camera-relative view
- Mouse wheel: zoom in and out across `512 / 256 / 128 / 64 / 32` visible-tile steps
- `Alt+Enter`: enter or exit fullscreen mode
- Left mouse in `Q` mode: continuously paint pollution while held
- `Q`: pollution brush
- `W`: place smokestack lot
- `E`: place park lot
- `F`: place factory lot
- `G`: place house lot
- `R`: drag-place a ground local street with a ghost preview while dragging
- `H`: drag-place an elevated highway with a ghost preview while dragging
- `[` / `]`: decrease / increase road lane count for new road strokes
- `C`: toggle right-hand / left-hand road traffic side
- `O`: cycle road direction mode between two-way, one-way forward, and one-way reverse
- `T`: toggle the traffic capacity overlay
- `M`: add park module to an adjacent lot footprint
- `Y`: remove the module under the hovered tile
- `A`: query hovered tile; queried lots show their accepted commute route as green arrows

## Build
Primary target: `x64 Release`

This shell environment may need the process `PATH` entry cleared before invoking MSBuild because duplicate `Path`/`PATH` variables can trip MSBuild on Windows:

```powershell
[System.Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' 'City Builder/City Builder.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
```

If link fails with `LNK1104` on `City Builder.exe`, stop any running copy of the game and rebuild.

Transport topology has a standalone non-graphics test target:

```powershell
[System.Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' 'City Builder/TransportNetworkTests.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
& 'City Builder/x64/Release/TransportNetworkTests.exe'
```

## Architecture notes
- Simulation remains tile-statistical and authoritative.
- Rendering consumes published immutable snapshots only.
- Tile chunk geometry is static after renderer setup for the current flat-tile presentation.
- Dynamic scalar tile debug color uploads only visible stale chunks through a compact texture.
- Lot occupancy lift uploads only visible stale chunks through a small mask texture.
- Roads live in a separate transport layer with their own published cell snapshot, directional pathfinding cost map, packed ground-road render state, traffic overlay state, and split ground/elevated chunk revisions so `Tile` stays compact for the scalar simulation passes.
- Ground-road and elevated-road uploads are dirty visible-chunk only, and stale hidden chunks stay deferred until visible.
- Traffic overlays use the same visible-dirty chunk upload pattern and draw above roads and lots as a presentation tint.
- Road drag previews are renderer-only transient instances tinted with alpha; committed road topology still arrives through published snapshots.
- The renderer timing print breaks out tile-state packing/upload bytes, lift uploads, ground-road uploads, elevated-road uploads, and draw costs.
- Lots are not chunk-owned yet; they still use a separate renderer path for now.
- Lot/module archetypes load from XML under `City Builder/Data`.
- Factory/house XML assets are the first driver-backed lots: houses emit low-wealth resident demand, factories emit dirty-industry jobs, and the commute pass assigns accepted low-wealth commutes through the directional transport cost map into road load.

## Design guides
- `docs/design/transport-network.md` - lane-owned road placement, directional cost maps, pathfinding, crosswalk graphic rules, packed road state, and layer revisions. Main code anchors: `TransportTypes.h`, `TransportCostMap.h`, `RoadLane.h`, `Road.h`, `TransportTile.h`, `RoadRenderState.h`, and `TransportNetwork.h`.
- `docs/design/renderer.md` - renderer upload, culling, texture, shader decisions, packed lane graphic masks, shared ground/elevated road render data, and road ghost previews. Main code anchors: `BuildRoadPreviewInstances` (`City Builder/Renderer.cpp:1192`), `BuildRoadChunkInstances` (`City Builder/Renderer.cpp:1261`), `UpdateGroundRoadChunkTexture` (`City Builder/Renderer.cpp:1474`), and `applyRoadEdgeOverlays` (`City Builder/Basic.shader:105`).
- `docs/design/simulation-threading.md` - tile passes, triple buffering, chunk worker rules, and published snapshot ownership.
- `docs/design/lots.md` - lot/module placement, occupancy, effects, and render snapshots.
- `docs/design/xml-assets.md` - strict XML archetype loading and validation.

## Repository hygiene
- The active code lives under `City Builder/`; stale tracked build outputs and legacy unused helper files were removed.
- `LotModule` is header-only right now; there is no meaningful `LotModule.cpp`.
- User-local Visual Studio files such as `.vcxproj.user` should stay untracked.

## Near-term refactor candidates
- split the growing `Renderer.cpp` support code into smaller renderer units
- move lots into chunk-owned render data
- continue replacing placeholder geometry with more intentional staged 3D presentation
- profile the remaining runtime and renderer hot spots before any SIMD experiments
