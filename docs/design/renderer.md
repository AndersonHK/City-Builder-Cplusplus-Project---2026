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
- Reusable tile overlays render from RGBA tile textures. Zoning and traffic capacity both use visible-dirty chunk uploads from published state; land value reuses the same overlay texture/draw path and packs visible chunks from the published tile snapshot using the current city-wide min/max land-value range.
- Queried lot and road commutes render morning-only coalesced route arrows above roads, lots, and tile overlays; car arrows are green and pedestrian arrows are pink.
- Elevated roads use separate per-chunk instance buffers and rebuild lazily for visible stale chunks. They consume the same resolved road glyph, lane graphic, and divider masks through `BuildRoadChunkInstances`.
- Road placement ghost previews are transient renderer instances built from the active drag stroke and drawn with a blue alpha tint when valid or a red tint when invalid. They do not enter published road snapshots.
- Lot placement ghost previews are transient renderer instances built from XML-backed, rotation-aware lot candidate geometry and drawn with a green alpha tint when valid or a red tint when invalid. They do not enter published lot snapshots.
- Bulldoze area previews are transient renderer instances built from the active drag rectangle. The selected tiles draw a red world-space overlay and intersecting buildings draw through a red-tinted lot pass; the preview does not enter published snapshots, and the committed command may destroy buildings or roads.
- Residential and industrial zoning previews are produced by the XML-backed RCI tool planner. Plain area mode reuses the transient tint overlay; lot modes draw parcel-style ghost overlays with boundary lines, and lots+roads also feeds alpha-tinted two-tile local road ghost instances.
- Committed zoning draws from the published `Tile::zoningType` texture overlay. Empty RCI zoning lots publish separately as parcel rectangles so the renderer can draw persistent lot-boundary overlays without using the building `Lot` path; runtime area zoning may create those rectangles after commit even though its preview was a simple tint.
- Lots still render through one global placeholder-prism instance buffer keyed by lot revision.
- In-game windows and tool menus render as screen-space UI quads after world rendering. `InGameWindow` supplies window/text layout, `UiLayout` supplies menu/button layout, and `BuildWindowQuads` / `RendererBuildUiMenuQuads` turn them into dynamic `UiQuadInstanceData`.
- The startup and foreground save/load screens use the same screen-space UI quad path through `RendererAppendLoadingScreenQuads`, with the loading bar positioned about three-quarters down the screen. `GameSession` owns the current label/progress and calls the renderer presenter for blocking foreground work; renderer-owned region preview refresh uses the same loading quad builder while stale preview textures are regenerated.
- A minimal bootstrap loading bar is drawn immediately after the OpenGL context is ready, before heavier renderer resource creation, so startup does not sit on a black window.
- The HUD is also screen-space UI quads: city mode draws the configured numeric simulation date at top left and active-city population at top right, while region mode draws the summed region population at top right.
- Region mode draws a filtered UI subset: the top-left exit button plus whichever centered modal dialog is open. City mode draws the date-speed widget, side tools, menu toggle, and centered exit modal.
- The city date widget shares the UI quad path for its speed buttons. Button icons are drawn as renderer-side bitmap quad patterns from the XML `icon` attribute.
- The current text renderer decodes UTF-8 and emits clipped 5x7 bitmap glyph quads. Unsupported glyphs draw as `?`.
- Region mode draws city preview textures with the same angled camera settings as city mode.
- City previews are rendered through the normal city draw passes with a top-down orthographic camera, then uploaded to renderer-owned region preview textures.
- Preview state loading/generation may happen on background futures, but GL preview rendering and texture upload stay on the render thread.
- Region preview textures stay resident when entering city mode so F3 return can draw cached previews and refresh only stale city previews, normally the city that was just open.
- `GameSession::renderStateRevision` fences city load/enter transitions; when it changes, all city tile, road, lot, overlay, and route upload caches must be treated as stale before the next draw.
- Startup fullscreen/windowed size comes from `AppConfig`, but the actual fullscreen toggle stays in `RendererCallbacks` because it owns GLFW monitor/window mutation before input reaches `AppController`.

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
- Draw the shared loading screen while `GameSession::isLoading()` is true. Also draw it while region preview textures are stale before drawing the region grid. Fenced city loads still invalidate city render caches only after the load finishes, so world rendering must wait until the loading stage is complete.
- Add renderer metrics when adding new upload paths.
- Keep ground and elevated road rendering fed by the same resolved road cell contract; do not fork road-template semantics in the renderer.
- Keep road ghost previews presentation-only. They may reuse road templates and glyph helpers, but committed topology and validation must stay in the simulation/transport command path.
- Keep lot ghost previews presentation-only. They may reuse XML-backed lot candidate geometry, but committed placement validation must stay in the simulation command path.
- Keep bulldoze previews presentation-only. They may reuse published lot render instances for tinting, but committed destruction must stay in queued simulation commands and must not clear zoning or empty parcels.
- Keep zoning and unzone previews presentation-only. Committed zoning/clearing must stay in queued simulation commands, published tile snapshots, and published RCI parcel snapshots.
- Draw overlays after roads and lots with depth disabled/restored so the tint remains presentation, not terrain truth.
- Draw query route arrows after tile overlays with depth disabled/restored so selected morning commute paths remain inspectable.
- Draw in-game windows last with a screen-space orthographic projection, depth disabled, and no simulation ownership.
- Keep HUD values read-only from published snapshots or region metadata; do not query mutable simulation state directly from the renderer.
- Keep date formatting presentation-only. The city simulation tick comes from snapshots; `AppConfig` only selects how the renderer formats that tick.
- Keep game-speed buttons as controller intent. The renderer only draws icon buttons and active states; `SimulationRuntime` owns tick pacing.
- Future overlays should publish the same RGBA tile payload and chunk revisions instead of adding one-off shader paths.
- Snapshot-derived overlays that do not need simulation-owned derived state may pack into the existing overlay texture on the renderer side, but should keep the same visible-chunk freshness behavior.
- Keep city preview capture top-down orthographic so preview orientation remains stable before the region camera projects it.

