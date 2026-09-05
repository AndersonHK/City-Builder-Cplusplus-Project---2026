# Road Lane Cell Refactor Handout

Snapshot: 2026-05-17

This handout captures the planned simplification for road construction, pathing, and graphics. It is historical design context now; use `docs/design/transport-network.md` and `docs/design/renderer.md` for the current implementation contract.

## Goal

Road graphics and road gameplay should be produced from the same lane data.

If a crosswalk, sidewalk, median, road marker, or intersection graphic is visible, the lane that emitted it must also be the lane that contributes the corresponding pathing or non-pathing graphic state. No hidden pedestrian continuity. No visual-only crosswalks. No topology-only sidewalks.

## Current Signal

Automatic rotated `SandboxCases` were disabled for now because the test rotation harness geometrically rotates final footprints, while production road strokes anchor footprints from the drag command. This created false failures for simple two-tile roads.

With rotation disabled, the meaningful failures are:

- `four_lane_corner rot0` corrected expectations.
- `mixed_width_crossing rot0 sidewalk_directions`.

The corrected `four_lane_corner` is especially important: `active_car_axes` already matches the constructor output, but resolved topology, car directions, sidewalk directions, median directions, and materials do not. That means the broad authored footprint is acceptable, but the constructor/resolver is interpreting the overlapping wide L-corner wedge incorrectly.

The suspected failure point is `Road.cpp` around `CollapsePathPlacementsToTiles`: perpendicular same-type placements are merged into one tile blob, and their axes, sidewalk edges, divider masks, and travel masks are unioned. This destroys lane identity before graphics/pathing are resolved.

## Core Class Model

Do not escape this conceptual model by turning it into unrelated structs or by making car a special permanent assumption. Car is simply the only primary commuter mode currently implemented.

```cpp
class RoadLaneCell {
    Lane primary;    // required, currently car-like lanes
    Lane secondary;  // optional, currently pedestrian or median
};

class Lane {
    CommuterMode mode;          // car, pedestrian, none
    int capacity;               // ignored/0 for median/non-commuter graphics
    std::uint8_t directionMask; // N/E/S/W local movement or graphic direction
    bool centerSide;            // true = prefers center side of primary, false = outer side

    RoadGraphic parallelGraphic;
    RoadGraphic crossingGraphic;

    std::uint8_t centerMask() const;
};
```

Important constraints:

- `RoadLaneCell.primary` is required.
- `RoadLaneCell.secondary` is optional.
- There is no pedestrian-only road tile.
- There is no sidewalk plus median cohabitation in one `RoadLaneCell`.
- Two-lane roads should not have true median lanes. Their lane markers are graphical only.
- Four-lane roads may have true median secondary lanes.
- A tile may contain multiple `RoadLaneCell`s, but complexity comes from multiple cells, not one over-merged lane.

## Lane Shape Rules

Each lane is resolved from a 4-bit cardinal direction mask.

Valid single-lane cases:

- one bit: cap/dead-end
- two opposite bits: straight
- two adjacent bits: corner
- three bits: T / branch

Avoid using a four-direction `+` lane shape. A four-way intersection should be composed from multiple local `RoadLaneCell`s. The final legacy packed `ResolvedRoadCell` may still present cross-like aggregate data if the renderer needs it, but no individual lane should be resolved as a `N|E|S|W` blob.

## Center Rule

`Lane::centerMask()` is the small geometry brain.

The center is derived from the primary lane, not from the secondary lane. Secondary lanes position themselves relative to the primary lane's center:

- median secondary lanes prefer the center side
- pedestrian secondary lanes prefer the outer side

The function should be table-driven, not guessed with broad aggregate tile rules. It should transform the primary lane's `directionMask` into the local center side for rendering and secondary-lane ordering.

For straight directed movement, center is perpendicular to the primary lane direction.

For corners, center combines the center side of each participating leg.

For T shapes, center combines the center sides of the participating directed legs.

This rule is what lets one lane decide whether an attached secondary should hug the outside edge, draw toward the center, or cross the primary.

## Graphics And Pathing Rule

Cost and rendering should use the same simple loop:

