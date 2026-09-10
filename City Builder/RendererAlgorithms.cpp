#include "RendererAlgorithms.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <limits>

namespace {
struct RoutePoint {
    float x, z, seconds;
};
float RouteDistance(const RoutePoint& a, const RoutePoint& b) {
    const float x = b.x - a.x, z = b.z - a.z;
    return std::sqrt(x * x + z * z);
}
RoutePoint RouteLerp(const RoutePoint& a, const RoutePoint& b, float t) {
    return {a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t,
            a.seconds + (b.seconds - a.seconds) * t};
}
// Preserve every speed change, but avoid emitting one quad per tile at constant speed.
void AppendRoutePoint(std::vector<RoutePoint>& points, const RoutePoint& point) {
    if (points.size() >= 2) {
        const auto& a = points[points.size() - 2];
        const auto& b = points.back();
        const float ab = RouteDistance(a, b), bc = RouteDistance(b, point);
        if (ab > 0 && bc > 0 &&
            std::abs((b.x - a.x) * (point.z - b.z) - (b.z - a.z) * (point.x - b.x)) < 0.00001f &&
            (b.x - a.x) * (point.x - b.x) + (b.z - a.z) * (point.z - b.z) > 0 &&
            std::abs((b.seconds - a.seconds) / ab - (point.seconds - b.seconds) / bc) < 0.00001f) {
            points.back() = point;
            return;
        }
    }
    points.push_back(point);
}
void AppendRouteRibbon(const std::vector<RoutePoint>& points, const CommuteRouteSegment& style,
                       float secondsPerArrow, std::vector<RouteArrowInstanceData>& instances) {
    if (points.size() < 2) return;
    std::vector<RoutePoint> curve;
    curve.push_back(points.front());
    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
        const auto& a = points[i - 1];
        const auto& b = points[i];
        const auto& c = points[i + 1];
        const float ab = RouteDistance(a, b), bc = RouteDistance(b, c);
        const float dot = ((b.x - a.x) * (c.x - b.x) + (b.z - a.z) * (c.z - b.z)) / (ab * bc);
        if (dot > 0.999f || dot < -0.999f) {
            curve.push_back(b);
            continue;
        }
        const float radius = std::min(0.6f, 0.45f * std::min(ab, bc));
        const auto entry = RouteLerp(b, a, radius / ab);
        const auto exit = RouteLerp(b, c, radius / bc);
        curve.push_back(entry);
        // Round inside the road corner; preserve the clock on both sides of the turn.
        for (int j = 1; j <= 8; ++j) {
            const float t = j / 8.0f;
            auto point = RouteLerp(RouteLerp(entry, b, t), RouteLerp(b, exit, t), t);
            point.seconds = t <= 0.5f ? RouteLerp(entry, b, t * 2).seconds : RouteLerp(b, exit, t * 2 - 1).seconds;
            curve.push_back(point);
        }
    }
    curve.push_back(points.back());
    std::vector<RoutePoint> normals(curve.size());
    for (std::size_t i = 0; i < curve.size(); ++i) {
        const auto& a = curve[i == 0 ? 0 : i - 1];
        const auto& b = curve[i];
        const auto& c = curve[i + 1 == curve.size() ? i : i + 1];
        const float ab = RouteDistance(a, b), bc = RouteDistance(b, c);
        float nx = bc > 0 ? -(c.z - b.z) / bc : -(b.z - a.z) / ab;
        float nz = bc > 0 ? (c.x - b.x) / bc : (b.x - a.x) / ab;
        if (ab > 0 && bc > 0) {
            const float mx = nx - (b.z - a.z) / ab, mz = nz + (b.x - a.x) / ab;
            const float denominator = mx * nx + mz * nz;
            if (denominator > 0.5f) { nx = mx / denominator; nz = mz / denominator; }
        }
        normals[i] = {nx, nz, 0};
    }
    for (std::size_t i = 1; i < curve.size(); ++i) {
        const auto& a = curve[i - 1];
        const auto& b = curve[i];
        const float length = RouteDistance(a, b);
        if (length == 0) continue;
        RouteArrowInstanceData instance{};
        instance.originX = a.x;
        instance.originZ = a.z;
        instance.sizeX = length;
        instance.sizeZ = 0.56f;
        instance.directionX = (b.x - a.x) / length;
        instance.directionZ = (b.z - a.z) / length;
        const float layerLift = style.layer == TransportLayerId::Elevated ? 0.60f :
            style.layer == TransportLayerId::Underground ? -0.25f : 0.035f;
        instance.lift = layerLift + 0.09f;
        instance.alpha = 0.88f;
        const bool pedestrian = style.mode == TransportMode::Pedestrian;
        instance.colorR = pedestrian ? 1.0f : 0.08f;
        instance.colorG = pedestrian ? 0.22f : 0.95f;
        instance.colorB = pedestrian ? 0.66f : 0.26f;
        instance.startPhase = a.seconds / secondsPerArrow;
        instance.endPhase = b.seconds / secondsPerArrow;
        instance.startNormalX = normals[i - 1].x;
        instance.startNormalZ = normals[i - 1].z;
        instance.endNormalX = normals[i].x;
        instance.endNormalZ = normals[i].z;
        instances.push_back(instance);
    }
}
}

