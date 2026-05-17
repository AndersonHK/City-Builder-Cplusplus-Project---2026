#include "RoadAtlas.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "RoadRenderState.h"

namespace {
struct Vec2 {
    float x;
    float y;

    Vec2()
        : x(0.0f),
          y(0.0f) {
    }

    Vec2(float xValue, float yValue)
        : x(xValue),
          y(yValue) {
    }
};

struct Rgba {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;
};

const Rgba kTransparent = {0u, 0u, 0u, 0u};
const Rgba kConcreteRoad = {118u, 118u, 112u, 255u};
const Rgba kAsphaltRoad = {2u, 2u, 2u, 255u};
const Rgba kSidewalk = {190u, 190u, 190u, 255u};
const Rgba kMedian = {160u, 160u, 160u, 255u};
const Rgba kCrosswalk = {232u, 227u, 199u, 255u};
const Rgba kWhiteMarking = {225u, 220u, 205u, 255u};
const Rgba kYellowMarking = {238u, 220u, 82u, 255u};
const int kRoadMaterialAtlasStride = 2048;
const int kSidewalkAtlasOffset = 64;
const int kSidewalkYellowDividerAtlasOffset = 256;
const int kSidewalkWhiteDividerAtlasOffset = 512;
const int kSidewalkCrosswalkAtlasOffset = 768;
const int kSidewalkMedianDividerAtlasOffset = 1024;

float Clamp01(float value) {
    return std::max(0.0f, std::min(value, 1.0f));
}

std::uint8_t BaseGlyphJunctionMask(RoadBaseGlyph baseGlyph) {
    switch (baseGlyph) {
        case RoadBaseGlyph::LocalDeadEndNorth:
        case RoadBaseGlyph::HighwayDeadEndNorth:
            return kRoadDirectionNorth;
        case RoadBaseGlyph::LocalDeadEndEast:
        case RoadBaseGlyph::HighwayDeadEndEast:
            return kRoadDirectionEast;
        case RoadBaseGlyph::LocalDeadEndSouth:
        case RoadBaseGlyph::HighwayDeadEndSouth:
            return kRoadDirectionSouth;
        case RoadBaseGlyph::LocalDeadEndWest:
        case RoadBaseGlyph::HighwayDeadEndWest:
            return kRoadDirectionWest;
        case RoadBaseGlyph::LocalStraightVertical:
        case RoadBaseGlyph::HighwayStraightVertical:
            return kRoadDirectionNorth | kRoadDirectionSouth;
        case RoadBaseGlyph::LocalStraightHorizontal:
        case RoadBaseGlyph::HighwayStraightHorizontal:
            return kRoadDirectionEast | kRoadDirectionWest;
        case RoadBaseGlyph::LocalCornerNorthEast:
        case RoadBaseGlyph::HighwayCornerNorthEast:
            return kRoadDirectionNorth | kRoadDirectionEast;
        case RoadBaseGlyph::LocalCornerSouthEast:
        case RoadBaseGlyph::HighwayCornerSouthEast:
            return kRoadDirectionSouth | kRoadDirectionEast;
        case RoadBaseGlyph::LocalCornerSouthWest:
        case RoadBaseGlyph::HighwayCornerSouthWest:
            return kRoadDirectionSouth | kRoadDirectionWest;
        case RoadBaseGlyph::LocalCornerNorthWest:
        case RoadBaseGlyph::HighwayCornerNorthWest:
            return kRoadDirectionNorth | kRoadDirectionWest;
        case RoadBaseGlyph::LocalTeeMissingNorth:
        case RoadBaseGlyph::HighwayTeeMissingNorth:
            return kRoadDirectionEast | kRoadDirectionSouth | kRoadDirectionWest;
        case RoadBaseGlyph::LocalTeeMissingEast:
        case RoadBaseGlyph::HighwayTeeMissingEast:
            return kRoadDirectionNorth | kRoadDirectionSouth | kRoadDirectionWest;
        case RoadBaseGlyph::LocalTeeMissingSouth:
        case RoadBaseGlyph::HighwayTeeMissingSouth:
            return kRoadDirectionNorth | kRoadDirectionEast | kRoadDirectionWest;
        case RoadBaseGlyph::LocalTeeMissingWest:
        case RoadBaseGlyph::HighwayTeeMissingWest:
            return kRoadDirectionNorth | kRoadDirectionEast | kRoadDirectionSouth;
        case RoadBaseGlyph::LocalCross:
        case RoadBaseGlyph::HighwayCross:
            return kRoadDirectionNorth | kRoadDirectionEast | kRoadDirectionSouth | kRoadDirectionWest;
        default:
            return 0;
    }
}

float DistanceToSegment(const Vec2& point, const Vec2& startPoint, const Vec2& endPoint) {
    const Vec2 pointOffset(point.x - startPoint.x, point.y - startPoint.y);
    const Vec2 lineOffset(endPoint.x - startPoint.x, endPoint.y - startPoint.y);
    const float lineLengthSquared = (lineOffset.x * lineOffset.x) + (lineOffset.y * lineOffset.y);
    const float projection = lineLengthSquared <= 0.000001f
        ? 0.0f
        : Clamp01(((pointOffset.x * lineOffset.x) + (pointOffset.y * lineOffset.y)) / lineLengthSquared);
    const Vec2 closestPoint(startPoint.x + (lineOffset.x * projection), startPoint.y + (lineOffset.y * projection));
    const float dx = point.x - closestPoint.x;
    const float dy = point.y - closestPoint.y;
    return std::sqrt((dx * dx) + (dy * dy));
}

float LineMask(const Vec2& point, const Vec2& startPoint, const Vec2& endPoint, float thickness) {
    const float distance = DistanceToSegment(point, startPoint, endPoint);
    return 1.0f - Clamp01((distance - thickness) / 0.02f);
}

float ArrowMask(const Vec2& point, const Vec2& direction) {
    const float length = std::sqrt((direction.x * direction.x) + (direction.y * direction.y));
    if (length <= 0.000001f) {
        return 0.0f;
    }

    const Vec2 normalizedDirection(direction.x / length, direction.y / length);
    const Vec2 perpendicular(-normalizedDirection.y, normalizedDirection.x);
    const Vec2 centerPoint(0.5f, 0.5f);
    const Vec2 tipPoint(centerPoint.x + normalizedDirection.x * 0.24f, centerPoint.y + normalizedDirection.y * 0.24f);
    const float shaft = LineMask(point,
        Vec2(centerPoint.x - normalizedDirection.x * 0.10f, centerPoint.y - normalizedDirection.y * 0.10f),
        Vec2(tipPoint.x - normalizedDirection.x * 0.05f, tipPoint.y - normalizedDirection.y * 0.05f),
        0.028f);
    const float leftHead = LineMask(point,
        Vec2(tipPoint.x - normalizedDirection.x * 0.10f + perpendicular.x * 0.06f, tipPoint.y - normalizedDirection.y * 0.10f + perpendicular.y * 0.06f),
        tipPoint,
        0.024f);
    const float rightHead = LineMask(point,
        Vec2(tipPoint.x - normalizedDirection.x * 0.10f - perpendicular.x * 0.06f, tipPoint.y - normalizedDirection.y * 0.10f - perpendicular.y * 0.06f),
        tipPoint,
        0.024f);
    return std::max(shaft, std::max(leftHead, rightHead));
}

Vec2 ArrowIntentDirection(std::uint8_t laneIntent) {
    switch (laneIntent) {
        case kLaneIntentNorth:
            return Vec2(0.0f, -1.0f);
        case kLaneIntentEast:
            return Vec2(1.0f, 0.0f);
        case kLaneIntentSouth:
            return Vec2(0.0f, 1.0f);
        case kLaneIntentWest:
            return Vec2(-1.0f, 0.0f);
        default:
            return Vec2(0.0f, 0.0f);
    }
}

bool SidewalkMask(float u, float v, std::uint8_t sidewalkEdges) {
    return ((sidewalkEdges & kRoadDirectionNorth) != 0 && v < 0.16f) ||
        ((sidewalkEdges & kRoadDirectionEast) != 0 && u > 0.84f) ||
        ((sidewalkEdges & kRoadDirectionSouth) != 0 && v > 0.84f) ||
        ((sidewalkEdges & kRoadDirectionWest) != 0 && u < 0.16f);
}

bool CrosswalkMask(float u, float v, std::uint8_t crosswalkEdges) {
    const bool stripe = std::fmod((u + v) * 10.0f, 1.0f) < 0.52f;
    if (!stripe) {
        return false;
    }

    return ((crosswalkEdges & kRoadDirectionNorth) != 0 && v < 0.18f) ||
        ((crosswalkEdges & kRoadDirectionEast) != 0 && u > 0.82f) ||
        ((crosswalkEdges & kRoadDirectionSouth) != 0 && v > 0.82f) ||
        ((crosswalkEdges & kRoadDirectionWest) != 0 && u < 0.18f);
}

int MaterialOffsetForBaseGlyph(std::uint8_t baseGlyph) {
    return baseGlyph >= static_cast<std::uint8_t>(RoadBaseGlyph::HighwayIsolated) ? kRoadMaterialAtlasStride : 0;
}

void SetPixel(RoadAtlasImage& image, int glyphIndex, int localX, int localY, Rgba color) {
    const int cellX = glyphIndex % image.columns;
    const int cellY = glyphIndex / image.columns;
    const int pixelX = cellX * image.tileSize + localX;
    const int pixelY = cellY * image.tileSize + localY;
    const std::size_t pixelOffset = (static_cast<std::size_t>(pixelY) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(pixelX)) * 4u;
    image.pixels[pixelOffset + 0u] = color.r;
    image.pixels[pixelOffset + 1u] = color.g;
    image.pixels[pixelOffset + 2u] = color.b;
    image.pixels[pixelOffset + 3u] = color.a;
}

void PaintRoadTile(RoadAtlasImage& image, int glyphIndex, const Rgba& roadColor, std::uint8_t sidewalkEdges, std::uint8_t crosswalkEdges, std::uint8_t dividerMask, bool includeDebugMarkings, std::uint8_t junctionMask) {
    const std::uint8_t whiteDividers = dividerMask & 0x0fu;
    const std::uint8_t yellowDividers = static_cast<std::uint8_t>((dividerMask >> 4) & 0x0fu);
    const std::uint8_t medianDividers = static_cast<std::uint8_t>(whiteDividers & yellowDividers);
    const std::uint8_t whiteLaneDividers = static_cast<std::uint8_t>(whiteDividers & ~medianDividers);
    const std::uint8_t yellowLaneDividers = static_cast<std::uint8_t>(yellowDividers & ~medianDividers);
    for (int localY = 0; localY < image.tileSize; ++localY) {
        for (int localX = 0; localX < image.tileSize; ++localX) {
            const float u = (static_cast<float>(localX) + 0.5f) / static_cast<float>(image.tileSize);
            const float v = (static_cast<float>(localY) + 0.5f) / static_cast<float>(image.tileSize);
            Rgba color = SidewalkMask(u, v, sidewalkEdges) ? kSidewalk : roadColor;
            if (CrosswalkMask(u, v, crosswalkEdges)) {
                color = kCrosswalk;
            }

            if ((medianDividers & kRoadDirectionNorth) != 0 && v < 0.08f) {
                color = kMedian;
            } else if ((medianDividers & kRoadDirectionSouth) != 0 && v > 0.92f) {
                color = kMedian;
            } else if ((medianDividers & kRoadDirectionWest) != 0 && u < 0.08f) {
                color = kMedian;
            } else if ((medianDividers & kRoadDirectionEast) != 0 && u > 0.92f) {
                color = kMedian;
            } else if ((yellowLaneDividers & kRoadDirectionNorth) != 0 && v < 0.035f) {
                color = kYellowMarking;
            } else if ((yellowLaneDividers & kRoadDirectionSouth) != 0 && v > 0.965f) {
                color = kYellowMarking;
            } else if ((yellowLaneDividers & kRoadDirectionWest) != 0 && u < 0.035f) {
                color = kYellowMarking;
            } else if ((yellowLaneDividers & kRoadDirectionEast) != 0 && u > 0.965f) {
                color = kYellowMarking;
            } else {
                const bool horizontalDash = std::fmod(u * 3.0f, 1.0f) < 0.58f;
                const bool verticalDash = std::fmod(v * 3.0f, 1.0f) < 0.58f;
                const bool whiteDivider =
                    ((whiteLaneDividers & kRoadDirectionNorth) != 0 && v < 0.035f && horizontalDash) ||
                    ((whiteLaneDividers & kRoadDirectionSouth) != 0 && v > 0.965f && horizontalDash) ||
                    ((whiteLaneDividers & kRoadDirectionWest) != 0 && u < 0.035f && verticalDash) ||
                    ((whiteLaneDividers & kRoadDirectionEast) != 0 && u > 0.965f && verticalDash);
                if (whiteDivider) {
                    color = kWhiteMarking;
                }
            }

            if (includeDebugMarkings && junctionMask != 0) {
                const Vec2 point(u, v);
                const Vec2 center(0.5f, 0.5f);
                float markingMask = 0.0f;
                if ((junctionMask & kRoadDirectionNorth) != 0) {
                    markingMask = std::max(markingMask, LineMask(point, center, Vec2(0.5f, 0.08f), 0.02f));
                }
                if ((junctionMask & kRoadDirectionEast) != 0) {
                    markingMask = std::max(markingMask, LineMask(point, center, Vec2(0.92f, 0.5f), 0.02f));
                }
                if ((junctionMask & kRoadDirectionSouth) != 0) {
                    markingMask = std::max(markingMask, LineMask(point, center, Vec2(0.5f, 0.92f), 0.02f));
                }
                if ((junctionMask & kRoadDirectionWest) != 0) {
                    markingMask = std::max(markingMask, LineMask(point, center, Vec2(0.08f, 0.5f), 0.02f));
                }
                if (markingMask > 0.0f) {
                    color = kYellowMarking;
                }
            }

            SetPixel(image, glyphIndex, localX, localY, color);
        }
    }
}

void PaintArrowTile(RoadAtlasImage& image, int glyphIndex) {
    const std::uint8_t laneIntentMask = static_cast<std::uint8_t>(glyphIndex) & (kLaneIntentNorth | kLaneIntentEast | kLaneIntentSouth | kLaneIntentWest);
    if (laneIntentMask == 0) {
        return;
    }

    const std::uint8_t laneIntents[] = {
        kLaneIntentNorth,
        kLaneIntentEast,
        kLaneIntentSouth,
        kLaneIntentWest
    };
    for (int localY = 0; localY < image.tileSize; ++localY) {
        for (int localX = 0; localX < image.tileSize; ++localX) {
            const float u = (static_cast<float>(localX) + 0.5f) / static_cast<float>(image.tileSize);
            const float v = (static_cast<float>(localY) + 0.5f) / static_cast<float>(image.tileSize);
            float alpha = 0.0f;
            for (std::size_t intentIndex = 0; intentIndex < sizeof(laneIntents) / sizeof(laneIntents[0]); ++intentIndex) {
                if ((laneIntentMask & laneIntents[intentIndex]) != 0) {
                    alpha = std::max(alpha, ArrowMask(Vec2(u, v), ArrowIntentDirection(laneIntents[intentIndex])));
                }
            }
            if (alpha <= 0.0f) {
                continue;
            }

            Rgba color = kYellowMarking;
            color.a = static_cast<std::uint8_t>(Clamp01(alpha) * 255.0f + 0.5f);
            SetPixel(image, glyphIndex, localX, localY, color);
        }
    }
}

RoadAtlasImage MakeAtlasImage() {
    RoadAtlasImage image;
    image.columns = kRoadAtlasColumns;
    image.rows = kRoadAtlasRows;
    image.tileSize = kRoadAtlasTileSize;
    image.width = image.columns * image.tileSize;
    image.height = image.rows * image.tileSize;
    image.pixels.resize(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4u, 0u);
    return image;
}
}

