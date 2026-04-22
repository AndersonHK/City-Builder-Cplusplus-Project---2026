# City-Builder-Cplusplus-Project - 2026

Modern C++ city-builder prototype aimed at an SC2000/SC4-style simulation core: tile-first, systems-driven, cache-aware, and staged toward richer 3D presentation without making rendering the source of truth.

## Current state
- `SimulationRuntime` owns authoritative world state, chunked simulation passes, triple buffering, published render snapshots, and timing instrumentation.
- `Renderer` owns the OpenGL presentation path:
  - constrained pitched perspective camera
  - world-space tile rendering
  - per-chunk persistent tile instance buffers
  - streamed tile-state debug shading texture
  - separate lot prism instancing
  - chunk frustum culling
  - ground-plane mouse picking
- `AppController` owns pan/zoom/tool intent and queues simulation commands.

## Controls
- Arrow keys: pan the camera-relative view
- Mouse wheel: zoom in and out
- Left mouse in `Q` mode: continuously paint pollution while held
- `Q`: pollution brush
- `W`: place smokestack lot
- `E`: place park lot
- `R`: add smokestack module to an adjacent lot footprint
- `T`: add park module to an adjacent lot footprint
- `Y`: remove the module under the hovered tile
- `A`: query hovered tile

## Build
Primary target: `x64 Release`

This shell environment may need the process `PATH` entry cleared before invoking MSBuild because duplicate `Path`/`PATH` variables can trip MSBuild on Windows:

```powershell
[System.Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
& 'D:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' 'City Builder/City Builder.vcxproj' /p:Configuration=Release /p:Platform=x64 /m
```

If link fails with `LNK1104` on `City Builder.exe`, stop any running copy of the game and rebuild.

## Architecture notes
- Simulation remains tile-statistical and authoritative.
- Rendering consumes published immutable snapshots only.
- Tile chunk render data rebuilds only when render-topology-relevant chunk revisions change.
- Dynamic scalar tile debug color still updates every publish through a texture upload so chunk instance data can stay stable.
- Lots are not chunk-owned yet; they still use a separate renderer path for now.
- Lot/module archetypes load from XML under `City Builder/Data`.

## Repository hygiene
- The active code lives under `City Builder/`; stale tracked build outputs and legacy unused helper files were removed.
- `LotModule` is header-only right now; there is no meaningful `LotModule.cpp`.
- User-local Visual Studio files such as `.vcxproj.user` should stay untracked.

## Near-term refactor candidates
- split the growing `Renderer.cpp` support code into smaller renderer units
- move lots into chunk-owned render data
- continue replacing placeholder geometry with more intentional staged 3D presentation
- profile the remaining runtime and renderer hot spots before any SIMD experiments
