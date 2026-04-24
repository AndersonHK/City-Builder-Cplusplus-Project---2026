# Renderer Design Notes

Use this guide when changing `Renderer.cpp`, `Basic.shader`, or render-facing snapshot data.

## Intent
- Rendering is presentation only. Simulation data remains authoritative in `SimulationRuntime`.
- The renderer consumes immutable published snapshots and never reads mutable simulation buffers.
- GPU work is acceptable; CPU packing, single-thread bandwidth, and RAM-to-VRAM uploads should stay measured and bounded.
- Window-mode concerns such as fullscreen toggles belong in the GLFW callback layer, not in simulation input commands.
- The current visual style is a staging layer for future richer 3D, not the final art direction.

## Current Shape
- Tiles draw from persistent per-chunk static instance buffers containing world origin and map UV.
- Tile scalar color comes from a persistent full-map `GL_RG16_SNORM` texture updated only for visible stale chunks.
- Lot occupancy lift comes from a persistent full-map `GL_R8` mask texture updated only for visible stale chunks.
- Ground roads render in the tile pass from packed road-state bytes and generated road atlases.
- Elevated roads use separate per-chunk instance buffers and rebuild lazily for visible stale chunks.
- Lots still render through one global placeholder-prism instance buffer keyed by lot revision.

## Rules
- Calculate visible chunks before upload work, then upload only visible stale chunks.
- Track freshness per chunk. A hidden stale chunk must remain stale and upload on the first frame it becomes visible.
- Keep static geometry separate from dynamic scalar masks so future elevation/terrain work can replace the geometry path without reintroducing full-map uploads.
- Keep shader sampling UV-compatible with full-map textures unless a future renderer migration changes the handoff contract explicitly.
- Add renderer metrics when adding new upload paths.

## Checks
- Build `x64 Release`.
- Compare the status line at `32`, `64`, `128`, `256`, and `512` visible-tile zoom.
- Verify `tileStateChunks`, `tileStateTiles`, and `tileStateBytes` scale with visible chunks.
- Pan after road or lot edits to confirm deferred chunks update before drawing.