RoadAtlasImage::RoadAtlasImage()
    : columns(0),
      rows(0),
      tileSize(0),
      width(0),
      height(0) {
}

int RoadAtlasGlyphIndex(std::uint8_t baseGlyph, std::uint8_t laneGraphicMask, std::uint8_t dividerMask) {
    const int materialOffset = MaterialOffsetForBaseGlyph(baseGlyph);
    const std::uint8_t sidewalkEdges = laneGraphicMask & kRoadSurfaceSidewalkEdgeMask;
    const std::uint8_t crosswalkEdges = static_cast<std::uint8_t>((laneGraphicMask >> kRoadSurfaceCrosswalkShift) & kRoadSurfaceSidewalkEdgeMask);
    if (crosswalkEdges != 0) {
        return materialOffset + kSidewalkCrosswalkAtlasOffset + laneGraphicMask;
    }

    if (sidewalkEdges != 0) {
        const std::uint8_t whiteDividers = dividerMask & kRoadSurfaceSidewalkEdgeMask;
        const std::uint8_t yellowDividers = static_cast<std::uint8_t>((dividerMask >> 4) & kRoadSurfaceSidewalkEdgeMask);
        const std::uint8_t medianDividers = static_cast<std::uint8_t>(whiteDividers & yellowDividers);
        if (medianDividers != 0) {
            return materialOffset + kSidewalkMedianDividerAtlasOffset + (static_cast<int>(sidewalkEdges) * 16) + medianDividers;
        }
        if (yellowDividers != 0) {
            return materialOffset + kSidewalkYellowDividerAtlasOffset + (static_cast<int>(sidewalkEdges) * 16) + yellowDividers;
        }
        if (whiteDividers != 0) {
            return materialOffset + kSidewalkWhiteDividerAtlasOffset + (static_cast<int>(sidewalkEdges) * 16) + whiteDividers;
        }
        return materialOffset + kSidewalkAtlasOffset + sidewalkEdges;
    }
    return materialOffset + baseGlyph;
}

