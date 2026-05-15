#include "RendererAlgorithms.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace {
const float kTileStateScalarScale = 640000.0f;
const std::uint8_t kOccupiedTileLiftMask = 255u;

struct RendererColor {
    float r;
    float g;
    float b;
    float a;

    RendererColor(float red, float green, float blue, float alpha)
        : r(red),
          g(green),
          b(blue),
          a(alpha) {
    }
};

struct GlyphPattern {
    std::uint32_t codepoint;
    const char* rows[7];
};

const char* const* RendererGlyphRows(std::uint32_t codepoint) {
    if (codepoint >= 'a' && codepoint <= 'z') {
        codepoint = codepoint - 'a' + 'A';
    }

    static const GlyphPattern glyphs[] = {
        {' ', {"00000", "00000", "00000", "00000", "00000", "00000", "00000"}},
        {'0', {"01110", "10001", "10011", "10101", "11001", "10001", "01110"}},
        {'1', {"00100", "01100", "00100", "00100", "00100", "00100", "01110"}},
        {'2', {"01110", "10001", "00001", "00010", "00100", "01000", "11111"}},
        {'3', {"11110", "00001", "00001", "01110", "00001", "00001", "11110"}},
        {'4', {"00010", "00110", "01010", "10010", "11111", "00010", "00010"}},
        {'5', {"11111", "10000", "10000", "11110", "00001", "00001", "11110"}},
        {'6', {"01110", "10000", "10000", "11110", "10001", "10001", "01110"}},
        {'7', {"11111", "00001", "00010", "00100", "01000", "01000", "01000"}},
        {'8', {"01110", "10001", "10001", "01110", "10001", "10001", "01110"}},
        {'9', {"01110", "10001", "10001", "01111", "00001", "00001", "01110"}},
        {'A', {"01110", "10001", "10001", "11111", "10001", "10001", "10001"}},
        {'B', {"11110", "10001", "10001", "11110", "10001", "10001", "11110"}},
        {'C', {"01110", "10001", "10000", "10000", "10000", "10001", "01110"}},
        {'D', {"11110", "10001", "10001", "10001", "10001", "10001", "11110"}},
        {'E', {"11111", "10000", "10000", "11110", "10000", "10000", "11111"}},
        {'F', {"11111", "10000", "10000", "11110", "10000", "10000", "10000"}},
        {'G', {"01110", "10001", "10000", "10111", "10001", "10001", "01111"}},
        {'H', {"10001", "10001", "10001", "11111", "10001", "10001", "10001"}},
        {'I', {"01110", "00100", "00100", "00100", "00100", "00100", "01110"}},
        {'J', {"00111", "00010", "00010", "00010", "10010", "10010", "01100"}},
        {'K', {"10001", "10010", "10100", "11000", "10100", "10010", "10001"}},
        {'L', {"10000", "10000", "10000", "10000", "10000", "10000", "11111"}},
        {'M', {"10001", "11011", "10101", "10101", "10001", "10001", "10001"}},
        {'N', {"10001", "11001", "10101", "10011", "10001", "10001", "10001"}},
        {'O', {"01110", "10001", "10001", "10001", "10001", "10001", "01110"}},
        {'P', {"11110", "10001", "10001", "11110", "10000", "10000", "10000"}},
        {'Q', {"01110", "10001", "10001", "10001", "10101", "10010", "01101"}},
        {'R', {"11110", "10001", "10001", "11110", "10100", "10010", "10001"}},
        {'S', {"01111", "10000", "10000", "01110", "00001", "00001", "11110"}},
        {'T', {"11111", "00100", "00100", "00100", "00100", "00100", "00100"}},
        {'U', {"10001", "10001", "10001", "10001", "10001", "10001", "01110"}},
        {'V', {"10001", "10001", "10001", "10001", "10001", "01010", "00100"}},
        {'W', {"10001", "10001", "10001", "10101", "10101", "10101", "01010"}},
        {'X', {"10001", "10001", "01010", "00100", "01010", "10001", "10001"}},
        {'Y', {"10001", "10001", "01010", "00100", "00100", "00100", "00100"}},
        {'Z', {"11111", "00001", "00010", "00100", "01000", "10000", "11111"}},
        {'#', {"01010", "01010", "11111", "01010", "11111", "01010", "01010"}},
        {':', {"00000", "00100", "00100", "00000", "00100", "00100", "00000"}},
        {'/', {"00001", "00010", "00010", "00100", "01000", "01000", "10000"}},
        {'-', {"00000", "00000", "00000", "11111", "00000", "00000", "00000"}},
        {'_', {"00000", "00000", "00000", "00000", "00000", "00000", "11111"}},
        {',', {"00000", "00000", "00000", "00000", "00100", "00100", "01000"}},
        {'.', {"00000", "00000", "00000", "00000", "00000", "01100", "01100"}},
        {'(', {"00010", "00100", "01000", "01000", "01000", "00100", "00010"}},
        {')', {"01000", "00100", "00010", "00010", "00010", "00100", "01000"}},
        {'+', {"00000", "00100", "00100", "11111", "00100", "00100", "00000"}},
        {'=', {"00000", "00000", "11111", "00000", "11111", "00000", "00000"}},
        {'?', {"01110", "10001", "00001", "00010", "00100", "00000", "00100"}}
    };

    std::size_t glyphIndex = 0;
    for (; glyphIndex < sizeof(glyphs) / sizeof(glyphs[0]); ++glyphIndex) {
        if (glyphs[glyphIndex].codepoint == codepoint) {
            return glyphs[glyphIndex].rows;
        }
    }

    return glyphs[sizeof(glyphs) / sizeof(glyphs[0]) - 1u].rows;
}

