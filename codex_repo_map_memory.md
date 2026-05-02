# Codex repository map and implementation memory

Snapshot: 2026-04-22
Workspace: C:\Users\imper\Documents\GitHub\City-Builder-Cplusplus-Project - 2026

## Top-level layout that matters now
- `City Builder/City Builder.vcxproj` - authoritative build entrypoint
- `City Builder/Source.cpp` - app bootstrap only
- `City Builder/SimulationRuntime.h`
- `City Builder/SimulationRuntime.cpp`
  - tile buffers
  - simulation loop
  - chunk worker pool
  - command queue
  - published render snapshots
  - per-chunk render revisions
  - transport network publication
  - per-pass timing
- `City Builder/TransportTypes.h`
- `City Builder/RoadLane.h`
- `City Builder/Road.h`
- `City Builder/TransportTile.h`
- `City Builder/RoadRenderState.h`
- `City Builder/TransportNetwork.h`
- `City Builder/TransportNetwork.cpp`
  - shared transport enums/snapshot masks
  - lane-owned road placement/state
  - per-tile lane merge validation and local topology resolution
  - lane graphic/crosswalk render-state derivation
  - split ground/elevated road chunk revisions
- `City Builder/TransportNetworkTests.vcxproj`
  - standalone non-graphics transport topology tests
- `City Builder/Renderer.h`
- `City Builder/Renderer.cpp`
  - GLFW window/context ownership
  - OpenGL draw loop
  - renderer-local math helpers
  - perspective camera state
  - ground-plane picking
  - static tile chunk instance buffer ownership
  - visible-chunk tile-state and tile-lift texture uploads
  - lazy visible-chunk ground-road texture upload
  - road atlas generation
  - lazy visible-chunk elevated-road buffer ownership
  - lot instance buffer ownership
  - chunk frustum culling
- `City Builder/AppController.h`
- `City Builder/AppController.cpp`
  - input/tool mode logic
  - pan/zoom state
  - hovered tile state
  - queued player actions
  - continuous pollution brush handling
- `City Builder/ChunkConfig.h`
- `City Builder/ChunkConfig.cpp`
  - L2 detection
  - cache budget selection
  - rectangular chunk sizing
- `City Builder/Tile.h`
  - canonical tile data model for the current milestone
- `City Builder/LotModule.h`
- `City Builder/Lot.h`
- `City Builder/Lot.cpp`
  - building footprint/effect primitives
  - render-facing placeholder prism data
- `City Builder/AssetLoader.h`
- `City Builder/AssetLoader.cpp`
  - strict XML asset loading for module and lot archetypes
- `City Builder/ShaderProgram.h`
- `City Builder/ShaderProgram.cpp`
  - shader parsing/compilation/linking
- `City Builder/Basic.shader`
  - canonical runtime shader file copied beside the executable
  - matrix-driven world-space instancing shader
- `City Builder/Data/Lots/*.xml`
- `City Builder/Data/Modules/*.xml`
  - runtime lot/module archetype data copied beside the executable
- `docs/design/*.md`
  - focused design guides for renderer, simulation threading, lots, XML assets, and transport network work

## Build/project facts
- Primary target is `x64 Release`.
- The shader file is intended to ship beside the built `.exe`.
- `Release|x64` now enables `/MP`.
- `.vcxproj.user`, debug logs, and build outputs were cleaned out of source control and should stay untracked.
- This shell environment may need the duplicate process `PATH` entry cleared before MSBuild runs cleanly.
- Missing legacy `Dependencies`, `Linker`, and `vendor` references from the old project layout should not remain load-bearing assumptions forever; the refactor should keep the project internally coherent even if external library paths are machine-local.

## Runtime map
- Simulation owns authoritative world state.
- Rendering consumes published snapshots only.
- Input queues commands instead of mutating live simulation buffers directly.
- Triple buffering is the sim/render contract.
- Published snapshots now expose immutable pointers to:
  - tiles
  - lots
  - per-chunk render revisions
  - resolved road cells
  - packed ground-road render state
  - split ground/elevated road chunk revisions

## Current renderer map
- Tiles:
  - built into static per-chunk world-space instance caches
  - colored from compact tile-state texture uploads for visible stale chunks
  - lifted from a small lot-occupancy mask texture for visible stale chunks
- Roads:
  - ground roads render as a tile overlay from packed road-state bytes plus atlas lookups
  - ground and elevated road uploads are deferred while dirty chunks are hidden
  - elevated roads rebuild only when visible chunks have changed revisions
- Lots:
  - rebuilt only when the lot revision changes
  - rendered as separate world-space placeholder prisms
- Input:
  - mouse hit testing uses renderer-driven raycasts
  - arrow-key pan semantics now match the current fixed camera heading
  - `Alt+Enter` toggles fullscreen on the primary monitor
  - holding left mouse in `Q` mode continuously paints pollution again
  - mouse wheel supports `512 / 256 / 128 / 64 / 32` visible-tile steps
  - `R` drag-places ground streets and `H` drag-places elevated highways
  - `T` and `Y` still expose live module add/remove testing for the XML-backed lot system

## Current migration doctrine
- Keep the tile-object model for now.
- Use contiguous storage and chunk-based passes to improve cache behavior without jumping to full structure-of-arrays immediately.
- Add comments only where they preserve future reasoning about cache sizing, swap rules, command timing, and render/sim ownership.
- When changing architecture, update the nearest `docs/design/*.md` guide, keep `README.md` as the navigable index, and cite important code symbols/line references where practical so future sessions can find the source of truth quickly.
- The next likely renderer seam is one of:
  - split `Renderer.cpp` into smaller renderer support units
  - move lots into chunk ownership
  - replace flat tile quads with richer staged terrain/solid presentation
