# Renderer Design Notes

Use this guide when changing `Renderer.cpp`, `Basic.shader`, or render-facing snapshot data.

## Intent
- Rendering is presentation only. Simulation data remains authoritative in `SimulationRuntime`.
- The renderer consumes immutable published snapshots and never reads mutable simulation buffers.
- GPU work is acceptable; CPU packing, single-thread bandwidth, and RAM-to-VRAM uploads should stay measured and bounded.
- Window-mode concerns such as fullscreen toggles belong in the GLFW callback layer, not in simulation input commands.
- The current visual style is a staging layer for future richer 3D, not the final art direction.
- Camera setup should stay projection/direction agnostic; city interaction and region mode use the shared angled view settings, while city preview capture uses a top-down orthographic view.

## Current Shape
- Tiles draw from persistent per-chunk static instance buffers containing world origin and map UV.
- Tile scalar color comes from a persistent full-map `GL_RG16_SNORM` texture updated only for visible stale chunks.
- Lot occupancy lift comes from a persistent full-map `GL_R8` mask texture updated only for visible stale chunks.
- Ground roads render in the tile pass from packed road-state bytes and generated road atlases. The ground-road upload path is `UpdateGroundRoadChunkTexture`.
- Reusable tile overlays render from a published RGBA tile texture. Traffic capacity is the first overlay and uses visible-dirty chunk uploads.
- Queried lot commutes render as coalesced route arrows above roads, lots, and tile overlays; car arrows are green and pedestrian arrows are pink.
- Elevated roads use separate per-chunk instance buffers and rebuild lazily for visible stale chunks. They consume the same resolved road glyph, lane graphic, and divider masks through `BuildRoadChunkInstances`.
- Road placement ghost previews are transient renderer instances built from the active drag stroke and drawn with a blue alpha tint when valid or a red tint when invalid. They do not enter published road snapshots.
- Lot placement ghost previews are transient renderer instances built from XML-backed, rotation-aware lot candidate geometry and drawn with a green alpha tint when valid or a red tint when invalid. They do not enter published lot snapshots.
- Lots still render through one global placeholder-prism instance buffer keyed by lot revision.
- In-game windows render as screen-space UI quads after world rendering. `InGameWindow` supplies window/text layout, and `BuildWindowQuads` turns the active query window into dynamic `UiQuadInstanceData`.
- The current text renderer decodes UTF-8 and emits clipped 5x7 bitmap glyph quads. Unsupported glyphs draw as `?`.
- Region mode draws city preview textures with the same angled camera settings as city mode.
- City previews are rendered through the normal city draw passes with a top-down orthographic camera, then uploaded to renderer-owned region preview textures.
- Preview state loading/generation may happen on background futures, but GL preview rendering and texture upload stay on the render thread.
- Region preview textures are destroyed when entering city mode so city rendering does not keep the region preview set resident.
- `GameSession::renderStateRevision` fences city load/enter transitions; when it changes, all city tile, road, lot, overlay, and route upload caches must be treated as stale before the next draw.

## Road Render Data
- The road simulation owns lane topology, lane type masks, graphic masks, and path masks; rendering only consumes published road snapshots.
- Ground road channel 0 is the base glyph, channel 1 is the arrow glyph, channel 2 packs lane graphic masks, and channel 3 packs divider masks.
- Channel 2 low nibble is sidewalk edges and high nibble is crosswalk edges. These values are produced by `RoadRenderState` from lane-owned transport resolution.
- Crosswalk policy is not shader-owned. A crosswalk is a pedestrian lane graphic selected only when the lane overlaps a perpendicular car lane and both lane systems continue through the crossing.
- `F11` toggles road debug graphics. The renderer switches between marked and clean base-road atlases, and `Basic.shader` hides only arrow glyphs tagged with the debug bit; road surfaces, turn-lane arrows, sidewalks, crosswalks, and lane dividers still render.
- Elevated road instances carry the same base/arrow glyphs and packed lane-graphic/divider masks as instance attributes.
- `Basic.shader` unpacks the lane graphic and divider masks in `applyRoadEdgeOverlays`.

## Rules
- Calculate visible chunks before upload work, then upload only visible stale chunks.
- Track freshness per chunk. A hidden stale chunk must remain stale and upload on the first frame it becomes visible.
- Keep static geometry separate from dynamic scalar masks so future elevation/terrain work can replace the geometry path without reintroducing full-map uploads.
- Keep shader sampling UV-compatible with full-map textures unless a future renderer migration changes the handoff contract explicitly.
- Do not draw while `GameSession::isLoading()` is true; the previous completed frame should remain visible until the fenced load stage finishes.
- Add renderer metrics when adding new upload paths.
- Keep ground and elevated road rendering fed by the same resolved road cell contract; do not fork road-template semantics in the renderer.
- Keep road ghost previews presentation-only. They may reuse road templates and glyph helpers, but committed topology and validation must stay in the simulation/transport command path.
- Keep lot ghost previews presentation-only. They may reuse XML-backed lot candidate geometry, but committed placement validation must stay in the simulation command path.
- Draw overlays after roads and lots with depth disabled/restored so the tint remains presentation, not terrain truth.
- Draw query route arrows after tile overlays with depth disabled/restored so selected commute paths remain inspectable.
- Draw in-game windows last with a screen-space orthographic projection, depth disabled, and no simulation ownership.
- Future overlays should publish the same RGBA tile payload and chunk revisions instead of adding one-off shader paths.
- Keep city preview capture top-down orthographic so preview orientation remains stable before the region camera projects it.

## Checks
- Build `x64 Release`.
- Build and run `RendererTests.vcxproj` after touching renderer CPU packing, UTF-8 text, or UI quad generation.
- Compare the status line at `32`, `64`, `128`, `256`, `512`, `1024`, and `2048` visible-tile zoom.
- Verify `tileStateChunks`, `tileStateTiles`, and `tileStateBytes` scale with visible chunks.
- Pan after road or lot edits to confirm deferred chunks update before drawing.
- Verify local-street crosswalk graphics appear only where pedestrian and car lanes both continue through the crossing, and elevated highways render without pedestrian lane graphics by default.
- Drag local streets and elevated highways before release to confirm the alpha-tinted ghost follows the intended L-shaped stroke and disappears after commit.
- Hover each lot placement tool over valid terrain to confirm the alpha-tinted lot ghost matches the committed footprint and disappears after placement.
- Rotate lot placement with `,` and `.` while hovering to confirm the ghost footprint rotates before commit.
- Toggle `T` to verify the traffic capacity overlay appears above roads/lots and starts green at zero load.
- Query lots with `A` to verify the in-game window draws above world content, hugs populated fields, and disappears when the query selection has no lot.
- Temporarily remove `Data/UI/lot_query.xml` from the output folder and verify the fallback query window still appears.

## Related Guides
- `docs/design/transport-network.md` owns road template placement, overlap validation, and resolved road-cell meaning.
- `docs/design/simulation-threading.md` owns snapshot publication rules.
- `docs/design/window-system.md` owns XML-backed windows, text fields, and UI layout rules.
