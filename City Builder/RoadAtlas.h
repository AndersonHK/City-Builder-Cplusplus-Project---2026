#pragma once

#include <cstdint>
#include <vector>

#include "TransportTypes.h"

struct RoadAtlasImage {
    int columns;
    int rows;
    int tileSize;
    int width;
    int height;
    std::vector<std::uint8_t> pixels;

    RoadAtlasImage();
};

constexpr int kRoadAtlasColumns = 32;
constexpr int kRoadAtlasRows = 128;
constexpr int kRoadAtlasTileSize = 32;

int RoadAtlasGlyphIndex(std::uint8_t baseGlyph, std::uint8_t laneGraphicMask, std::uint8_t dividerMask);
RoadAtlasImage BuildRoadBaseAtlas(bool includeDebugMarkings);
RoadAtlasImage BuildRoadArrowAtlas();
