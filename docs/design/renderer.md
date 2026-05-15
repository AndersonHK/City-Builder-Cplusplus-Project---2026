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
- Ground roads render in the tile pass from packed road-state bytes and generated road atlases. The ground-road upload path is `UpdateGroundRoadChunkTexture` (`City Builder/Renderer.cpp:1474`).
- Reusable tile overlays render from a published RGBA tile texture. Traffic capacity is the first overlay and uses visible-dirty chunk uploads.
- Elevated roads use separate per-chunk instance buffers and rebuild lazily for visible stale chunks. They consume the same resolved road glyph, lane graphic, and divider masks through `BuildRoadChunkInstances` (`City Builder/Renderer.cpp:1261`).
- Road placement ghost previews are transient renderer instances built from the active drag stroke and drawn with a blue alpha tint. They do not enter published road snapshots.
- Lots still render through one global placeholder-prism instance buffer keyed by lot revision.

## Road Render Data
- The road simulation owns lane topology, lane type masks, graphic masks, and path masks; rendering only consumes published road snapshots.
- Ground road channel 0 is the base glyph, channel 1 is the arrow glyph, channel 2 packs lane graphic masks, and channel 3 packs divider masks.
- Channel 2 low nibble is sidewalk edges and high nibble is crosswalk edges. These values are produced by `RoadRenderState` from lane-owned transport resolution.
- Crosswalk policy is not shader-owned. A crosswalk is a pedestrian lane graphic selected only when the lane overlaps a perpendicular car lane and both lane systems continue through the crossing.
- Elevated road instances carry the same base/arrow glyphs and packed lane-graphic/divider masks as instance attributes (`City Builder/Renderer.cpp:1147`).
- `Basic.shader` unpacks the lane graphic and divider masks in `applyRoadEdgeOverlays` (`City Builder/Basic.shader:102`).

## Rules
- Calculate visible chunks before upload work, then upload only visible stale chunks.
- Track freshness per chunk. A hidden stale chunk must remain stale and upload on the first frame it becomes visible.
- Keep static geometry separate from dynamic scalar masks so future elevation/terrain work can replace the geometry path without reintroducing full-map uploads.
- Keep shader sampling UV-compatible with full-map textures unless a future renderer migration changes the handoff contract explicitly.
- Add renderer metrics when adding new upload paths.
- Keep ground and elevated road rendering fed by the same resolved road cell contract; do not fork road-template semantics in the renderer.
- Keep road ghost previews presentation-only. They may reuse road templates and glyph helpers, but committed topology and validation must stay in the simulation/transport command path.
- Draw overlays after roads and lots with depth disabled/restored so the tint remains presentation, not terrain truth.
- Future overlays should publish the same RGBA tile payload and chunk revisions instead of adding one-off shader paths.

## Checks
- Build `x64 Release`.
- Compare the status line at `32`, `64`, `128`, `256`, and `512` visible-tile zoom.
- Verify `tileStateChunks`, `tileStateTiles`, and `tileStateBytes` scale with visible chunks.
- Pan after road or lot edits to confirm deferred chunks update before drawing.
- Verify local-street crosswalk graphics appear only where pedestrian and car lanes both continue through the crossing, and elevated highways render without pedestrian lane graphics by default.
- Drag local streets and elevated highways before release to confirm the alpha-tinted ghost follows the intended L-shaped stroke and disappears after commit.
- Toggle `T` to verify the traffic capacity overlay appears above roads/lots and starts green at zero load.

## Related Guides
- `docs/design/transport-network.md` owns road template placement, overlap validation, and resolved road-cell meaning.
- `docs/design/simulation-threading.md` owns snapshot publication rules.
