# Codex repository map and implementation memory

Snapshot: 2026-04-21
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
  - per-pass timing
- `City Builder/Renderer.h`
- `City Builder/Renderer.cpp`
  - GLFW window/context ownership
  - OpenGL draw loop
  - renderer-local math helpers
  - perspective camera state
  - ground-plane picking
  - chunk instance buffer ownership
  - lot instance buffer ownership
  - tile-state texture upload
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

## Current renderer map
- Tiles:
  - built into per-chunk world-space instance caches
  - rebuilt only when a chunk revision changes
  - colored from a streamed tile-state texture each publish
- Lots:
  - rebuilt only when the lot revision changes
  - rendered as separate world-space placeholder prisms
- Input:
  - mouse hit testing uses renderer-driven raycasts
  - arrow-key pan semantics now match the current fixed camera heading
  - holding left mouse in `Q` mode continuously paints pollution again
  - `R`, `T`, and `Y` expose live module-add/remove testing for the XML-backed lot system

## Current migration doctrine
- Keep the tile-object model for now.
- Use contiguous storage and chunk-based passes to improve cache behavior without jumping to full structure-of-arrays immediately.
- Add comments only where they preserve future reasoning about cache sizing, swap rules, command timing, and render/sim ownership.
- The next likely renderer seam is one of:
  - split `Renderer.cpp` into smaller renderer support units
  - move lots into chunk ownership
  - replace flat tile quads with richer staged terrain/solid presentation
