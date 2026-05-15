# Project Instructions

This repository is a modern C++ city-builder prototype inspired by SC2000 and SC4.

## Architecture
- Keep simulation authoritative, tile-first, and statistics-first.
- Treat rendering as presentation over immutable published snapshots.
- Preserve the command-queue input boundary and triple-buffer render handoff.
- Prefer cache-aware chunked simulation before deeper SIMD or data-layout rewrites.

## Design Guides
- See `../../docs/design/renderer.md` before changing renderer uploads, shaders, culling, or GPU payloads.
- See `../../docs/design/simulation-threading.md` before changing tile passes, worker scheduling, or buffer publication.
- See `../../docs/design/lots.md` before changing lot/module placement, occupancy, or effects.
- See `../../docs/design/xml-assets.md` before changing XML archetype loading or schema behavior.
- See `../../docs/design/transport-network.md` before changing road topology, layers, or render-state packing.
- See `../../docs/design/region-save.md` before changing region/city ownership, saves, loads, or previews.
- See `../../docs/design/window-system.md` before changing in-game windows, UI XML, query text, or UI draw behavior.

## Build
- Use `x64 Release` as the primary validation target.
- Use `msbuild` from `PATH` when available.
- Keep user-local Visual Studio files and generated build outputs out of source control.
