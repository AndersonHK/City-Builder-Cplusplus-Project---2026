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
- Road strokes are two axis-aligned legs that carry a modular road template.
- Road templates are ordered cross-sections of elements such as sidewalks, lanes, dividers, and shoulders. Each element has min/preferred/max widths, and the layout pass chooses the nearest whole-tile footprint that satisfies those constraints.
- Lane elements resolve to aggregate per-tile travel masks for pathfinding, while visual junctions are derived from lane continuity rather than raw same-family adjacency. Parallel lanes beside each other are not intersections.
- Dirty topology resolves only the changed tiles and their neighbors.

## Rules
- Keep ground/elevated chunk revisions split.
- Bump only chunks touched by resolved road topology changes.
- Reject ground-road placement on occupied lot tiles.
- Preserve packed render-state compatibility with `Basic.shader` and road atlas generation.
- Track upload freshness per chunk in the renderer so hidden stale chunks update when visible.

## Checks
- Place local streets and elevated highways, then pan away and back.
- Verify lane order, corners, tees, crosses, dead ends, transition caps, and arrows resolve correctly.
- Confirm ground roads block lots but elevated roads do not use ground occupancy.