void RendererAddUiQuad(std::vector<UiQuadInstanceData>& quads, float x, float y, float width, float height, const RendererColor& color) {
    if (width <= 0.0f || height <= 0.0f || color.a <= 0.0f) {
        return;
    }

    UiQuadInstanceData quad;
    quad.x = x;
    quad.y = y;
    quad.width = width;
    quad.height = height;
    quad.colorR = color.r;
    quad.colorG = color.g;
    quad.colorB = color.b;
    quad.colorA = color.a;
    quads.push_back(quad);
}

void RendererBuildTextQuads(const TextFieldElement& field, float windowX, float windowY, std::vector<UiQuadInstanceData>& quads) {
    const float scale = 2.0f;
    const float characterAdvance = 6.0f * scale;
    const float originX = windowX + static_cast<float>(field.x());
    const float originY = windowY + static_cast<float>(field.y());
    const float maxX = originX + static_cast<float>(field.width());
    const float maxY = originY + static_cast<float>(field.height());
    const RendererColor textColor(0.88f, 0.94f, 0.91f, 1.0f);

    float cursorX = originX;
    std::size_t byteIndex = 0;
    while (byteIndex < field.text().size()) {
        std::uint32_t codepoint = 0u;
        if (!RendererNextUtf8Codepoint(field.text(), byteIndex, codepoint) || codepoint == '\n') {
            break;
        }

        if (cursorX + (5.0f * scale) > maxX || originY + (7.0f * scale) > maxY) {
            break;
        }

        const char* const* rows = RendererGlyphRows(codepoint);
        int row = 0;
        for (; row < 7; ++row) {
            int column = 0;
            for (; column < 5; ++column) {
                if (rows[row][column] == '1') {
                    RendererAddUiQuad(quads, cursorX + static_cast<float>(column) * scale, originY + static_cast<float>(row) * scale, scale, scale, textColor);
                }
            }
        }

        cursorX += characterAdvance;
    }
}
}

std::int16_t RendererPackTileStateScalar(int value) {
    const float normalizedValue = std::max(-1.0f, std::min(static_cast<float>(value) / kTileStateScalarScale, 1.0f));
    if (normalizedValue <= -1.0f) {
        return static_cast<std::int16_t>(-32768);
    }

    return static_cast<std::int16_t>(std::lround(normalizedValue * 32767.0f));
}

void RendererFillTileStateChunkPixels(const std::vector<Tile>& tiles, int mapWidth, const ChunkRect& chunkRect, std::vector<std::int16_t>& texturePixels) {
    const std::size_t chunkTileCount = static_cast<std::size_t>(chunkRect.width) * static_cast<std::size_t>(chunkRect.height);
    if (texturePixels.size() != chunkTileCount * 2u) {
        texturePixels.resize(chunkTileCount * 2u, 0);
    }

    std::size_t writeIndex = 0;
    int tileY = chunkRect.startY;
    for (; tileY < chunkRect.startY + chunkRect.height; ++tileY) {
        int tileX = chunkRect.startX;
        for (; tileX < chunkRect.startX + chunkRect.width; ++tileX) {
            const std::size_t sourceIndex = static_cast<std::size_t>(tileY) * static_cast<std::size_t>(mapWidth) + static_cast<std::size_t>(tileX);
            const Tile& tile = tiles[sourceIndex];
            texturePixels[writeIndex++] = RendererPackTileStateScalar(tile.airPollution);
            texturePixels[writeIndex++] = RendererPackTileStateScalar(tile.landValue);
        }
    }
}

