#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ChunkConfig.h"
#include "InGameWindow.h"
#include "Tile.h"

struct UiQuadInstanceData {
    float x;
    float y;
    float width;
    float height;
    float colorR;
    float colorG;
    float colorB;
    float colorA;
};

std::int16_t RendererPackTileStateScalar(int value);
void RendererFillTileStateChunkPixels(const std::vector<Tile>& tiles, int mapWidth, const ChunkRect& chunkRect, std::vector<std::int16_t>& texturePixels);
void RendererFillTileLiftChunkPixels(const std::vector<int>& lotOccupancy, int mapWidth, const ChunkRect& chunkRect, std::vector<std::uint8_t>& texturePixels);
bool RendererNextUtf8Codepoint(const std::string& text, std::size_t& byteIndex, std::uint32_t& codepoint);
std::vector<UiQuadInstanceData> RendererBuildWindowQuads(const InGameWindow& window);