```cpp
for each tile:
    setup all RoadLaneCell requirements

    for each RoadLaneCell:
        resolve primary.centerMask()
        determine secondary order from secondary.centerSide

        for each lane in primary and optional secondary:
            for each direction in lane.directionMask:
                add pathing if lane.mode has commuters
                render graphics from the same lane and direction
```

Primary lane:

- `parallelGraphic` is the ordinary lane graphic, such as road body with normal lane markers.
- `crossingGraphic` is used when the primary moves into more than one direction, such as an intersection/corner road piece without normal through markers.
- Marker logic should be graphical only.

Secondary pedestrian lane:

- If it travels along the primary lane's outer side, draw sidewalk using `parallelGraphic`.
- If it travels across the primary lane, draw crosswalk using `crossingGraphic`.
- In both cases, pedestrian pathing comes from the same `directionMask` that emitted the graphic.

Secondary median lane:

- It has `mode = none`, no commuter pathing, and `centerSide = true`.
- It renders median graphics on the primary lane's center side.

## Four-Way Intersection Interpretation

A four-way intersection is rendered as four intersection pieces with sidewalks on the outer corners.

Crosswalks arise from pedestrian secondary lanes that are perpendicular to their primary lanes. They are not special standalone intersection decorations.

For example, if a primary lane is traveling south-to-north and the attached pedestrian lane travels east through that tile, that pedestrian secondary lane is a crosswalk across the primary lane. It also contributes pedestrian pathing in that same direction.

In a four-way corner tile, a primary lane may come from the south and continue east/north. The pedestrian secondary lane hugs the outer corner and propagates along the outer edges according to the primary-derived center. This reproduces the quarter-circle sidewalk corner behavior without inventing a whole-tile `+` lane.

## Implementation Targets

1. Add an internal `RoadLaneCell` / `Lane` representation near road construction/resolution.
2. Preserve authored lane identity through construction.
3. Replace or bypass `CollapsePathPlacementsToTiles` where it collapses perpendicular same-type lanes into a single tile/type blob.
4. Keep same-axis overlap/replay validation strict.
5. Allow perpendicular coexistence as multiple lane cells.
6. Resolve each lane cell independently into:
   - primary direction mask
   - primary center mask
   - optional secondary direction mask
   - pathing contributions
   - render graphics
7. Aggregate into existing public `ResolvedRoadCell`, packed ground-road channels, and cost-map cells only after lane-cell resolution.
8. Keep public `TransportNetwork` APIs and renderer channel layout unchanged unless absolutely necessary.

## Invariants

- No graphic exists without a lane that emits it.
- No lane pathing exists without the same lane's graphic state being derivable.
- No sidewalk graphic unless a pedestrian secondary lane routes there.
- No crosswalk graphic unless a pedestrian secondary lane routes there and crosses its primary lane.
- No median graphic unless a median secondary lane exists.
- No sidewalk plus median inside the same `RoadLaneCell`.
- No pedestrian-only road cells.
- No four-direction `+` mask on one lane cell.
- Tile-level complexity comes from multiple simple lane cells.

## Suggested Work Order

1. Read the corrected `SandboxCases`, especially `four_lane_corner.txt` and `mixed_width_crossing.txt`.
2. Inspect `Road.cpp` around lane placement construction and `CollapsePathPlacementsToTiles`.
3. Introduce the lane-cell representation internally without changing public save/render APIs.
4. Convert the constructor output into lane cells before any tile-level aggregation.
5. Implement `Lane::centerMask()` as a small explicit table.
6. Make cost-map emission consume lane cells.
7. Make render packing consume the same lane cells.
8. Run `TransportNetworkTests.vcxproj` in `x64 Release`.
9. Keep `SandboxCases` expectations unchanged unless the user explicitly revises them.

## Expected Payoff

The four-lane corner should stop resolving the wide L overlap as fake T/cross blobs. The broad footprint can remain, but individual lanes keep their own center/outer relationship.

The mixed-width crossing should become easier to reason about because crosswalks emerge only where pedestrian secondary lanes actually cross primary lanes, rather than where a tile aggregate happens to contain perpendicular road material.
