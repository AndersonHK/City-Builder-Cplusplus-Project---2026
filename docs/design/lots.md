# Lot And Module Design Notes

Use this guide when changing `Lot`, lot placement, module expansion/removal, lot effects, or lot render snapshots.

## Intent
- Lots are statistical tile modifiers, not object-simulation truth.
- Modules let lots grow while preserving tile-based occupancy and effects.
- XML archetypes define reusable lot/module data; runtime lots hold placement state.
- Lot footprints can now be explicit and larger than their modules, so a lot can reserve yard/parcel tiles while only some tiles contain active modules.
- Lots can declare a front plus per-tile commuter access by mode; placement rotation rotates both geometry and those access declarations.

## Current Shape
- `Lot` owns module placements, occupied offsets, occupied tile indices, aggregate effects, render height, render color, and construction progress for constructor-built lots.
- Module placements store the placed footprint width/height separately from the source module archetype so non-square modules rotate correctly.
- `Lot` stores clockwise quarter-turn placement rotation so commute access can be evaluated in the same orientation as the placed footprint.
- `Lot` also caches module-backed city-parameter contributions, accepted commute route records, and coalesced route segments for query visualization.
- `SimulationRuntime` owns the live lot list and the global lot-occupancy map.
- Lot render snapshots rebuild only when `lotsRevision_` changes.
- Tile lift for occupied lots is rendered through the renderer's lift mask texture rather than tile geometry rebuilds.
- Lot placement ghost previews reuse the XML-backed candidate geometry but stay renderer-only until the placement command commits.
- Factory lots use a 3x2 footprint containing a 2x2 warehouse plus an adjacent smokestack module; their access XML defines eight exterior connection points that accept cars and pedestrians. House lots use a garden on the front-left pedestrian access tile, a driveway on the front-right car access tile, and a centered 2x2 house module behind them.
- Lots still draw through a global placeholder-prism instance path.
- RCI zoning lots start as separate empty parcel records. They have no modules, do not reserve building occupancy, and exist only to mark zoning boundaries over tiles until the constructor pass finds a matching RCI lot archetype and instantiates a real `Lot`.
- Plain RCI area zoning now creates those empty parcel records too. `SimulationRuntime` fits parcels onto unoccupied, unparcelled RCI tiles with the XML-backed smart RCI tool dimensions, first trying frontage runs beside existing ground roads and then partitioning remaining RCI blocks.
- Constructor-built RCI lots can be under construction. Their modules render as height-scaled growth from 0 percent to full height, but their city parameters, pollution, land-value effects, and commute demand are suppressed until construction completes.
- Destroyed RCI building lots are removed and replaced by their former empty zoning parcel after a 30-day grace period. Bulldozing remains responsible for roads and building destruction; zoning and empty-parcel removal belong to the unzone tool.
- RCI constructor lot archetypes are tagged with `zoningType` and currently cover residential and industrial 2- and 3-tile widths for depths 2 through 8.

## Rules
- Update occupancy and chunk revisions whenever a lot footprint changes.
- Preserve the invariant that a module add must attach to exactly one adjacent lot.
- Rebuild cached lot state after any module add/remove/rebase.
- Recompute city parameters when lots or modules change, but do not count under-construction lots as active demand/capacity until their construction timer completes. Queue only the affected source/destination lots immediately and let the deterministic rolling commute queue rebalance the rest over time.
- Keep commute access declared in lot XML, either as specific `<connection>` rows or as an intentional `<perimeter>` shortcut.
- Keep lot previews presentation-only; they can share candidate footprint/render construction, but occupancy rejection and mutation belong to committed placement.
- Keep RCI parcel creation separate from building `Lot` placement. Only the constructor pass should consume a zoning parcel and instantiate a building lot inside it.
- Keep new empty RCI parcel records on unoccupied footprints. Plain area zoning may update existing RCI-lot tiles, but parcel creation over a live lot would create overlapping ownership for the constructor.
- When RCI tiles are zoned without explicit parcels, run the parcel fitter over unparcelled RCI tiles instead of waiting for demand. Existing empty parcels, live lots, and ground roads are blockers; road-adjacent candidate runs should be claimed before landlocked fallback blocks.
- Keep bulldoze focused on building/module destruction and road removal. Removing zoning or empty parcels belongs to the unzone command path, not the bulldozer command path.
- Keep unzone from clearing zoning beneath live lots. If unzone touches an empty RCI parcel record, clear only the selected unoccupied tiles, then rebuild affected empty parcel records around the remaining zoned tiles.
- When rotating lots, rotate both module origin and placed module footprint dimensions. Do not render a rotated non-square module with its original width/height.
- Keep lot effects simple and tile-statistical until profiling proves a different model is needed.
- Do not make lots the source of tile truth; they apply effects into simulation passes.

## Checks
- Place and remove modules, then query affected tiles.
- Verify lots cannot overlap existing lots or ground roads.
- Hover each lot placement tool before clicking to confirm the ghost footprint matches the eventual committed lot.
- Rotate lot placement with `,` and `.` before clicking to verify rotated footprints still use the intended front/access side.
- Rotate factories before placement and confirm the smokestack remains beside the warehouse rather than intersecting it.
- Query a house after a successful commute to verify car and pedestrian route arrows are published with mode colors.
- Pan away and back after lot edits to confirm renderer lift masks update lazily but correctly.