## Checks
- Build `x64 Release`.
- Build and run `RendererTests.vcxproj` after touching renderer CPU packing, UTF-8 text, or UI quad generation.
- Toggle `fullscreen` and the preferred windowed dimensions in `Data/config.ini` before launch to verify startup and `Alt+Enter` restore behavior.
- Compare the status line at `32`, `64`, `128`, `256`, `512`, `1024`, and `2048` visible-tile zoom.
- Verify `tileStateChunks`, `tileStateTiles`, and `tileStateBytes` scale with visible chunks.
- Pan after road or lot edits to confirm deferred chunks update before drawing.
- Verify local-street crosswalk graphics appear only where pedestrian and car lanes both continue through the crossing, and elevated highways render without pedestrian lane graphics by default.
- Drag local streets and elevated highways before release to confirm the alpha-tinted ghost follows the intended L-shaped stroke and disappears after commit.
- Hover each lot placement tool over valid terrain to confirm the alpha-tinted lot ghost matches the committed footprint and disappears after placement.
- Drag `B` across lots and empty/road tiles to confirm selected tiles overlay red, selected buildings tint red, roads/buildings are removed on release, and zoning/empty parcels remain.
- Drag residential and industrial zoning from the side menu across vacant empty tiles to confirm default lots+roads shows parcel and two-tile road ghosts, `Shift` shows lots only, `Ctrl` shows a plain area tint, and committed green/yellow overlays plus parcel boundaries persist after release. Repeat beside an existing road to confirm plain area zoning creates road-facing parcel boundaries.
- Drag unzone from the side menu across zoned tiles and empty RCI parcels to confirm the gray preview appears and committed zoning tint/parcel boundaries disappear without bulldozing buildings, roads, or zoning beneath live lots.
- Toggle the bottom-left tool menu and confirm hidden menu buttons no longer draw or capture clicks.
- Rotate lot placement with `,` and `.` while hovering to confirm the ghost footprint rotates before commit.
- Toggle `T` to verify the traffic capacity overlay appears above roads/lots and starts green at zero load.
- Toggle `L` to verify the land value overlay appears above roads/lots and maps low values red, middle values yellow, and high values green with normal overlay alpha.
- Query lots and roads with `A` to verify the in-game window draws above world content, hugs populated fields, summarizes morning road commuters, and disappears when the query selection has no lot, road, or RCI zoning.
- Use the date widget speed buttons to verify paused holds the date, play advances one day per second, fast advances at render lockstep, and fast-forward is uncapped.
- Launch the game and confirm the first visible frame is the startup loading background with a horizontally centered progress bar near the lower quarter before the region appears.
- Enter a city, reload with `F2`, save with `F1`, and return to region with `F3` to confirm the same loading bar appears around foreground disk/runtime/preview work.
- Press `Esc` and verify the centered game menu and save-before-exit dialog render above world/UI content.
- In region mode, click `Exit` at top left and verify it opens the same save-before-exit dialog; after returning from a dirty city, double-click another city and verify the save-before-leaving-city dialog renders above the region previews.
- Temporarily remove `Data/UI/lot_query.xml` from the output folder and verify the fallback query window still appears.

## Related Guides
- `docs/design/transport-network.md` owns road template placement, overlap validation, and resolved road-cell meaning.
- `docs/design/simulation-threading.md` owns snapshot publication rules.
- `docs/design/window-system.md` owns XML-backed windows, text fields, and UI layout rules.