std::vector<RouteArrowInstanceData> BuildRouteArrowInstances(const std::vector<CommuteRouteSegment>& segments,
                                                           float secondsPerArrow) {
    std::vector<RouteArrowInstanceData> instances;
    if (!std::isfinite(secondsPerArrow) || secondsPerArrow <= 0) return instances;
    std::vector<RoutePoint> points;
    const CommuteRouteSegment* previous = nullptr;
    for (const auto& segment : segments) {
        const float dx = static_cast<float>(segment.endTileX - segment.startTileX);
        const float dz = static_cast<float>(segment.endTileY - segment.startTileY);
        if (dx == 0 && dz == 0) continue;
        const bool timed = segment.elapsedSeconds && segment.timingEnd > segment.timingBegin &&
                           segment.timingEnd < segment.elapsedSeconds->size();
        const bool joins = previous && timed && previous->elapsedSeconds == segment.elapsedSeconds &&
            previous->timingEnd == segment.timingBegin && previous->endTileX == segment.startTileX &&
            previous->endTileY == segment.startTileY && previous->mode == segment.mode && previous->layer == segment.layer;
        if (!joins && !points.empty()) {
            AppendRouteRibbon(points, *previous, secondsPerArrow, instances);
            points.clear();
        }
        const std::size_t steps = timed ? segment.timingEnd - segment.timingBegin : 1;
        for (std::size_t i = joins ? 1 : 0; i <= steps; ++i) {
            // Untimed debug fixtures retain a ribbon without pretending to encode speed.
            const float seconds = timed ? (*segment.elapsedSeconds)[segment.timingBegin + i] : 0.5f * secondsPerArrow;
            AppendRoutePoint(points, {segment.startTileX + 0.5f + (dx / steps) * i,
                                     segment.startTileY + 0.5f + (dz / steps) * i, seconds});
        }
        previous = &segment;
    }
    if (!points.empty()) AppendRouteRibbon(points, *previous, secondsPerArrow, instances);
    return instances;
}