void RendererFillTileLiftChunkPixels(const std::vector<int>& lotOccupancy, int mapWidth, const ChunkRect& chunkRect, std::vector<std::uint8_t>& texturePixels) {
    const std::size_t chunkTileCount = static_cast<std::size_t>(chunkRect.width) * static_cast<std::size_t>(chunkRect.height);
    if (texturePixels.size() != chunkTileCount) {
        texturePixels.resize(chunkTileCount, 0u);
    }

    std::size_t writeIndex = 0;
    int tileY = chunkRect.startY;
    for (; tileY < chunkRect.startY + chunkRect.height; ++tileY) {
        int tileX = chunkRect.startX;
        for (; tileX < chunkRect.startX + chunkRect.width; ++tileX) {
            const std::size_t sourceIndex = static_cast<std::size_t>(tileY) * static_cast<std::size_t>(mapWidth) + static_cast<std::size_t>(tileX);
            texturePixels[writeIndex++] = lotOccupancy[sourceIndex] < 0 ? 0u : kOccupiedTileLiftMask;
        }
    }
}

bool RendererNextUtf8Codepoint(const std::string& text, std::size_t& byteIndex, std::uint32_t& codepoint) {
    if (byteIndex >= text.size()) {
        return false;
    }

    const unsigned char first = static_cast<unsigned char>(text[byteIndex++]);
    if (first < 0x80u) {
        codepoint = first;
        return true;
    }

    int continuationCount = 0;
    std::uint32_t value = 0u;
    if ((first & 0xE0u) == 0xC0u) {
        continuationCount = 1;
        value = first & 0x1Fu;
    } else if ((first & 0xF0u) == 0xE0u) {
        continuationCount = 2;
        value = first & 0x0Fu;
    } else if ((first & 0xF8u) == 0xF0u) {
        continuationCount = 3;
        value = first & 0x07u;
    } else {
        codepoint = '?';
        return true;
    }

    int continuationIndex = 0;
    for (; continuationIndex < continuationCount; ++continuationIndex) {
        if (byteIndex >= text.size()) {
            codepoint = '?';
            return true;
        }

        const unsigned char next = static_cast<unsigned char>(text[byteIndex++]);
        if ((next & 0xC0u) != 0x80u) {
            codepoint = '?';
            return true;
        }

        value = (value << 6) | static_cast<std::uint32_t>(next & 0x3Fu);
    }

    codepoint = value;
    return true;
}

std::vector<UiQuadInstanceData> RendererBuildWindowQuads(const InGameWindow& window) {
    std::vector<UiQuadInstanceData> quads;
    if (!window.visible()) {
        return quads;
    }

    const float x = static_cast<float>(window.x());
    const float y = static_cast<float>(window.y());
    const float width = static_cast<float>(window.width());
    const float height = static_cast<float>(window.height());
    RendererAddUiQuad(quads, x, y, width, height, RendererColor(0.035f, 0.047f, 0.058f, 0.90f));
    RendererAddUiQuad(quads, x, y, width, 2.0f, RendererColor(0.36f, 0.52f, 0.47f, 0.92f));
    RendererAddUiQuad(quads, x, y + height - 2.0f, width, 2.0f, RendererColor(0.13f, 0.18f, 0.19f, 0.92f));
    RendererAddUiQuad(quads, x, y, 2.0f, height, RendererColor(0.22f, 0.31f, 0.30f, 0.92f));
    RendererAddUiQuad(quads, x + width - 2.0f, y, 2.0f, height, RendererColor(0.13f, 0.18f, 0.19f, 0.92f));

    const std::vector<TextFieldElement>& textFields = window.textFields();
    std::size_t fieldIndex = 0;
    for (; fieldIndex < textFields.size(); ++fieldIndex) {
        RendererBuildTextQuads(textFields[fieldIndex], x, y, quads);
    }

    return quads;
}
