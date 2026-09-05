#pragma once

// One simulation/render tile. Keep authoring in metres and convert only here.
// 12m two-tile street. User-approved rounded scale, allowing 3.35m lanes,
// 1.525m sidewalks plus curbs, gutters and visual spacing.
// See docs/design/metric-art-standard.md for the government references.
constexpr float kWorldTileMeters = 6.0f;
inline float MetersToTiles(float meters) { return meters / kWorldTileMeters; }
