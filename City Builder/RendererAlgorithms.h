#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ChunkConfig.h"
#include "InGameWindow.h"
#include "Tile.h"
#include "UiWidgets.h"

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
void RendererFillZoningOverlayChunkPixels(const std::vector<Tile>& tiles, int mapWidth, const ChunkRect& chunkRect, std::vector<std::uint8_t>& texturePixels);
bool RendererFindLandValueRange(const std::vector<Tile>& tiles, int& minimumLandValue, int& maximumLandValue);
void RendererFillLandValueOverlayChunkPixels(const std::vector<Tile>& tiles, int mapWidth, const ChunkRect& chunkRect, int minimumLandValue, int maximumLandValue, std::uint8_t alpha, std::vector<std::uint8_t>& texturePixels);
bool RendererNextUtf8Codepoint(const std::string& text, std::size_t& byteIndex, std::uint32_t& codepoint);
std::vector<UiQuadInstanceData> RendererBuildWindowQuads(const InGameWindow& window);
std::vector<UiQuadInstanceData> RendererBuildUiMenuQuads(const UiLayout& uiLayout, int framebufferWidth, int framebufferHeight, const std::string& activeAction);
std::vector<UiQuadInstanceData> RendererBuildUiMenuQuads(const UiLayout& uiLayout, int framebufferWidth, int framebufferHeight, const std::vector<std::string>& activeActions);
std::vector<UiQuadInstanceData> RendererBuildUiMenuQuads(const UiLayout& uiLayout, int framebufferWidth, int framebufferHeight, const std::vector<std::string>& activeActions, const std::vector<std::string>& menuIds);
void RendererAppendTextQuads(const std::string& text, float x, float y, float width, float height, const UiColor& textColor, bool centered, std::vector<UiQuadInstanceData>& quads);
void RendererAppendLoadingScreenQuads(const std::string& label, float progress, int framebufferWidth, int framebufferHeight, std::vector<UiQuadInstanceData>& quads);
