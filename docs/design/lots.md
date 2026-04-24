# Lot And Module Design Notes

Use this guide when changing `Lot`, lot placement, module expansion/removal, lot effects, or lot render snapshots.

## Intent
- Lots are statistical tile modifiers, not object-simulation truth.
- Modules let lots grow while preserving tile-based occupancy and effects.
- XML archetypes define reusable lot/module data; runtime lots hold placement state.

## Current Shape
- `Lot` owns module placements, occupied offsets, occupied tile indices, aggregate effects, render height, and render color.
- `SimulationRuntime` owns the live lot list and the global lot-occupancy map.
- Lot render snapshots rebuild only when `lotsRevision_` changes.
- Tile lift for occupied lots is rendered through the renderer's lift mask texture rather than tile geometry rebuilds.
- Lots still draw through a global placeholder-prism instance path.

## Rules
- Update occupancy and chunk revisions whenever a lot footprint changes.
- Preserve the invariant that a module add must attach to exactly one adjacent lot.
- Rebuild cached lot state after any module add/remove/rebase.
- Keep lot effects simple and tile-statistical until profiling proves a different model is needed.
- Do not make lots the source of tile truth; they apply effects into simulation passes.

## Checks
- Place and remove modules, then query affected tiles.
- Verify lots cannot overlap existing lots or ground roads.
- Pan away and back after lot edits to confirm renderer lift masks update lazily but correctly.
