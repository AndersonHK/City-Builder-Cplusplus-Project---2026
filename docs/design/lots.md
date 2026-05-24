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
- `Lot` also caches module-backed city-parameter contributions, accepted round-trip commute route records, and morning coalesced route segments for query visualization.
- `SimulationRuntime` owns the live lot list and the global lot-occupancy map.
- Lot render snapshots rebuild only when `lotsRevision_` changes.
- Tile lift for occupied lots is rendered through the renderer's lift mask texture rather than tile geometry rebuilds.
- Lot placement ghost previews reuse the XML-backed candidate geometry but stay renderer-only until the placement command commits.
- Factory lots use a 3x2 footprint containing a 2x2 warehouse plus an adjacent smokestack module; their access XML defines eight exterior connection points that accept cars and pedestrians. House lots use a garden on the front-left pedestrian access tile, a driveway on the front-right car access tile, and a centered 2x2 house module behind them.
- Residential RCI access is front-only, including empty residential parcels. Explicit XML access coordinates should line up with sensible entrance tiles such as driveways, paths, gardens, parking aprons, or building entries.
- Lots still draw through a global placeholder-prism instance path.
- RCI zoning lots start as separate empty parcel records. They have no modules, do not reserve building occupancy, and exist only to mark zoning boundaries over tiles until the constructor pass finds a matching RCI lot archetype and instantiates a real `Lot`.
- Plain RCI area zoning now creates those empty parcel records too. `SimulationRuntime` fits parcels onto unoccupied, unparcelled RCI tiles with the XML-backed smart RCI tool dimensions, first trying frontage runs beside existing ground roads and then partitioning remaining RCI blocks.
- Constructor-built RCI lots can be under construction. Their modules render as height-scaled growth from 0 percent to full height, but their city parameters, pollution, land-value effects, and commute demand are suppressed until construction completes. Authored construction durations should use logical `constructionDays` where possible and are stored as ticks after load.
- Destroyed RCI building lots are removed and replaced by their former empty zoning parcel after a 30-day logical grace period converted to ticks through `SimulationTime`. Bulldozing remains responsible for roads and building destruction; zoning and empty-parcel removal belong to the unzone tool.
- The RCI constructor now evaluates a connected same-RCI source block per attempt, capped to an 8x8 bounding rectangle. Sources are empty parcels plus completed live RCI buildings; under-construction buildings are not sources and remain blockers. Candidate growth rectangles must consume whole source rectangles, may merge adjacent parcels/buildings, and cannot leave gaps.
- RCI constructor lot archetypes are named templates tagged with zone `zoningType`, RCI type `rciType`, and `densityBand`. Residential and industrial now cover every 2x2 through 8x8 footprint, including rectangles, with at least two templates per density band and footprint.
- RCI lots should have one singular primary module, at most two. Secondary modules such as yards, gardens, paths, driveways, service yards, parking, and loading areas are the tool for lowering density and making larger lots believable.
- Larger low-density residential templates should reuse the same house, duplex, or rowhouse family with more yard and access space. They should not simply merge two detached houses or two trailers into one wider lot; those are better represented as two individual parcels.
- Residential density language is explicit: houses, townhouses, and rowhouses are low density; walkups, apartment blocks, and midrises are medium density; towers are high density. Rowhouse modules should never be wider than three project tiles.
- RCI growth is gated by `Data/RCI/rci_tools.xml`: each constructor-enabled zone declares a desirability threshold and max density-per-tile points interpolated from current population, while each RCI type declares demand/desirability identity and the zones it can grow in. Runtime then limits that citywide zone cap by a local land-value/intensity factor. Current desirability requires XML road access through the transport cost map, then averages the RCI type's desirability table over the candidate footprint.
- Capacity authoring should treat larger modules as usually denser, but not linearly free: a larger building module earns higher capacity because it spends less lot area on repeated access/decor, while the surrounding lot may intentionally spend yard, parking, loading, setback, or landscape tiles to moderate final density. This keeps hand-authored XML compatible with a future templated-lot generator: modules provide base capacity by footprint and role, lots apply coverage/access/decor rules, and the RCI density cap remains the final hard ceiling.

## Rules
- Update occupancy and chunk revisions whenever a lot footprint changes.
- Preserve the invariant that a module add must attach to exactly one adjacent lot.
- Rebuild cached lot state after any module add/remove/rebase.
- Recompute city parameters when lots or modules change, but do not count under-construction lots as active demand/capacity until their construction timer completes. Queue only the affected source/destination lots immediately and let the deterministic rolling commute queue rebalance the rest over time.
- Keep commute access declared in lot XML. Use explicit `<connection>` rows for residential lots and reserve `<perimeter>` shortcuts for lot types where every side truly has access.
- Keep lot previews presentation-only; they can share candidate footprint/render construction, but occupancy rejection and mutation belong to committed placement.
- Keep RCI parcel creation separate from building `Lot` placement. Only the constructor pass should consume empty parcels or completed RCI buildings and instantiate a building lot inside the selected candidate rectangle.
- Keep new empty RCI parcel records on unoccupied footprints. Plain area zoning may update existing RCI-lot tiles, but parcel creation over a live lot would create overlapping ownership for the constructor.
- When RCI tiles are zoned without explicit parcels, run the parcel fitter over unparcelled RCI tiles instead of waiting for demand. Existing empty parcels, live lots, and ground roads are blockers; road-adjacent candidate runs should be claimed before landlocked fallback blocks.
- Keep constructor growth bounded by XML demand, desirability, and density rules. Redevelopment/merge candidates should only replace existing sources when the candidate capacity exceeds the consumed built capacity plus the best standalone empty-parcel capacity.
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
- Query a house after a successful commute to verify morning car and pedestrian route arrows are published with mode colors.
- Pan away and back after lot edits to confirm renderer lift masks update lazily but correctly.