namespace {
// Keep occupied terrain below shallow lot surfaces; a full mask is close enough
// to concrete pavements to z-fight at distant zoom levels.
const std::uint8_t kOccupiedTileLiftMask = 32u;

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

struct IconPattern {
    const char* name;
    int width;
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

const IconPattern* RendererIconPatternForName(const std::string& iconName) {
    static const IconPattern icons[] = {
        {"pause", 9, {"110001100", "110001100", "110001100", "110001100", "110001100", "110001100", "110001100"}},
        {"play", 9, {"100000000", "111000000", "111110000", "111111100", "111110000", "111000000", "100000000"}},
        {"fast", 9, {"100010000", "110011000", "111011100", "111111110", "111011100", "110011000", "100010000"}},
        {"fastForward", 9, {"100010001", "110011001", "111011101", "111111111", "111011101", "110011001", "100010001"}},
        {"fast_forward", 9, {"100010001", "110011001", "111011101", "111111111", "111011101", "110011001", "100010001"}}
    };

    std::size_t iconIndex = 0;
    for (; iconIndex < sizeof(icons) / sizeof(icons[0]); ++iconIndex) {
        if (iconName == icons[iconIndex].name) {
            return &icons[iconIndex];
        }
    }

    return 0;
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

RendererColor ToRendererColor(const UiColor& color) {
    return RendererColor(color.r, color.g, color.b, color.a);
}

std::size_t RendererVisibleCharacterCount(const std::string& text) {
    std::size_t byteIndex = 0;
    std::size_t characterCount = 0;
    while (byteIndex < text.size()) {
        std::uint32_t codepoint = 0u;
        if (!RendererNextUtf8Codepoint(text, byteIndex, codepoint) || codepoint == '\n') {
            break;
        }

        ++characterCount;
    }

    return characterCount;
}

void RendererBuildTextQuadsInRect(const std::string& text, float originX, float originY, float width, float height, const RendererColor& textColor, bool centered, std::vector<UiQuadInstanceData>& quads) {
    const float scale = 2.0f;
    const float characterAdvance = 6.0f * scale;
    const float glyphWidth = 5.0f * scale;
    const float glyphHeight = 7.0f * scale;
    const float maxX = originX + width;
    const float maxY = originY + height;
    float cursorX = originX;
    float cursorY = originY;

    if (centered) {
        const std::size_t characterCount = RendererVisibleCharacterCount(text);
        const float textWidth = characterCount == 0u ? 0.0f : (static_cast<float>(characterCount - 1u) * characterAdvance) + glyphWidth;
        cursorX = originX + std::max(0.0f, (width - textWidth) * 0.5f);
        cursorY = originY + std::max(0.0f, (height - glyphHeight) * 0.5f);
    }

    std::size_t byteIndex = 0;
    while (byteIndex < text.size()) {
        std::uint32_t codepoint = 0u;
        if (!RendererNextUtf8Codepoint(text, byteIndex, codepoint) || codepoint == '\n') {
            break;
        }

        if (cursorX + glyphWidth > maxX || cursorY + glyphHeight > maxY) {
            break;
        }

        const char* const* rows = RendererGlyphRows(codepoint);
        int row = 0;
        for (; row < 7; ++row) {
            int column = 0;
            for (; column < 5; ++column) {
                if (rows[row][column] == '1') {
                    RendererAddUiQuad(quads, cursorX + static_cast<float>(column) * scale, cursorY + static_cast<float>(row) * scale, scale, scale, textColor);
                }
            }
        }

        cursorX += characterAdvance;
    }
}

bool RendererBuildIconQuadsInRect(const std::string& iconName, float originX, float originY, float width, float height, const RendererColor& iconColor, std::vector<UiQuadInstanceData>& quads) {
    const IconPattern* icon = RendererIconPatternForName(iconName);
    if (icon == 0) {
        return false;
    }

    const float scale = 2.0f;
    const float iconWidth = static_cast<float>(icon->width) * scale;
    const float iconHeight = 7.0f * scale;
    const float startX = originX + std::max(0.0f, (width - iconWidth) * 0.5f);
    const float startY = originY + std::max(0.0f, (height - iconHeight) * 0.5f);
    const float maxX = originX + width;
    const float maxY = originY + height;

    int row = 0;
    for (; row < 7; ++row) {
        int column = 0;
        for (; column < icon->width; ++column) {
            if (icon->rows[row][column] != '1') {
                continue;
            }

            const float x = startX + static_cast<float>(column) * scale;
            const float y = startY + static_cast<float>(row) * scale;
            if (x + scale <= maxX && y + scale <= maxY) {
                RendererAddUiQuad(quads, x, y, scale, scale, iconColor);
            }
        }
    }

    return true;
}

void RendererBuildTextQuads(const TextFieldElement& field, float windowX, float windowY, std::vector<UiQuadInstanceData>& quads) {
    RendererBuildTextQuadsInRect(
        field.text(),
        windowX + static_cast<float>(field.x()),
        windowY + static_cast<float>(field.y()),
        static_cast<float>(field.width()),
        static_cast<float>(field.height()),
        RendererColor(0.88f, 0.94f, 0.91f, 1.0f),
        false,
        quads);
}
}

RendererSignedScalarPayload RendererPackTileStateScalar(int value) {
    if (value >= kSimulationStatDisplayCap) {
        return static_cast<RendererSignedScalarPayload>(kRendererSignedScalarPayloadMaxValue);
    }

    if (value <= -kSimulationStatDisplayCap) {
        return static_cast<RendererSignedScalarPayload>(kRendererSignedScalarPayloadMinValue);
    }

    const bool negative = value < 0;
    const std::int64_t wideValue = static_cast<std::int64_t>(value);
    const std::uint64_t magnitude = static_cast<std::uint64_t>(negative ? -wideValue : wideValue);
    const std::uint64_t packedMagnitude =
        ((magnitude * static_cast<std::uint64_t>(kRendererSignedScalarPayloadMaxValue)) +
         (static_cast<std::uint64_t>(kSimulationStatDisplayCap) / 2u)) /
        static_cast<std::uint64_t>(kSimulationStatDisplayCap);
    const std::int32_t signedValue = static_cast<std::int32_t>(packedMagnitude);
    return static_cast<RendererSignedScalarPayload>(negative ? -signedValue : signedValue);
}

void RendererFillTileStateChunkPixels(const std::vector<Tile>& tiles, int mapWidth, const ChunkRect& chunkRect, std::vector<RendererSignedScalarPayload>& texturePixels) {
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
            texturePixels[writeIndex++] = RendererPackTileStateScalar(tile.parkEffect);
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

void RendererFillZoningOverlayChunkValues(const std::vector<Tile>& tiles, int mapWidth, const ChunkRect& chunkRect, std::vector<RendererScalarPayload>& textureValues) {
    const std::size_t chunkTileCount = static_cast<std::size_t>(chunkRect.width) * static_cast<std::size_t>(chunkRect.height);
    if (textureValues.size() != chunkTileCount) {
        textureValues.resize(chunkTileCount, 0u);
    }

    std::size_t writeIndex = 0;
    int tileY = chunkRect.startY;
    for (; tileY < chunkRect.startY + chunkRect.height; ++tileY) {
        int tileX = chunkRect.startX;
        for (; tileX < chunkRect.startX + chunkRect.width; ++tileX) {
            const std::size_t sourceIndex = static_cast<std::size_t>(tileY) * static_cast<std::size_t>(mapWidth) + static_cast<std::size_t>(tileX);
            const Tile& tile = tiles[sourceIndex];
            textureValues[writeIndex++] = tile.zoningType;
        }
    }
}

void RendererFillLandValueOverlayChunkValues(const std::vector<Tile>& tiles, int mapWidth, const ChunkRect& chunkRect, std::vector<RendererScalarPayload>& textureValues) {
    const std::size_t chunkTileCount = static_cast<std::size_t>(chunkRect.width) * static_cast<std::size_t>(chunkRect.height);
    if (textureValues.size() != chunkTileCount) {
        textureValues.resize(chunkTileCount, 0u);
    }

    std::size_t writeIndex = 0;
    int tileY = chunkRect.startY;
    for (; tileY < chunkRect.startY + chunkRect.height; ++tileY) {
        int tileX = chunkRect.startX;
        for (; tileX < chunkRect.startX + chunkRect.width; ++tileX) {
            const std::size_t sourceIndex = static_cast<std::size_t>(tileY) * static_cast<std::size_t>(mapWidth) + static_cast<std::size_t>(tileX);
            textureValues[writeIndex++] = RendererPackCappedStatToScalarPayload(tiles[sourceIndex].landValue, kSimulationStatDisplayCap);
        }
    }
}

void RendererFillAirPollutionOverlayChunkValues(const std::vector<Tile>& tiles, int mapWidth, const ChunkRect& chunkRect, std::vector<RendererScalarPayload>& textureValues) {
    const std::size_t chunkTileCount = static_cast<std::size_t>(chunkRect.width) * static_cast<std::size_t>(chunkRect.height);
    if (textureValues.size() != chunkTileCount) {
        textureValues.resize(chunkTileCount, 0u);
    }

    std::size_t writeIndex = 0;
    int tileY = chunkRect.startY;
    for (; tileY < chunkRect.startY + chunkRect.height; ++tileY) {
        int tileX = chunkRect.startX;
        for (; tileX < chunkRect.startX + chunkRect.width; ++tileX) {
            const std::size_t sourceIndex = static_cast<std::size_t>(tileY) * static_cast<std::size_t>(mapWidth) + static_cast<std::size_t>(tileX);
            textureValues[writeIndex++] = RendererPackCappedStatToScalarPayload(tiles[sourceIndex].airPollution, kSimulationStatDisplayCap);
        }
    }
}

void RendererFillParkEffectOverlayChunkValues(const std::vector<Tile>& tiles, int mapWidth, const ChunkRect& chunkRect, std::vector<RendererScalarPayload>& textureValues) {
    const std::size_t chunkTileCount = static_cast<std::size_t>(chunkRect.width) * static_cast<std::size_t>(chunkRect.height);
    if (textureValues.size() != chunkTileCount) {
        textureValues.resize(chunkTileCount, 0u);
    }

    std::size_t writeIndex = 0;
    int tileY = chunkRect.startY;
    for (; tileY < chunkRect.startY + chunkRect.height; ++tileY) {
        int tileX = chunkRect.startX;
        for (; tileX < chunkRect.startX + chunkRect.width; ++tileX) {
            const std::size_t sourceIndex = static_cast<std::size_t>(tileY) * static_cast<std::size_t>(mapWidth) + static_cast<std::size_t>(tileX);
            textureValues[writeIndex++] = RendererPackCappedStatToScalarPayload(tiles[sourceIndex].parkEffect, kSimulationStatDisplayCap);
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

std::vector<UiQuadInstanceData> RendererBuildUiMenuQuads(const UiLayout& uiLayout, int framebufferWidth, int framebufferHeight, const std::string& activeAction) {
    std::vector<std::string> activeActions;
    if (!activeAction.empty()) {
        activeActions.push_back(activeAction);
    }
    return RendererBuildUiMenuQuads(uiLayout, framebufferWidth, framebufferHeight, activeActions);
}

std::vector<UiQuadInstanceData> RendererBuildUiMenuQuads(const UiLayout& uiLayout, int framebufferWidth, int framebufferHeight, const std::vector<std::string>& activeActions) {
    const std::vector<std::string> menuIds;
    return RendererBuildUiMenuQuads(uiLayout, framebufferWidth, framebufferHeight, activeActions, menuIds);
}

std::vector<UiQuadInstanceData> RendererBuildUiMenuQuads(const UiLayout& uiLayout, int framebufferWidth, int framebufferHeight, const std::vector<std::string>& activeActions, const std::vector<std::string>& menuIds) {
    std::vector<UiQuadInstanceData> quads;
    const std::vector<UiMenu>& menus = uiLayout.menus();
    const bool includeAllMenus = menuIds.empty();

    std::size_t menuIndex = 0;
    for (; menuIndex < menus.size(); ++menuIndex) {
        const UiMenu& menu = menus[menuIndex];
        if (!menu.visible() ||
            (!includeAllMenus && std::find(menuIds.begin(), menuIds.end(), menu.id()) == menuIds.end())) {
            continue;
        }

        UiRect menuRect;
        if (!uiLayout.resolveMenuRect(menu.id(), framebufferWidth, framebufferHeight, menuRect)) {
            continue;
        }
        RendererAddUiQuad(
            quads,
            static_cast<float>(menuRect.x),
            static_cast<float>(menuRect.y),
            static_cast<float>(menuRect.width),
            static_cast<float>(menuRect.height),
            ToRendererColor(menu.backgroundColor()));
    }

    std::vector<UiResolvedButton> resolvedButtons;
    uiLayout.resolveButtons(framebufferWidth, framebufferHeight, activeActions, menuIds, resolvedButtons);

    std::size_t buttonIndex = 0;
    for (; buttonIndex < resolvedButtons.size(); ++buttonIndex) {
        const UiResolvedButton& button = resolvedButtons[buttonIndex];
        const float x = static_cast<float>(button.rect.x);
        const float y = static_cast<float>(button.rect.y);
        const float width = static_cast<float>(button.rect.width);
        const float height = static_cast<float>(button.rect.height);

        if (!button.action.empty()) {
            RendererAddUiQuad(quads, x, y, width, height, ToRendererColor(button.color));
            RendererAddUiQuad(quads, x, y, width, 2.0f, RendererColor(0.70f, 0.78f, 0.70f, button.isActive ? 0.96f : 0.52f));
            RendererAddUiQuad(quads, x, y + height - 2.0f, width, 2.0f, RendererColor(0.03f, 0.04f, 0.05f, 0.56f));
            RendererAddUiQuad(quads, x, y, 2.0f, height, RendererColor(0.58f, 0.66f, 0.61f, button.isActive ? 0.92f : 0.46f));
            RendererAddUiQuad(quads, x + width - 2.0f, y, 2.0f, height, RendererColor(0.03f, 0.04f, 0.05f, 0.56f));
        }
        if (button.icon.empty() ||
            !RendererBuildIconQuadsInRect(button.icon, x, y, width, height, RendererColor(0.92f, 0.96f, 0.92f, 1.0f), quads)) {
            RendererBuildTextQuadsInRect(button.text, x, y, width, height, RendererColor(0.92f, 0.96f, 0.92f, 1.0f), true, quads);
        }
    }

    return quads;
}

void RendererAppendTextQuads(const std::string& text, float x, float y, float width, float height, const UiColor& textColor, bool centered, std::vector<UiQuadInstanceData>& quads) {
    RendererBuildTextQuadsInRect(text, x, y, width, height, ToRendererColor(textColor), centered, quads);
}

void RendererAppendLoadingScreenQuads(const std::string& label, float progress, int framebufferWidth, int framebufferHeight, std::vector<UiQuadInstanceData>& quads) {
    const float width = static_cast<float>(std::max(1, framebufferWidth));
    const float height = static_cast<float>(std::max(1, framebufferHeight));
    const float clampedProgress = std::max(0.0f, std::min(progress, 1.0f));
    const float barWidth = std::max(180.0f, std::min(560.0f, width - 96.0f));
    const float barHeight = 20.0f;
    const float barX = (width - barWidth) * 0.5f;
    float barY = height * 0.75f;
    if (barY + 96.0f > height) {
        barY = std::max(42.0f, height - 96.0f);
    }

    RendererAddUiQuad(quads, 0.0f, 0.0f, width, height, RendererColor(0.018f, 0.025f, 0.030f, 1.0f));
    RendererAddUiQuad(quads, 0.0f, 0.0f, width, 4.0f, RendererColor(0.18f, 0.33f, 0.30f, 0.70f));
    RendererAddUiQuad(quads, 0.0f, height - 4.0f, width, 4.0f, RendererColor(0.18f, 0.33f, 0.30f, 0.50f));
    RendererAddUiQuad(quads, barX - 2.0f, barY - 2.0f, barWidth + 4.0f, barHeight + 4.0f, RendererColor(0.36f, 0.48f, 0.44f, 0.95f));
    RendererAddUiQuad(quads, barX, barY, barWidth, barHeight, RendererColor(0.055f, 0.070f, 0.076f, 0.98f));
    RendererAddUiQuad(quads, barX + 3.0f, barY + 3.0f, (barWidth - 6.0f) * clampedProgress, barHeight - 6.0f, RendererColor(0.24f, 0.62f, 0.50f, 0.96f));
    RendererBuildTextQuadsInRect("PROJECT PRIME", 0.0f, std::max(12.0f, barY - 74.0f), width, 28.0f, RendererColor(0.76f, 0.89f, 0.84f, 1.0f), true, quads);
    RendererBuildTextQuadsInRect(label.empty() ? "Loading" : label, 0.0f, std::max(12.0f, barY - 34.0f), width, 22.0f, RendererColor(0.90f, 0.96f, 0.93f, 1.0f), true, quads);
}
