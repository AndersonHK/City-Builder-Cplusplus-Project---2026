# Lot And Module Design Notes

Use this guide when changing `Lot`, lot placement, module expansion/removal, lot effects, or lot render snapshots.

## Intent
- Lots are statistical tile modifiers, not object-simulation truth.
- Modules let lots grow while preserving tile-based occupancy and effects.
- XML archetypes define reusable lot/module data; runtime lots hold placement state.
- Lot footprints can now be explicit and larger than their modules, so a lot can reserve yard/parcel tiles while only some tiles contain active modules.
- Lots can declare a front plus explicit per-tile commuter access by mode; placement rotation rotates both geometry and those access declarations.

## Current Shape
- `Lot` owns module placements, occupied offsets, occupied tile indices, aggregate effects, render height, and render color.
- Module placements store the placed footprint width/height separately from the source module archetype so non-square modules rotate correctly.
- `Lot` stores clockwise quarter-turn placement rotation so commute access can be evaluated in the same orientation as the placed footprint.
- `Lot` also caches module-backed city-parameter contributions and the latest accepted commute route segments for query visualization.
- `SimulationRuntime` owns the live lot list and the global lot-occupancy map.
- Lot render snapshots rebuild only when `lotsRevision_` changes.
- Tile lift for occupied lots is rendered through the renderer's lift mask texture rather than tile geometry rebuilds.
- Lot placement ghost previews reuse the XML-backed candidate geometry but stay renderer-only until the placement command commits.
- Factory lots use a 3x2 footprint containing a 2x2 warehouse plus an adjacent smokestack module; their access XML defines eight exterior connection points that accept cars and pedestrians. House lots use a garden on the front-left pedestrian access tile, a driveway on the front-right car access tile, and a centered 2x2 house module behind them.
- Lots still draw through a global placeholder-prism instance path.

## Rules
- Update occupancy and chunk revisions whenever a lot footprint changes.
- Preserve the invariant that a module add must attach to exactly one adjacent lot.
- Rebuild cached lot state after any module add/remove/rebase.
- Recompute city parameters and dirty commutes when lots or modules change.
- Keep commute access explicit in lot XML. Do not fall back to whole-footprint perimeter guessing for new commuter-producing lots.
- Keep lot previews presentation-only; they can share candidate footprint/render construction, but occupancy rejection and mutation belong to committed placement.
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
