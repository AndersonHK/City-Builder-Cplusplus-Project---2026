# Codex handoff memory

Snapshot: 2026-04-21
Workspace: C:\Users\imper\Documents\GitHub\City-Builder-Cplusplus-Project - 2026

## Recommended next-chat posture
- Start from the refactored architecture, not from the original monolithic prototype.
- Assume the important current arc is:
  - chunked tile simulation
  - command-queue input
  - triple-buffer render handoff
  - world-space renderer with chunked tile instancing

## Current architecture checkpoint
- `SimulationRuntime` is still the simulation owner and the place where passes, chunks, buffers, publish rules, lot snapshots, and chunk revision tracking live.
- `Renderer` owns GLFW/OpenGL setup, camera math, world-space picking, streamed tile-state texture updates, chunk instance buffers, lot instance buffers, and frustum culling.
- `AppController` owns tool selection, pan/zoom intent, hovered-tile state, and command submission.
- `ChunkConfig` is still the explicit place to reason about cache-derived chunk sizing.

## Current renderer checkpoint
- The old CPU-built visible-tile clip-space quad path is gone.
- Tiles now render as persistent per-chunk world-space instances.
- Tile scalar debug shading now streams through a tile-state texture each published generation.
- Lots render through a separate world-space placeholder prism path and are not chunk-owned yet.
- Mouse picking now raycasts from the perspective camera onto the ground plane.
- Arrow-key panning was corrected after the renderer migration so movement now matches the camera-facing directions.
- Holding the left mouse button in `Q` pollution mode now continuously paints again.

## Current runtime checkpoint
- Published snapshots are now pointer-based for tiles, lots, and per-chunk render revisions.
- Published lot render data is rebuilt only when the lot set changes.
- Worker chunk dispatch no longer copies a hot-path `std::function`; it uses an enum-driven task path plus an atomic chunk cursor.
- The simulation thread now participates in chunk work instead of only dispatching and waiting.
- Write-buffer selection no longer uses the old 1 ms sleep polling path; it waits on the render condition variable.
- Per-pass timing is available for:
  - neighbor pass
  - command apply
  - lot effects
  - local pass
  - publish path
  - write-buffer wait
  - renderer cull/upload/draw

## Current priorities after this pass
- verify the camera feel, chunk culling behavior, and perspective picking under more real play sessions
- decide the next renderer refactor:
  - chunk-own lots too, or
  - move tiles from flat quads to simple raised terrain/lot solids, or
  - split renderer support code into smaller files now that `Renderer.cpp` is carrying a lot
- continue cleaning up project/build assumptions around local dependency paths
- keep profiling the simulation before speculative SIMD work
- look for remaining low-hanging runtime wins in:
  - lot-effects iteration cost
  - tile-state texture upload cost
  - any remaining master-thread/control-flow overhead
  - future hot/cold tile layout opportunities
- treat AVX2 as an experiment path to revisit after profiling, using intrinsics rather than inline assembly on MSVC x64

## Build note
- `x64 Release` is still the practical target.
- In this shell environment, MSBuild needs the duplicate process `PATH` entry cleared first:
  - `[System.Environment]::SetEnvironmentVariable('PATH', $null, 'Process')`
- A stale running `City Builder.exe` can block relinking; stop it before rebuild if `LNK1104` appears on the output executable.

## Guardrails
- Do not collapse the seams back into `Source.cpp`.
- Preserve the doctrine that rendering is presentation and the tile simulation remains the source of truth.
- Keep comments limited to load-bearing architectural seams.