RoadAtlasImage BuildRoadBaseAtlas(bool includeDebugMarkings) {
    RoadAtlasImage image = MakeAtlasImage();
    const int baseGlyphCount = static_cast<int>(RoadBaseGlyph::HighwayCross) + 1;
    const Rgba roadColors[] = {
        kConcreteRoad,
        kAsphaltRoad
    };

    for (int materialIndex = 0; materialIndex < 2; ++materialIndex) {
        const int materialOffset = materialIndex * kRoadMaterialAtlasStride;
        const Rgba& roadColor = roadColors[materialIndex];
        for (int glyphIndex = 1; glyphIndex < baseGlyphCount; ++glyphIndex) {
            PaintRoadTile(image, materialOffset + glyphIndex, roadColor, 0, 0, 0, includeDebugMarkings, BaseGlyphJunctionMask(static_cast<RoadBaseGlyph>(glyphIndex)));
        }

        for (std::uint8_t sidewalkEdges = 1u; sidewalkEdges < 16u; ++sidewalkEdges) {
            PaintRoadTile(image, materialOffset + kSidewalkAtlasOffset + sidewalkEdges, roadColor, sidewalkEdges, 0, 0, includeDebugMarkings, 0);
            for (std::uint8_t dividerMask = 1u; dividerMask < 16u; ++dividerMask) {
                PaintRoadTile(
                    image,
                    materialOffset + kSidewalkYellowDividerAtlasOffset + (static_cast<int>(sidewalkEdges) * 16) + dividerMask,
                    roadColor,
                    sidewalkEdges,
                    0,
                    static_cast<std::uint8_t>(dividerMask << kRoadDividerYellowShift),
                    includeDebugMarkings,
                    0);
                PaintRoadTile(
                    image,
                    materialOffset + kSidewalkWhiteDividerAtlasOffset + (static_cast<int>(sidewalkEdges) * 16) + dividerMask,
                    roadColor,
                    sidewalkEdges,
                    0,
                    dividerMask,
                    includeDebugMarkings,
                    0);
                PaintRoadTile(
                    image,
                    materialOffset + kSidewalkMedianDividerAtlasOffset + (static_cast<int>(sidewalkEdges) * 16) + dividerMask,
                    roadColor,
                    sidewalkEdges,
                    0,
                    PackDividerMask(dividerMask, dividerMask),
                    includeDebugMarkings,
                    0);
            }
        }

        for (std::uint16_t laneGraphicMask = 1u; laneGraphicMask < 256u; ++laneGraphicMask) {
            const std::uint8_t crosswalkEdges = static_cast<std::uint8_t>((laneGraphicMask >> kRoadSurfaceCrosswalkShift) & kRoadSurfaceSidewalkEdgeMask);
            if (crosswalkEdges == 0) {
                continue;
            }

            PaintRoadTile(
                image,
                materialOffset + kSidewalkCrosswalkAtlasOffset + laneGraphicMask,
                roadColor,
                static_cast<std::uint8_t>(laneGraphicMask & kRoadSurfaceSidewalkEdgeMask),
                crosswalkEdges,
                0,
                includeDebugMarkings,
                0);
        }
    }

    return image;
}

RoadAtlasImage BuildRoadArrowAtlas() {
    RoadAtlasImage image = MakeAtlasImage();
    for (int glyphIndex = 1; glyphIndex < 16; ++glyphIndex) {
        PaintArrowTile(image, glyphIndex);
    }
    return image;
}
