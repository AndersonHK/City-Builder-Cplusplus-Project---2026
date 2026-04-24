# Transport Network Design Notes

Use this guide when changing road placement, topology resolution, road render data, or transport layers.

## Intent
- Roads live outside `Tile` so scalar simulation data stays compact.
- Ground and elevated roads have distinct render update paths.
- Road rendering should express topology without making transport objects the simulation source of truth.

## Current Shape
- `TransportNetwork` owns authored build cells, resolved road cells, packed ground-road render bytes, and per-layer chunk revisions.
- Ground roads render as tile overlays from packed base/arrow glyph ids.
- Elevated roads render as separate chunked instance buffers.
- Road strokes are two axis-aligned legs with lane-intent bits.
- Dirty topology resolves only the changed tiles and their neighbors.

## Rules
- Keep ground/elevated chunk revisions split.
- Bump only chunks touched by resolved road topology changes.
- Reject ground-road placement on occupied lot tiles.
- Preserve packed render-state compatibility with `Basic.shader` and road atlas generation.
- Track upload freshness per chunk in the renderer so hidden stale chunks update when visible.

## Checks
- Place local streets and elevated highways, then pan away and back.
- Verify corners, tees, crosses, dead ends, and arrows resolve correctly.
- Confirm ground roads block lots but elevated roads do not use ground occupancy.
