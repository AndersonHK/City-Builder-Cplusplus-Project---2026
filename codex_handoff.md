# Codex handoff memory

Snapshot: 2026-04-21
Workspace: C:\Users\imper\Documents\GitHub\City-Builder-Cplusplus-Project - 2026

## Recommended next-chat posture
- Start from the refactored architecture, not from the original monolithic prototype.
- Assume the important current arc is:
  - chunked tile simulation
  - command-queue input
  - triple-buffer render handoff
  - renderer extracted into its own module

## Current architecture checkpoint
- `SimulationRuntime` is the simulation owner and should stay the place where passes, chunks, buffers, and publishing rules live.
- `Renderer` owns GLFW/OpenGL draw setup and should not own game-state truth.
- `AppController` owns tool selection, camera movement, zoom, and command submission.
- `ChunkConfig` is the explicit place to reason about cache-derived chunk sizing.

## Current priorities after this pass
- verify and tune chunk sizing behavior on the actual machine
- continue cleaning up project/build assumptions around local dependency paths
- keep pushing severe correctness bugs out of the runtime before growing feature scope
- begin the next renderer phase:
  - real camera projection
  - chunked instance rendering
  - dirty chunk rebuilds
  - chunk frustum culling
- profile the simulation passes before more speculative optimization work
- look for low-hanging simulation wins in:
  - mutex-heavy chunk dispatch
  - published snapshot copying
  - write-buffer wait behavior
  - master-thread/control-flow overhead
- treat AVX2 as an experiment path to revisit after profiling, using intrinsics rather than inline assembly on MSVC x64

## Guardrails
- Keep `x64 Release` as the practical target.
- Do not collapse the new seams back into `Source.cpp`.
- Preserve the doctrine that rendering is presentation and the tile simulation remains the source of truth.
