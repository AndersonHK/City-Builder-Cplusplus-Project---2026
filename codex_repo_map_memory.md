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
- `City Builder/Renderer.h`
- `City Builder/Renderer.cpp`
  - GLFW window/context ownership
  - OpenGL draw loop
  - shader loading and render-facing callbacks
  - current weakness: still CPU-builds visible tile geometry each frame instead of consuming chunk-local instance data
- `City Builder/AppController.h`
- `City Builder/AppController.cpp`
  - input/tool mode logic
  - camera/zoom state
  - queued player actions
- `City Builder/ChunkConfig.h`
- `City Builder/ChunkConfig.cpp`
  - L2 detection
  - cache budget selection
  - rectangular chunk sizing
- `City Builder/Tile.h`
  - canonical tile data model for the current milestone
- `City Builder/LotModule.h`
- `City Builder/LotModule.cpp`
- `City Builder/Lot.h`
- `City Builder/Lot.cpp`
  - building footprint/effect primitives
- `City Builder/ShaderProgram.h`
- `City Builder/ShaderProgram.cpp`
  - shader parsing/compilation/linking
- `City Builder/Basic.shader`
  - canonical runtime shader file copied beside the executable

## Build/project facts
- Primary target is `x64 Release`.
- The shader file is intended to ship beside the built `.exe`.
- Missing legacy `Dependencies`, `Linker`, and `vendor` references from the old project layout should not remain load-bearing assumptions forever; the refactor should keep the project internally coherent even if external library paths are machine-local.

## Runtime map
- Simulation owns authoritative world state.
- Rendering consumes published snapshots only.
- Input queues commands instead of mutating live simulation buffers directly.
- Triple buffering is the sim/render contract.

## Current migration doctrine
- Keep the tile-object model for now.
- Use contiguous storage and chunk-based passes to improve cache behavior without jumping to full structure-of-arrays immediately.
- Add comments only where they preserve future reasoning about cache sizing, swap rules, command timing, and render/sim ownership.
- The next renderer seam should be:
  - camera/view/projection math
  - per-chunk render data
  - instance buffers
  - dirty chunk rebuild rules
