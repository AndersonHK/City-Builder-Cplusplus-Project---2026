#include "RendererAlgorithms.h"
#include "RendererColor.h"
#include "RciTool.h"
#include "SimulationDate.h"
#include "SimulationTime.h"
#include "TransportTypes.h"
#include "VulkanRendererSupport.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
class TestRunner {
public:
    TestRunner()
        : failures_(0),
          checks_(0) {
    }

    void expect(bool condition, const std::string& message) {
        ++checks_;
        if (condition) {
            return;
        }

        ++failures_;
        std::cout << "FAIL: " << message << std::endl;
    }

    int finish() const {
        if (failures_ == 0) {
            std::cout << "Renderer tests passed: " << checks_ << " checks." << std::endl;
            return 0;
        }

        std::cout << "Renderer tests failed: " << failures_ << " of " << checks_ << " checks." << std::endl;
        return 1;
    }

private:
    int failures_;
    int checks_;
};

bool AlmostEqual(float left, float right) {
    return std::fabs(left - right) < 0.001f;
}

bool RectsIntersect(int leftMinX, int leftMinY, int leftMaxX, int leftMaxY, int rightMinX, int rightMinY, int rightMaxX, int rightMaxY) {
    return leftMinX <= rightMaxX &&
        leftMaxX >= rightMinX &&
        leftMinY <= rightMaxY &&
        leftMaxY >= rightMinY;
}

bool RciRoadFootprintIntersectsLot(const RciRoadPlan& roadPlan, const RciLot& lot, int roadFootprint) {
    int roadMinX = std::min(roadPlan.startTileX, roadPlan.endTileX);
    int roadMaxX = std::max(roadPlan.startTileX, roadPlan.endTileX);
    int roadMinY = std::min(roadPlan.startTileY, roadPlan.endTileY);
    int roadMaxY = std::max(roadPlan.startTileY, roadPlan.endTileY);

    if (roadPlan.startTileX == roadPlan.endTileX) {
        roadMaxX = roadMinX + roadFootprint - 1;
    } else if (roadPlan.startTileY == roadPlan.endTileY) {
        roadMaxY = roadMinY + roadFootprint - 1;
    }

    return RectsIntersect(
        roadMinX,
        roadMinY,
        roadMaxX,
        roadMaxY,
        lot.rect.minTileX,
        lot.rect.minTileY,
        lot.rect.maxTileX,
        lot.rect.maxTileY);
}

void RciRoadFootprintBounds(const RciRoadPlan& roadPlan, int roadFootprint, int& minX, int& minY, int& maxX, int& maxY) {
    minX = std::min(roadPlan.startTileX, roadPlan.endTileX);
    maxX = std::max(roadPlan.startTileX, roadPlan.endTileX);
    minY = std::min(roadPlan.startTileY, roadPlan.endTileY);
    maxY = std::max(roadPlan.startTileY, roadPlan.endTileY);
    if (roadPlan.startTileX == roadPlan.endTileX) {
        maxX = minX + roadFootprint - 1;
    } else if (roadPlan.startTileY == roadPlan.endTileY) {
        maxY = minY + roadFootprint - 1;
    }
}

bool RciRoadFootprintsIntersect(const RciRoadPlan& left, const RciRoadPlan& right, int roadFootprint) {
    int leftMinX = 0;
    int leftMinY = 0;
    int leftMaxX = 0;
    int leftMaxY = 0;
    int rightMinX = 0;
    int rightMinY = 0;
    int rightMaxX = 0;
    int rightMaxY = 0;
    RciRoadFootprintBounds(left, roadFootprint, leftMinX, leftMinY, leftMaxX, leftMaxY);
    RciRoadFootprintBounds(right, roadFootprint, rightMinX, rightMinY, rightMaxX, rightMaxY);
    return RectsIntersect(leftMinX, leftMinY, leftMaxX, leftMaxY, rightMinX, rightMinY, rightMaxX, rightMaxY);
}

int RciContextIndex(const RciPlanningContext& context, int tileX, int tileY) {
    return (tileY * context.mapWidth) + tileX;
}

RciPlanningContext MakeRciPlanningContext(int mapWidth, int mapHeight, const RciRect& bounds, RciPlanMode mode) {
    RciPlanningContext context;
    context.mapWidth = mapWidth;
    context.mapHeight = mapHeight;
    context.bounds = bounds;
    context.mode = mode;
    const std::size_t totalTiles = static_cast<std::size_t>(mapWidth) * static_cast<std::size_t>(mapHeight);
    context.paintableTiles.assign(totalTiles, 0u);
    context.groundRoadTiles.assign(totalTiles, 0u);
    context.groundRoadAxisMasks.assign(totalTiles, 0u);
    return context;
}

void SetRciContextPaintable(RciPlanningContext& context, const RciRect& rect, bool paintable) {
    int tileY = rect.minTileY;
    for (; tileY <= rect.maxTileY; ++tileY) {
        int tileX = rect.minTileX;
        for (; tileX <= rect.maxTileX; ++tileX) {
            if (tileX < 0 || tileY < 0 || tileX >= context.mapWidth || tileY >= context.mapHeight) {
                continue;
            }

            context.paintableTiles[static_cast<std::size_t>(RciContextIndex(context, tileX, tileY))] = paintable ? 1u : 0u;
        }
    }
}

void SetRciContextGroundRoad(RciPlanningContext& context, const RciRect& rect, RoadAxis axis) {
    int tileY = rect.minTileY;
    for (; tileY <= rect.maxTileY; ++tileY) {
        int tileX = rect.minTileX;
        for (; tileX <= rect.maxTileX; ++tileX) {
            if (tileX < 0 || tileY < 0 || tileX >= context.mapWidth || tileY >= context.mapHeight) {
                continue;
            }

            const std::size_t index = static_cast<std::size_t>(RciContextIndex(context, tileX, tileY));
            context.groundRoadTiles[index] = 1u;
            context.groundRoadAxisMasks[index] = AxisMaskFor(axis);
            context.paintableTiles[index] = 0u;
        }
    }
}

bool PlanHasHorizontalRoadAt(const RciPlan& plan, int tileY) {
    std::size_t roadIndex = 0u;
    for (; roadIndex < plan.roadPlans.size(); ++roadIndex) {
        const RciRoadPlan& roadPlan = plan.roadPlans[roadIndex];
        if (roadPlan.startTileY == roadPlan.endTileY && roadPlan.startTileY == tileY) {
            return true;
        }
    }
    return false;
}

bool PlanHasVerticalRoadAt(const RciPlan& plan, int tileX) {
    std::size_t roadIndex = 0u;
    for (; roadIndex < plan.roadPlans.size(); ++roadIndex) {
        const RciRoadPlan& roadPlan = plan.roadPlans[roadIndex];
        if (roadPlan.startTileX == roadPlan.endTileX && roadPlan.startTileX == tileX) {
            return true;
        }
    }
    return false;
}

bool AnyLotIntersectsRect(const RciPlan& plan, const RciRect& rect) {
    std::size_t lotIndex = 0u;
    for (; lotIndex < plan.lots.size(); ++lotIndex) {
        if (plan.lots[lotIndex].rect.intersects(rect)) {
            return true;
        }
    }
    return false;
}

void TestTileStatePacking(TestRunner& runner) {
    runner.expect(RendererPackTileStateScalar(0) == 0, "zero scalar packs to zero");
    runner.expect(RendererPackTileStateScalar(kSimulationStatDisplayCap) == kRendererSignedScalarPayloadMaxValue, "positive full-scale scalar packs to max snorm");
    runner.expect(RendererPackTileStateScalar(kSimulationStatDisplayCap * 2) == kRendererSignedScalarPayloadMaxValue, "positive over-scale scalar clamps to max snorm");
    runner.expect(RendererPackTileStateScalar(-kSimulationStatDisplayCap) == kRendererSignedScalarPayloadMinValue, "negative full-scale scalar packs to min snorm");
    runner.expect(RendererPackTileStateScalar(-kSimulationStatDisplayCap * 2) == kRendererSignedScalarPayloadMinValue, "negative over-scale scalar clamps to min snorm");
    runner.expect(RendererPackTileStateScalar(kSimulationStatDisplayCap / 2) == ((kRendererSignedScalarPayloadMaxValue + 1) / 2), "half-scale scalar rounds consistently");
}

void TestTileStateChunkPacking(TestRunner& runner) {
    std::vector<Tile> tiles(16);
    tiles[5].airPollution = kSimulationStatDisplayCap;
    tiles[5].parkEffect = -kSimulationStatDisplayCap;
    tiles[6].airPollution = kSimulationStatDisplayCap / 2;
    tiles[6].parkEffect = 0;
    tiles[9].airPollution = -kSimulationStatDisplayCap / 2;
    tiles[9].parkEffect = kSimulationStatDisplayCap;
    tiles[10].airPollution = 0;
    tiles[10].parkEffect = kSimulationStatDisplayCap / 2;

    ChunkRect chunk;
    chunk.startX = 1;
    chunk.startY = 1;
    chunk.width = 2;
    chunk.height = 2;

    std::vector<RendererSignedScalarPayload> pixels;
    RendererFillTileStateChunkPixels(tiles, 4, chunk, pixels);

    runner.expect(pixels.size() == 8u, "tile-state chunk writes two channels per tile");
    runner.expect(pixels[0] == kRendererSignedScalarPayloadMaxValue && pixels[1] == kRendererSignedScalarPayloadMinValue, "first chunk tile packs pollution and park effect");
    runner.expect(pixels[2] == ((kRendererSignedScalarPayloadMaxValue + 1) / 2) && pixels[3] == 0, "second chunk tile preserves row-major order");
    runner.expect(pixels[4] == -((kRendererSignedScalarPayloadMaxValue + 1) / 2) && pixels[5] == kRendererSignedScalarPayloadMaxValue, "third chunk tile packs negative half-scale values");
    runner.expect(pixels[6] == 0 && pixels[7] == ((kRendererSignedScalarPayloadMaxValue + 1) / 2), "fourth chunk tile completes row-major packing");
}

void TestTileLiftChunkPacking(TestRunner& runner) {
    std::vector<int> occupancy(16, -1);
    occupancy[5] = 10;
    occupancy[10] = 12;

    ChunkRect chunk;
    chunk.startX = 1;
    chunk.startY = 1;
    chunk.width = 2;
    chunk.height = 2;

    std::vector<std::uint8_t> pixels;
    RendererFillTileLiftChunkPixels(occupancy, 4, chunk, pixels);

    runner.expect(pixels.size() == 4u, "lift chunk writes one byte per tile");
    runner.expect(pixels[0] == 32u, "occupied tile uses shallow lift mask");
    runner.expect(pixels[1] == 0u, "empty tile keeps zero lift mask");
    runner.expect(pixels[2] == 0u, "empty row-major tile keeps zero lift mask");
    runner.expect(pixels[3] == 32u, "occupied row-major tile uses shallow lift mask");
}

void TestRendererPayloadPacking(TestRunner& runner) {
    runner.expect(RendererPackCappedStatToScalarPayload(0, kSimulationStatDisplayCap) == 0u, "scalar stat payload maps display minimum to zero");
    runner.expect(RendererPackCappedStatToScalarPayload(kSimulationStatDisplayCap, kSimulationStatDisplayCap) == kRendererScalarPayloadMaxValue, "scalar stat payload maps the display cap to full scale");
    runner.expect(RendererPackCappedStatToScalarPayload(-1, kSimulationStatDisplayCap) == 0u, "scalar stat payload clamps negative values");
    runner.expect(RendererPackCappedStatToScalarPayload(kSimulationStatDisplayCap * 2, kSimulationStatDisplayCap) == kRendererScalarPayloadMaxValue, "scalar stat payload clamps above the stat cap");
    runner.expect(RendererPackCappedStatToScalarPayload(kSimulationStatDisplayCap / 2, kSimulationStatDisplayCap) == ((kRendererScalarPayloadMaxValue + 1u) / 2u), "scalar stat payload preserves capped-stat midpoints");
    runner.expect(RendererPackRatioToScalarPayload(1u, 2u) == ((kRendererScalarPayloadMaxValue + 1u) / 2u), "scalar ratio payload rounds with integer arithmetic");
    runner.expect(RendererPackCappedStatToScalarPayload(kRciDesirabilityDisplayMinimum, kRciDesirabilityDisplayCap) == 0u, "desirability payload maps minimum desirability to zero");
    runner.expect(RendererPackCappedStatToScalarPayload(kRciDesirabilityDisplayCap, kRciDesirabilityDisplayCap) == kRendererScalarPayloadMaxValue, "desirability payload maps cap desirability to full scale");

    const RendererScalarPayload emptyTraffic = RendererPackTrafficOverlayPayload(false, 10u, 10u);
    runner.expect(!RendererTrafficOverlayPayloadIsRelevant(emptyTraffic), "traffic payload keeps empty tiles irrelevant");

    const RendererScalarPayload loadedTraffic = RendererPackTrafficOverlayPayload(true, 3u, 4u);
    runner.expect(RendererTrafficOverlayPayloadIsRelevant(loadedTraffic), "traffic payload records road relevance");
    runner.expect(RendererTrafficOverlayPayloadUtilizationValue(loadedTraffic) == RendererPackRatioToTrafficUtilizationPayload(3u, 4u), "traffic payload stores fixed-ratio utilization without RGBA color");
    runner.expect(RendererPackRatioToTrafficUtilizationPayload(1u, 1u) == kRendererTrafficOverlayUtilizationMask, "traffic utilization maps capacity to full 15-bit scale");
}

void TestOverlayGradientDirections(TestRunner& runner) {
    runner.expect(RendererOverlayGradientDirectionForSemantic(RendererOverlaySemantic::TrafficCapacity) == RendererOverlayGradientDirection::GoodToBad, "traffic utilization rises from good to bad");
    runner.expect(RendererOverlayGradientDirectionForSemantic(RendererOverlaySemantic::AirPollution) == RendererOverlayGradientDirection::GoodToBad, "air pollution rises from good to bad");
    runner.expect(RendererOverlayGradientDirectionForSemantic(RendererOverlaySemantic::LandValue) == RendererOverlayGradientDirection::BadToGood, "land value rises from bad to good");
    runner.expect(RendererOverlayGradientDirectionForSemantic(RendererOverlaySemantic::ParkEffect) == RendererOverlayGradientDirection::BadToGood, "park effect rises from bad to good");
    runner.expect(RendererOverlayGradientDirectionForSemantic(RendererOverlaySemantic::RciDesirability) == RendererOverlayGradientDirection::BadToGood, "RCI desirability rises from bad to good");
    runner.expect(RendererOverlaySemanticIndex(RendererOverlaySemantic::TrafficCapacity) == 0, "traffic semantic index remains shader-compatible");
    runner.expect(RendererOverlaySemanticIndex(RendererOverlaySemantic::AirPollution) == 3, "air pollution semantic index remains shader-compatible");
    runner.expect(RendererOverlaySemanticIndex(RendererOverlaySemantic::ParkEffect) == 4, "park effect semantic index remains shader-compatible");
    runner.expect(RendererOverlayGradientDirectionIndex(RendererOverlayGradientDirection::BadToGood) == 1, "bad-to-good direction index remains shader-compatible");
}

void TestZoningOverlayChunkPacking(TestRunner& runner) {
    std::vector<Tile> tiles(16);
    tiles[5].zoningType = TileZoningResidentialHigh;
    tiles[6].zoningType = TileZoningIndustrial;
    tiles[9].zoningType = TileZoningResidentialLow;

    ChunkRect chunk;
    chunk.startX = 1;
    chunk.startY = 1;
    chunk.width = 2;
    chunk.height = 2;

    std::vector<RendererScalarPayload> values;
    RendererFillZoningOverlayChunkValues(tiles, 4, chunk, values);

    runner.expect(values.size() == 4u, "zoning overlay writes one semantic value per tile");
    runner.expect(values[0] == TileZoningResidentialHigh, "high density residential zoning keeps its semantic id");
    runner.expect(values[1] == TileZoningIndustrial, "industrial zoning keeps its semantic id");
    runner.expect(values[2] == TileZoningResidentialLow, "low density residential zoning keeps its semantic id");
    runner.expect(values[3] == TileZoningNone, "un-zoned tiles keep zero payload");
}

void TestLandValueOverlayChunkPacking(TestRunner& runner) {
    std::vector<Tile> tiles(16);
    tiles[5].landValue = kLandValueDisplayMinimum;
    tiles[6].landValue = kLandValueDisplayCap / 2;
    tiles[9].landValue = kLandValueDisplayCap;
    tiles[10].landValue = kLandValueDisplayCap * 2;

    ChunkRect chunk;
    chunk.startX = 1;
    chunk.startY = 1;
    chunk.width = 2;
    chunk.height = 2;

    std::vector<RendererScalarPayload> values;
    RendererFillLandValueOverlayChunkValues(tiles, 4, chunk, values);

    runner.expect(values.size() == 4u, "land value overlay writes one scalar value per tile");
    runner.expect(values[0] == 0u, "land value minimum renders at the bottom of the gradient");
    runner.expect(values[1] == RendererPackCappedStatToScalarPayload(kLandValueDisplayCap / 2, kSimulationStatDisplayCap), "half-cap land value renders at the gradient midpoint");
    runner.expect(values[2] == kRendererScalarPayloadMaxValue, "land value cap renders at the top of the gradient");
    runner.expect(values[3] == kRendererScalarPayloadMaxValue, "land value above the cap remains at the top of the gradient");
}

void TestAirPollutionOverlayChunkPacking(TestRunner& runner) {
    std::vector<Tile> tiles(16);
    tiles[5].airPollution = 0;
    tiles[6].airPollution = kSimulationStatDisplayCap / 2;
    tiles[9].airPollution = kSimulationStatDisplayCap;
    tiles[10].airPollution = kSimulationStatDisplayCap * 2;

    ChunkRect chunk;
    chunk.startX = 1;
    chunk.startY = 1;
    chunk.width = 2;
    chunk.height = 2;

    std::vector<RendererScalarPayload> values;
    RendererFillAirPollutionOverlayChunkValues(tiles, 4, chunk, values);

    runner.expect(values.size() == 4u, "air pollution overlay writes one scalar value per tile");
    runner.expect(values[0] == 0u, "zero air pollution renders at the bottom of the gradient");
    runner.expect(values[1] == RendererPackCappedStatToScalarPayload(kSimulationStatDisplayCap / 2, kSimulationStatDisplayCap), "half-cap air pollution renders at the gradient midpoint");
    runner.expect(values[2] == kRendererScalarPayloadMaxValue, "air pollution cap renders at the top of the gradient");
    runner.expect(values[3] == kRendererScalarPayloadMaxValue, "air pollution above the cap remains at the top of the gradient");
}

void TestParkEffectOverlayChunkPacking(TestRunner& runner) {
    std::vector<Tile> tiles(16);
    tiles[5].parkEffect = 0;
    tiles[6].parkEffect = kSimulationStatDisplayCap / 2;
    tiles[9].parkEffect = kSimulationStatDisplayCap;
    tiles[10].parkEffect = kSimulationStatDisplayCap * 2;

    ChunkRect chunk;
    chunk.startX = 1;
    chunk.startY = 1;
    chunk.width = 2;
    chunk.height = 2;

    std::vector<RendererScalarPayload> values;
    RendererFillParkEffectOverlayChunkValues(tiles, 4, chunk, values);

    runner.expect(values.size() == 4u, "park effect overlay writes one scalar value per tile");
    runner.expect(values[0] == 0u, "zero park effect renders at the bottom of the gradient");
    runner.expect(values[1] == RendererPackCappedStatToScalarPayload(kSimulationStatDisplayCap / 2, kSimulationStatDisplayCap), "half-cap park effect renders at the gradient midpoint");
    runner.expect(values[2] == kRendererScalarPayloadMaxValue, "park effect cap renders at the top of the gradient");
    runner.expect(values[3] == kRendererScalarPayloadMaxValue, "park effect above the cap remains at the top of the gradient");
}

void TestRendererColorContract(TestRunner& runner) {
    runner.expect(AlmostEqual(RendererSrgbToLinear(0.0f), 0.0f), "sRGB zero converts to linear zero");
    runner.expect(RendererSrgbToLinear(0.5f) > 0.21f && RendererSrgbToLinear(0.5f) < 0.22f, "sRGB midpoint converts to scene-linear value");
    runner.expect(AlmostEqual(RendererLinearToSrgb(1.0f), 1.0f), "linear one converts to sRGB one");

    const LinearColor authored = RendererColorFromSrgb(0.5f, 1.0f, 0.0f, 0.25f);
    runner.expect(authored.r > 0.21f && authored.r < 0.22f && AlmostEqual(authored.g, 1.0f), "authored presentation colors convert into scene-linear HDR color");
    runner.expect(RendererToneMapSdr(LinearColor(3.0f, 1.0f, 0.0f, 1.5f)).r < 1.0f, "SDR tone map compresses HDR channel before presentation");
    runner.expect(RendererEncodePqFromNits(1000.0f) > RendererEncodePqFromNits(100.0f), "HDR10 PQ encode is monotonic over display nits");
    runner.expect(RendererChooseOutputMode(true, true) == RendererOutputMode::HdrScRgbFp16, "output preference chooses FP16 scRGB first");
    runner.expect(RendererChooseOutputMode(false, true) == RendererOutputMode::Hdr10Rgb10A2, "output preference chooses HDR10 second");
    runner.expect(RendererChooseOutputMode(false, false) == RendererOutputMode::SdrSrgb8, "output preference chooses SDR only when HDR modes are unavailable");
}

void TestVulkanSwapchainFormatPreference(TestRunner& runner) {
    std::vector<VkSurfaceFormatKHR> allFormats;
    allFormats.push_back({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
    allFormats.push_back({VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT});
    allFormats.push_back({VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT});

    VulkanSwapchainFormatSelection selection = VulkanChooseSwapchainFormat(allFormats, true);
    runner.expect(selection.supported && selection.outputMode == RendererOutputMode::HdrScRgbFp16, "Vulkan output prefers FP16 scRGB when available");
    runner.expect(selection.format == kVulkanSceneColorFormat, "Vulkan scRGB output uses the HDR scene format at the swapchain boundary");

    std::vector<VkSurfaceFormatKHR> hdr10AndSdr;
    hdr10AndSdr.push_back({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
    hdr10AndSdr.push_back({VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT});

    selection = VulkanChooseSwapchainFormat(hdr10AndSdr, true);
    runner.expect(selection.supported && selection.outputMode == RendererOutputMode::Hdr10Rgb10A2, "Vulkan output falls back to HDR10 before SDR");

    std::vector<VkSurfaceFormatKHR> sdrOnly;
    sdrOnly.push_back({VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});

    selection = VulkanChooseSwapchainFormat(sdrOnly, true);
    runner.expect(selection.supported && selection.outputMode == RendererOutputMode::SdrSrgb8, "Vulkan output can select SDR only when explicitly allowed");

    selection = VulkanChooseSwapchainFormat(sdrOnly, false);
    runner.expect(!selection.supported, "Vulkan output rejects silent SDR fallback when HDR is required");

    std::vector<VkSurfaceFormatKHR> undefinedSdr;
    undefinedSdr.push_back({VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});

    selection = VulkanChooseSwapchainFormat(undefinedSdr, false);
    runner.expect(!selection.supported, "Vulkan output does not infer HDR from undefined SDR color space");

    selection = VulkanChooseSwapchainFormat(undefinedSdr, true);
    runner.expect(selection.supported && selection.outputMode == RendererOutputMode::SdrSrgb8, "Vulkan output treats undefined SDR format as SDR only when allowed");

    std::vector<VkSurfaceFormatKHR> undefinedScRgb;
    undefinedScRgb.push_back({VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT});

    selection = VulkanChooseSwapchainFormat(undefinedScRgb, false);
    runner.expect(selection.supported && selection.outputMode == RendererOutputMode::HdrScRgbFp16, "Vulkan output accepts undefined format only with explicit scRGB color space");
}

void TestUtf8Decoder(TestRunner& runner) {
    std::string text;
    text.push_back('A');
    text.push_back(static_cast<char>(0xC3));
    text.push_back(static_cast<char>(0xA9));
    text.push_back(static_cast<char>(0xFF));

    std::size_t byteIndex = 0;
    std::uint32_t codepoint = 0u;
    runner.expect(RendererNextUtf8Codepoint(text, byteIndex, codepoint) && codepoint == 'A', "ASCII codepoint decodes");
    runner.expect(RendererNextUtf8Codepoint(text, byteIndex, codepoint) && codepoint == 0xE9u, "two-byte UTF-8 codepoint decodes");
    runner.expect(RendererNextUtf8Codepoint(text, byteIndex, codepoint) && codepoint == '?', "invalid UTF-8 byte falls back to question mark");
    runner.expect(!RendererNextUtf8Codepoint(text, byteIndex, codepoint), "decoder stops at end of string");
}

void TestSimulationDateCalculation(TestRunner& runner) {
    SimulationDateSettings dateSettings;
    runner.expect(SimulationTime::ticksPerDay() == 4u, "simulation days are four ticks long");
    runner.expect(SimulationTime::daysToTicks(31u) == 124u, "logical days convert to ticks");
    runner.expect(SimulationTime::tickToDay(127u) == 31u, "tick converts back to whole elapsed day");

    SimulationDate startDate = CalculateSimulationDate(0u);
    runner.expect(startDate.year == 1900 && startDate.month == 1 && startDate.day == 1, "tick zero starts on January 1 1900");

    SimulationDate firstHalfTickDate = CalculateSimulationDate(1u);
    runner.expect(firstHalfTickDate.year == 1900 && firstHalfTickDate.month == 1 && firstHalfTickDate.day == 1, "first tick stays on January 1 with four ticks per day");

    SimulationDate februaryDate = CalculateSimulationDate(SimulationTime::daysToTicks(31u));
    runner.expect(februaryDate.year == 1900 && februaryDate.month == 2 && februaryDate.day == 1, "day 31 advances to February 1 1900");
    runner.expect(FormatSimulationDateForTick(SimulationTime::daysToTicks(31u), dateSettings) == "1900/02/01", "formatted simulation date uses logical days");

    SimulationDate nextYearDate = CalculateSimulationDate(SimulationTime::daysToTicks(365u));
    runner.expect(nextYearDate.year == 1901 && nextYearDate.month == 1 && nextYearDate.day == 1, "1900 is not treated as a leap year");

    SimulationDate leapDate = CalculateSimulationDate(SimulationTime::daysToTicks(1519u));
    runner.expect(leapDate.year == 1904 && leapDate.month == 2 && leapDate.day == 29, "1904 leap day is reachable");
    runner.expect(FormatSimulationDate(leapDate, dateSettings) == "1904/02/29", "default simulation date formats as YYYY/MM/DD");

    dateSettings.format = SimulationDateFormat::MonthDayYear;
    runner.expect(FormatSimulationDate(leapDate, dateSettings) == "02/29/1904", "simulation date supports MM/DD/YYYY");

    dateSettings.format = SimulationDateFormat::DayMonthYear;
    runner.expect(FormatSimulationDate(leapDate, dateSettings) == "29/02/1904", "simulation date supports DD/MM/YYYY");
}

void TestWindowQuads(TestRunner& runner) {
    const char* filePath = "window_layout_test.xml";
    {
        std::ofstream file(filePath, std::ios::out | std::ios::trunc);
        file << "<window id=\"lot_query\" x=\"24\" y=\"96\" width=\"440\" height=\"220\" margin=\"16\" spacing=\"4\" hugElements=\"true\">"
            << "<textField id=\"title\" height=\"18\" />"
            << "<textField id=\"line0\" height=\"18\" />"
            << "</window>";
    }

    InGameWindow window;
    runner.expect(window.loadFromXmlFile(filePath), "query window XML fixture loads");
    window.setVisible(false);
    window.setText("title", "House");
    window.updateLayout();
    runner.expect(RendererBuildWindowQuads(window).empty(), "hidden window produces no UI quads");

    window.setVisible(true);
    std::vector<UiQuadInstanceData> quads = RendererBuildWindowQuads(window);
    runner.expect(quads.size() > 5u, "visible window produces frame and text quads");
    runner.expect(AlmostEqual(quads[0].x, 24.0f) && AlmostEqual(quads[0].y, 96.0f), "window background starts at fallback origin");
    runner.expect(AlmostEqual(quads[0].width, 440.0f), "window background uses fallback width");
    runner.expect(quads[0].height > 30.0f && quads[0].height < 80.0f, "hugging window height follows visible text");
    runner.expect(AlmostEqual(quads[0].colorA, 0.90f), "window background alpha is stable");

    bool allTextQuadsInsideWindow = true;
    std::size_t quadIndex = 5u;
    for (; quadIndex < quads.size(); ++quadIndex) {
        const UiQuadInstanceData& quad = quads[quadIndex];
        if (quad.x < quads[0].x || quad.y < quads[0].y ||
            quad.x + quad.width > quads[0].x + quads[0].width ||
            quad.y + quad.height > quads[0].y + quads[0].height) {
            allTextQuadsInsideWindow = false;
            break;
        }
    }
    runner.expect(allTextQuadsInsideWindow, "text quads stay inside the query window frame");

    std::remove(filePath);
}

void TestHudTextQuads(TestRunner& runner) {
    std::vector<UiQuadInstanceData> quads;
    RendererAppendTextQuads("Population: 12,345", 0.0f, 0.0f, 240.0f, 24.0f, UiColor(1.0f, 1.0f, 1.0f, 1.0f), false, quads);
    runner.expect(!quads.empty(), "HUD text helper emits bitmap glyph quads");
}

void TestLoadingScreenQuads(TestRunner& runner) {
    std::vector<UiQuadInstanceData> quads;
    RendererAppendLoadingScreenQuads("Loading city", 0.5f, 1024, 768, quads);
    runner.expect(quads.size() > 20u, "loading screen emits background, bar, and text quads");
    runner.expect(AlmostEqual(quads[0].x, 0.0f) && AlmostEqual(quads[0].y, 0.0f), "loading screen background starts at framebuffer origin");
    runner.expect(AlmostEqual(quads[0].width, 1024.0f) && AlmostEqual(quads[0].height, 768.0f), "loading screen background covers framebuffer");
    runner.expect(AlmostEqual(quads[3].x, 230.0f) && AlmostEqual(quads[3].y, 574.0f), "loading bar frame is horizontally centered near three quarter height");
    runner.expect(AlmostEqual(quads[5].width, 277.0f), "loading bar fill follows clamped progress");

    std::vector<UiQuadInstanceData> clampedQuads;
    RendererAppendLoadingScreenQuads("Saving city", 2.0f, 1024, 768, clampedQuads);
    runner.expect(clampedQuads.size() > 5u && AlmostEqual(clampedQuads[5].width, 554.0f), "loading bar progress clamps to full width");
}

void TestUiMenuQuadsAndHitTesting(TestRunner& runner) {
    const char* filePath = "ui_layout_test.xml";
    {
        std::ofstream file(filePath, std::ios::out | std::ios::trunc);
        file << "<ui>"
            << "<menu id=\"date_speed\" anchor=\"topLeft\" x=\"16\" y=\"48\" width=\"140\" height=\"28\" buttonWidth=\"32\" buttonHeight=\"28\" spacing=\"4\">"
            << "<button id=\"speed_pause\" icon=\"pause\" action=\"set_speed_paused\" x=\"0\" y=\"0\" />"
            << "<button id=\"speed_play\" icon=\"play\" action=\"set_speed_play\" x=\"36\" y=\"0\" />"
            << "<button id=\"speed_fast\" icon=\"fast\" action=\"set_speed_fast\" x=\"72\" y=\"0\" />"
            << "<button id=\"speed_fast_forward\" icon=\"fastForward\" action=\"set_speed_fast_forward\" x=\"108\" y=\"0\" />"
            << "</menu>"
            << "<menu id=\"side_tools\" parentMenu=\"menu_toggle\" parentButton=\"toggle_tools\" stack=\"away\" direction=\"up\" anchor=\"bottomLeft\" x=\"0\" bottom=\"72\" width=\"132\" buttonWidth=\"132\" buttonHeight=\"38\" spacing=\"8\" visible=\"true\">"
            << "<button id=\"bulldozer\" text=\"Bulldoze\" action=\"select_bulldozer\" />"
            << "<button id=\"street\" text=\"Street\" action=\"select_road_street\" />"
            << "<button id=\"road\" text=\"Road\" action=\"select_road_road\" />"
            << "<button id=\"one_way\" text=\"One-Way\" action=\"select_road_one_way\" />"
            << "<button id=\"avenue\" text=\"Avenue\" action=\"select_road_avenue\" />"
            << "<button id=\"query\" text=\"Query\" action=\"select_query\" />"
            << "<button id=\"rci_menu\" text=\"RCI\" action=\"toggle_rci_tool_menu\" />"
            << "</menu>"
            << "<menu id=\"rci_tools\" parentMenu=\"side_tools\" parentButton=\"rci_menu\" stack=\"centered\" direction=\"right\" anchor=\"bottomLeft\" x=\"16\" y=\"0\" bottom=\"16\" width=\"132\" buttonWidth=\"132\" buttonHeight=\"38\" spacing=\"8\" visible=\"false\">"
            << "<button id=\"rci_residential_low\" text=\"Low Res\" action=\"select_rci_residential_low\" />"
            << "<button id=\"rci_residential_high\" text=\"High Res\" action=\"select_rci_residential_high\" />"
            << "<button id=\"rci_industrial\" text=\"Industry\" action=\"select_rci_industrial\" />"
            << "<button id=\"rci_unzone\" text=\"Unzone\" action=\"select_rci_unzone\" />"
            << "</menu>"
            << "<menu id=\"side_overlays\" parentMenu=\"overlay_toggle\" parentButton=\"toggle_overlays\" stack=\"away\" direction=\"up\" anchor=\"bottomRight\" x=\"0\" bottom=\"72\" width=\"132\" buttonWidth=\"132\" buttonHeight=\"38\" spacing=\"8\" visible=\"true\">"
            << "<button id=\"overlay_traffic\" text=\"Traffic\" action=\"toggle_overlay_traffic\" />"
            << "<button id=\"overlay_land_value\" text=\"Land Value\" action=\"toggle_overlay_land_value\" />"
            << "<button id=\"overlay_air_pollution\" text=\"Air Poll.\" action=\"toggle_overlay_air_pollution\" />"
            << "<button id=\"overlay_rci\" text=\"RCI\" action=\"toggle_overlay_rci\" />"
            << "<button id=\"overlay_rci_desirability_menu\" text=\"RCI Desire\" action=\"toggle_rci_overlay_menu\" />"
            << "</menu>"
            << "<menu id=\"rci_desirability_overlays\" parentMenu=\"side_overlays\" parentButton=\"overlay_rci_desirability_menu\" stack=\"centered\" direction=\"left\" anchor=\"bottomRight\" x=\"16\" y=\"0\" bottom=\"16\" width=\"132\" buttonWidth=\"132\" buttonHeight=\"38\" spacing=\"8\" visible=\"false\">"
            << "<button id=\"overlay_desirability_low_wealth_residential\" text=\"Low Wealth Res\" action=\"toggle_overlay_desirability_low_wealth_residential\" />"
            << "<button id=\"overlay_desirability_dirty_industry\" text=\"Dirty Industry\" action=\"toggle_overlay_desirability_dirty_industry\" />"
            << "</menu>"
            << "<menu id=\"region_exit\" anchor=\"topLeft\" x=\"16\" y=\"16\" width=\"92\" height=\"40\" buttonWidth=\"92\" buttonHeight=\"40\" spacing=\"0\" visible=\"true\">"
            << "<button id=\"region_exit_game\" text=\"Exit\" action=\"open_exit_confirm\" x=\"0\" y=\"0\" width=\"92\" height=\"40\" />"
            << "</menu>"
            << "<menu id=\"escape_menu\" anchor=\"center\" x=\"0\" y=\"0\" width=\"220\" height=\"56\" buttonWidth=\"180\" buttonHeight=\"40\" spacing=\"0\" visible=\"false\" backgroundR=\"0.035\" backgroundG=\"0.047\" backgroundB=\"0.058\" backgroundA=\"0.94\">"
            << "<button id=\"exit_game\" text=\"Exit\" action=\"open_exit_confirm\" x=\"20\" y=\"8\" width=\"180\" height=\"40\" />"
            << "</menu>"
            << "<menu id=\"city_switch_confirm_dialog\" anchor=\"center\" x=\"0\" y=\"0\" width=\"340\" height=\"116\" buttonWidth=\"132\" buttonHeight=\"36\" spacing=\"8\" visible=\"false\" backgroundR=\"0.035\" backgroundG=\"0.047\" backgroundB=\"0.058\" backgroundA=\"0.96\">"
            << "<button id=\"city_switch_prompt\" text=\"Save city before leaving?\" x=\"18\" y=\"14\" width=\"304\" height=\"34\" />"
            << "<button id=\"city_switch_save_yes\" text=\"Yes\" action=\"city_switch_save_yes\" x=\"34\" y=\"66\" width=\"122\" height=\"36\" />"
            << "<button id=\"city_switch_save_no\" text=\"No\" action=\"city_switch_save_no\" x=\"184\" y=\"66\" width=\"122\" height=\"36\" />"
            << "</menu>"
            << "<menu id=\"menu_toggle\" anchor=\"bottomLeft\" x=\"16\" bottom=\"16\" width=\"92\" buttonWidth=\"92\" buttonHeight=\"40\" spacing=\"0\" visible=\"true\">"
            << "<button id=\"toggle_tools\" text=\"Tools\" action=\"toggle_side_menu\" />"
            << "</menu>"
            << "<menu id=\"overlay_toggle\" anchor=\"bottomRight\" x=\"16\" bottom=\"16\" width=\"132\" buttonWidth=\"132\" buttonHeight=\"40\" spacing=\"0\" visible=\"true\">"
            << "<button id=\"toggle_overlays\" text=\"Overlays\" action=\"toggle_overlay_menu\" />"
            << "</menu>"
            << "</ui>";
    }

    UiLayout layout;
    runner.expect(layout.loadFromXmlFile(filePath), "UI XML fixture loads menus");
    std::string action;
    std::vector<std::string> regionMenuIds;
    regionMenuIds.push_back("region_exit");
    runner.expect(layout.hitTestAction(24.0, 24.0, 1024, 768, regionMenuIds, action) && action == "open_exit_confirm", "region exit button hit tests in filtered region UI");
    runner.expect(layout.hitTestAction(24.0, 390.0, 1024, 768, action) && action == "select_bulldozer", "side-menu button hit tests by screen position");
    runner.expect(layout.hitTestAction(24.0, 664.0, 1024, 768, action) && action == "toggle_rci_tool_menu", "side-menu exposes RCI submenu button");
    layout.setMenuVisible("rci_tools", true);
    runner.expect(layout.hitTestAction(172.0, 733.0, 1024, 768, action) && action == "select_rci_unzone", "RCI child menu exposes unzone button");
    runner.expect(layout.hitTestAction(900.0, 484.0, 1024, 768, action) && action == "toggle_overlay_traffic", "overlay menu hit tests from the bottom-right");
    runner.expect(layout.hitTestAction(900.0, 576.0, 1024, 768, action) && action == "toggle_overlay_air_pollution", "air pollution overlay button hit tests from the bottom-right");
    runner.expect(layout.hitTestAction(900.0, 740.0, 1024, 768, action) && action == "toggle_overlay_menu", "bottom-right overlay menu toggle remains clickable");

    std::vector<UiQuadInstanceData> quads = RendererBuildUiMenuQuads(layout, 1024, 768, "select_road_street");
    runner.expect(!quads.empty(), "visible UI menus produce quads");
    runner.expect(AlmostEqual(quads[0].x, 16.0f), "side menu resolves to left edge");
    runner.expect(quads[0].colorA >= 0.0f, "menu background alpha is stable");
    UiRect rciMenuRect;
    runner.expect(layout.resolveMenuRect("rci_tools", 1024, 768, rciMenuRect) && rciMenuRect.x == 164 && rciMenuRect.y == 589, "centered-right RCI child menu resolves beside its parent button");
    layout.setMenuVisible("rci_desirability_overlays", true);
    UiRect rciOverlayMenuRect;
    runner.expect(layout.resolveMenuRect("rci_desirability_overlays", 1024, 768, rciOverlayMenuRect) && rciOverlayMenuRect.x == 728 && rciOverlayMenuRect.y == 635, "centered-left RCI overlay child menu resolves beside its parent button");
    layout.setMenuVisible("rci_desirability_overlays", false);

    layout.setMenuVisible("escape_menu", true);
    std::vector<UiResolvedButton> resolvedButtons;
    layout.resolveButtons(1024, 768, std::string(), resolvedButtons);
    bool foundCenteredExitButton = false;
    std::size_t buttonIndex = 0;
    for (; buttonIndex < resolvedButtons.size(); ++buttonIndex) {
        if (resolvedButtons[buttonIndex].action == "open_exit_confirm") {
            foundCenteredExitButton =
                resolvedButtons[buttonIndex].rect.x == 422 &&
                resolvedButtons[buttonIndex].rect.y == 364;
        }
    }
    runner.expect(foundCenteredExitButton, "center-anchored escape menu resolves around framebuffer center");
    runner.expect(layout.hitTestAction(512.0, 384.0, 1024, 768, action) && action == "open_exit_confirm", "centered escape menu button hit tests");
    std::vector<std::string> activeActions;
    std::vector<std::string> modalMenuIds;
    modalMenuIds.push_back("escape_menu");
    std::vector<UiQuadInstanceData> modalQuads = RendererBuildUiMenuQuads(layout, 1024, 768, activeActions, modalMenuIds);
    runner.expect(!modalQuads.empty() && AlmostEqual(modalQuads[0].x, 402.0f), "filtered UI menu rendering can draw only the centered modal menu");
    layout.setMenuVisible("escape_menu", false);

    layout.setMenuVisible("city_switch_confirm_dialog", true);
    modalMenuIds.clear();
    modalMenuIds.push_back("city_switch_confirm_dialog");
    std::vector<UiQuadInstanceData> switchQuads = RendererBuildUiMenuQuads(layout, 1024, 768, activeActions, modalMenuIds);
    runner.expect(!switchQuads.empty() && AlmostEqual(switchQuads[0].x, 342.0f), "city switch save dialog shares centered modal layout");
    runner.expect(layout.hitTestAction(388.0, 400.0, 1024, 768, modalMenuIds, action) && action == "city_switch_save_yes", "city switch save button hit tests through filtered UI");
    layout.setMenuVisible("city_switch_confirm_dialog", false);

    const std::size_t visibleQuadCount = quads.size();
    layout.toggleMenu("side_tools");
    std::vector<UiQuadInstanceData> hiddenQuads = RendererBuildUiMenuQuads(layout, 1024, 768, std::string());
    runner.expect(hiddenQuads.size() < visibleQuadCount, "hidden side menu removes its button quads");
    runner.expect(layout.hitTestAction(24.0, 740.0, 1024, 768, action) && action == "toggle_side_menu", "bottom-left menu toggle remains clickable");

    std::remove(filePath);
}

void TestUiButtonIconXmlAndRendering(TestRunner& runner) {
    const char* filePath = "ui_icon_test.xml";
    {
        std::ofstream file(filePath, std::ios::out | std::ios::trunc);
        file << "<ui><menu id=\"speed\" anchor=\"topLeft\" x=\"10\" y=\"12\" width=\"68\" height=\"28\" buttonWidth=\"32\" buttonHeight=\"28\" spacing=\"4\">"
            << "<button id=\"pause\" text=\"\" icon=\"pause\" action=\"set_speed_paused\" x=\"0\" y=\"0\" />"
            << "<button id=\"play\" text=\"\" icon=\"play\" action=\"set_speed_play\" x=\"36\" y=\"0\" />"
            << "</menu></ui>";
    }

    UiLayout layout;
    runner.expect(layout.loadFromXmlFile(filePath), "UI XML loads icon button declarations");

    std::vector<std::string> activeActions;
    activeActions.push_back("set_speed_play");
    std::vector<UiResolvedButton> buttons;
    layout.resolveButtons(320, 240, activeActions, buttons);
    runner.expect(buttons.size() == 2u, "icon button XML resolves both buttons");
    runner.expect(!buttons.empty() && buttons[0].icon == "pause", "button icon attribute is stored");
    runner.expect(buttons.size() > 1u && buttons[1].isActive, "active action vector highlights icon button");

    std::vector<UiQuadInstanceData> quads = RendererBuildUiMenuQuads(layout, 320, 240, activeActions);
    runner.expect(quads.size() > 10u, "icon buttons render bitmap icon quads");

    std::remove(filePath);
}

void TestRciToolXmlPlanning(TestRunner& runner) {
    const char* filePath = "rci_tool_planning_test.xml";
    {
        std::ofstream file(filePath, std::ios::out | std::ios::trunc);
        file << "<rci>"
            << "<zone id=\"residential_low\" name=\"Low Density Residence\" zoningType=\"residential_low\" labelStringId=\"zone.tool.residential_low\" minDepth=\"2\" preferredDepth=\"4\" maxDepth=\"8\" minWidth=\"2\" preferredWidth=\"16\" maxWidth=\"24\" colorR=\"0.44\" colorG=\"0.92\" colorB=\"0.46\" colorA=\"0.50\" />"
            << "<zone id=\"residential_high\" name=\"High Density Residence\" zoningType=\"residential_high\" labelStringId=\"zone.tool.residential_high\" minDepth=\"2\" preferredDepth=\"4\" maxDepth=\"8\" minWidth=\"2\" preferredWidth=\"16\" maxWidth=\"24\" colorR=\"0.10\" colorG=\"0.48\" colorB=\"0.20\" colorA=\"0.50\" />"
            << "<zone id=\"industrial\" name=\"Industry\" zoningType=\"industrial\" labelStringId=\"zone.tool.industrial\" minDepth=\"2\" preferredDepth=\"8\" maxDepth=\"8\" minWidth=\"2\" preferredWidth=\"16\" maxWidth=\"24\" colorR=\"0.92\" colorG=\"0.76\" colorB=\"0.15\" colorA=\"0.50\" />"
            << "<rciType id=\"low_wealth_residential\" name=\"Low Wealth Residential\" desirabilityOverlayStringId=\"overlay.desirability.low_wealth_residential\" demandParameterId=\"residents.low_wealth\" zoneTypes=\"low_density_residential,high_density_residential\" colorR=\"0.18\" colorG=\"0.72\" colorB=\"0.28\" colorA=\"0.50\" />"
            << "<rciType id=\"dirty_industry\" name=\"Dirty Industry\" desirabilityOverlayStringId=\"overlay.desirability.dirty_industry\" demandParameterId=\"jobs.dirty_industry\" zoneTypes=\"industrial\" colorR=\"0.92\" colorG=\"0.76\" colorB=\"0.15\" colorA=\"0.50\" />"
            << "</rci>";
    }

    RciToolCatalog catalog;
    runner.expect(catalog.loadFromXmlFile(filePath), "RCI planning catalog XML loads");
    const RciTool* residential = catalog.findTool("residential_high");
    const RciTool* residentialLow = catalog.findTool("residential_low");
    const RciTool* industrial = catalog.findTool("industrial");
    runner.expect(residential != 0 && residentialLow != 0 && industrial != 0, "RCI catalog exposes low residential, high residential, and industrial zone tools");
    if (residential == 0 || residentialLow == 0 || industrial == 0) {
        std::remove(filePath);
        return;
    }

    runner.expect(residentialLow->zoningType() == TileZoningResidentialLow, "low density residential zone uses distinct zoning type");
    runner.expect(residential->minDepth() == 2 && residential->preferredDepth() == 4 && residential->maxDepth() == 8, "residential RCI depth preferences match design");
    runner.expect(industrial->minDepth() == 2 && industrial->preferredDepth() == 8 && industrial->maxDepth() == 8, "industrial RCI depth preferences match design");

    RciPlan areaPlan;
    runner.expect(residential->buildPlan(10, 10, 17, 17, RciPlanMode::Area, 1024, 1024, areaPlan), "RCI area mode builds a plan");
    runner.expect(areaPlan.zoneRects.size() == 1u && areaPlan.lots.empty() && areaPlan.roadPlans.empty(), "RCI area mode keeps one filled zone rect");

    RciPlan lotsPlan;
    runner.expect(residential->buildPlan(0, 0, 15, 7, RciPlanMode::Lots, 1024, 1024, lotsPlan), "RCI lots mode builds a plan");
    runner.expect(!lotsPlan.lots.empty(), "RCI lots mode creates empty parcel lots");
    if (!lotsPlan.lots.empty()) {
        runner.expect(lotsPlan.lots[0].rect.width() == 2, "RCI lots prefer two-tile frontage");
        runner.expect(lotsPlan.lots[0].rect.height() == 4, "residential RCI lots prefer four-tile depth");
    }

    RciPlan residentialRoadPlan;
    runner.expect(residential->buildPlan(0, 0, 17, 9, RciPlanMode::LotsAndRoads, 1024, 1024, residentialRoadPlan), "RCI lots-and-roads mode builds a plan");
    runner.expect(!residentialRoadPlan.roadPlans.empty(), "RCI lots-and-roads mode includes planned roads");
    runner.expect(!residentialRoadPlan.lots.empty(), "RCI lots-and-roads mode includes planned lots");
    const int plannedRoadFootprint = 2;
    std::size_t roadIndex = 0;
    for (; roadIndex < residentialRoadPlan.roadPlans.size(); ++roadIndex) {
        const RciRoadPlan& roadPlan = residentialRoadPlan.roadPlans[roadIndex];
        if (roadPlan.startTileX == roadPlan.endTileX) {
            runner.expect(roadPlan.startTileX + plannedRoadFootprint - 1 <= residentialRoadPlan.bounds.maxTileX, "RCI vertical road footprint stays inside east edge");
        }
        if (roadPlan.startTileY == roadPlan.endTileY) {
            runner.expect(roadPlan.startTileY + plannedRoadFootprint - 1 <= residentialRoadPlan.bounds.maxTileY, "RCI horizontal road footprint stays inside south edge");
        }

        std::size_t lotIndex = 0;
        for (; lotIndex < residentialRoadPlan.lots.size(); ++lotIndex) {
            runner.expect(!RciRoadFootprintIntersectsLot(roadPlan, residentialRoadPlan.lots[lotIndex], plannedRoadFootprint), "RCI planned lots do not overlap two-tile road footprints");
        }
    }

    RciPlan industrialRoadPlan;
    runner.expect(industrial->buildPlan(0, 0, 19, 19, RciPlanMode::LotsAndRoads, 1024, 1024, industrialRoadPlan), "industrial RCI lots-and-roads mode builds a plan");
    if (!industrialRoadPlan.lots.empty()) {
        runner.expect(industrialRoadPlan.lots[0].rect.height() == 8, "industrial RCI lots prefer eight-tile depth");
    }

    RciPlanningContext shiftContext = MakeRciPlanningContext(20, 16, RciRect(2, 2, 13, 9), RciPlanMode::Lots);
    SetRciContextPaintable(shiftContext, shiftContext.bounds, true);
    const RciRect existingVerticalRoad(7, 2, 8, 9);
    const RciRect occupiedPhysicalCell(4, 4, 4, 4);
    SetRciContextGroundRoad(shiftContext, existingVerticalRoad, RoadAxis::Vertical);
    SetRciContextPaintable(shiftContext, occupiedPhysicalCell, false);
    RciPlan shiftRoadAwarePlan;
    runner.expect(residential->buildPlan(shiftContext, shiftRoadAwarePlan), "road-aware shift RCI plan builds around existing roads");
    runner.expect(shiftRoadAwarePlan.roadPlans.empty(), "shift RCI plan does not create new roads");
    runner.expect(!shiftRoadAwarePlan.lots.empty(), "shift RCI plan creates lots from paintable cells");
    runner.expect(!AnyLotIntersectsRect(shiftRoadAwarePlan, existingVerticalRoad), "shift RCI lots skip existing road footprint");
    runner.expect(!AnyLotIntersectsRect(shiftRoadAwarePlan, occupiedPhysicalCell), "shift RCI lots skip occupied physical cells");
    bool foundSideFacingLot = false;
    std::size_t shiftLotIndex = 0u;
    for (; shiftLotIndex < shiftRoadAwarePlan.lots.size(); ++shiftLotIndex) {
        foundSideFacingLot = foundSideFacingLot ||
            shiftRoadAwarePlan.lots[shiftLotIndex].frontDirection == kRoadDirectionEast ||
            shiftRoadAwarePlan.lots[shiftLotIndex].frontDirection == kRoadDirectionWest;
    }
    runner.expect(foundSideFacingLot, "shift RCI lots face the existing road separator");

    RciPlanningContext existingEdgeRoadContext = MakeRciPlanningContext(24, 20, RciRect(4, 4, 17, 11), RciPlanMode::LotsAndRoads);
    SetRciContextPaintable(existingEdgeRoadContext, existingEdgeRoadContext.bounds, true);
    SetRciContextGroundRoad(existingEdgeRoadContext, RciRect(4, 3, 17, 3), RoadAxis::Horizontal);
    RciPlan existingEdgeRoadPlan;
    runner.expect(residential->buildPlan(existingEdgeRoadContext, existingEdgeRoadPlan), "normal RCI plan builds beside an existing edge road");
    runner.expect(!existingEdgeRoadPlan.lots.empty(), "normal RCI plan uses existing edge frontage");
    runner.expect(!PlanHasHorizontalRoadAt(existingEdgeRoadPlan, 4), "normal RCI plan does not add a duplicate road beside existing edge road");

    RciPlanningContext snappingContext = MakeRciPlanningContext(40, 20, RciRect(4, 4, 33, 13), RciPlanMode::LotsAndRoads);
    SetRciContextPaintable(snappingContext, snappingContext.bounds, true);
    SetRciContextGroundRoad(snappingContext, RciRect(20, 1, 21, 3), RoadAxis::Vertical);
    RciPlan snappingPlan;
    runner.expect(residential->buildPlan(snappingContext, snappingPlan), "normal RCI plan builds with touching grid alignment");
    runner.expect(PlanHasVerticalRoadAt(snappingPlan, 20), "normal RCI plan snaps an oversized block split to the touching road corridor");

    RciPlanningContext boundedParcelContext = MakeRciPlanningContext(24, 24, RciRect(5, 5, 14, 14), RciPlanMode::LotsAndRoads);
    SetRciContextPaintable(boundedParcelContext, boundedParcelContext.bounds, true);
    SetRciContextGroundRoad(boundedParcelContext, RciRect(5, 4, 14, 4), RoadAxis::Horizontal);
    SetRciContextGroundRoad(boundedParcelContext, RciRect(5, 15, 14, 15), RoadAxis::Horizontal);
    RciPlan boundedParcelPlan;
    runner.expect(residential->buildPlan(boundedParcelContext, boundedParcelPlan), "road-bounded RCI block builds parcels");
    std::vector<std::uint8_t> parcelCoverage(100u, 0u);
    bool allBoundedLotsAreFiveDeep = true;
    std::size_t boundedLotIndex = 0u;
    for (; boundedLotIndex < boundedParcelPlan.lots.size(); ++boundedLotIndex) {
        const RciLot& lot = boundedParcelPlan.lots[boundedLotIndex];
        if (!lot.rect.intersects(boundedParcelContext.bounds)) {
            continue;
        }

        allBoundedLotsAreFiveDeep = allBoundedLotsAreFiveDeep && lot.rect.height() == 5;
        int y = lot.rect.minTileY;
        for (; y <= lot.rect.maxTileY; ++y) {
            int x = lot.rect.minTileX;
            for (; x <= lot.rect.maxTileX; ++x) {
                if (x >= boundedParcelContext.bounds.minTileX &&
                    x <= boundedParcelContext.bounds.maxTileX &&
                    y >= boundedParcelContext.bounds.minTileY &&
                    y <= boundedParcelContext.bounds.maxTileY) {
                    const int localX = x - boundedParcelContext.bounds.minTileX;
                    const int localY = y - boundedParcelContext.bounds.minTileY;
                    parcelCoverage[static_cast<std::size_t>((localY * boundedParcelContext.bounds.width()) + localX)] = 1u;
                }
            }
        }
    }
    runner.expect(allBoundedLotsAreFiveDeep, "road-bounded ten-deep residential block splits into five-deep parcels");
    bool allBoundedTilesCovered = true;
    std::size_t coveredIndex = 0u;
    for (; coveredIndex < parcelCoverage.size(); ++coveredIndex) {
        allBoundedTilesCovered = allBoundedTilesCovered && parcelCoverage[coveredIndex] != 0u;
    }
    runner.expect(allBoundedTilesCovered, "road-bounded residential parceling consumes the whole buildable block");

    RciPlanningContext emptyGridContext = MakeRciPlanningContext(64, 64, RciRect(8, 8, 55, 55), RciPlanMode::LotsAndRoads);
    SetRciContextPaintable(emptyGridContext, emptyGridContext.bounds, true);
    RciPlan emptyGridPlan;
    runner.expect(residential->buildPlan(emptyGridContext, emptyGridPlan), "empty normal RCI smart grid builds");
    bool everyHorizontalRoadOverlapsVerticalRoad = true;
    std::size_t horizontalRoadCount = 0u;
    std::size_t verticalRoadCount = 0u;
    std::size_t outerRoadIndex = 0u;
    for (; outerRoadIndex < emptyGridPlan.roadPlans.size(); ++outerRoadIndex) {
        const RciRoadPlan& horizontalRoad = emptyGridPlan.roadPlans[outerRoadIndex];
        if (horizontalRoad.startTileY != horizontalRoad.endTileY) {
            ++verticalRoadCount;
            continue;
        }

        ++horizontalRoadCount;
        bool overlapsVertical = false;
        std::size_t innerRoadIndex = 0u;
        for (; innerRoadIndex < emptyGridPlan.roadPlans.size(); ++innerRoadIndex) {
            const RciRoadPlan& verticalRoad = emptyGridPlan.roadPlans[innerRoadIndex];
            if (verticalRoad.startTileX == verticalRoad.endTileX &&
                RciRoadFootprintsIntersect(horizontalRoad, verticalRoad, plannedRoadFootprint)) {
                overlapsVertical = true;
                break;
            }
        }
        everyHorizontalRoadOverlapsVerticalRoad = everyHorizontalRoadOverlapsVerticalRoad && overlapsVertical;
    }
    runner.expect(horizontalRoadCount > 0u && verticalRoadCount > 0u, "empty normal RCI grid has both road axes");
    runner.expect(everyHorizontalRoadOverlapsVerticalRoad, "planned horizontal streets overlap vertical streets at junctions");

    std::remove(filePath);
}

void TestRciToolXmlLoading(TestRunner& runner) {
    const char* filePath = "rci_tool_test.xml";
    {
        std::ofstream file(filePath, std::ios::out | std::ios::trunc);
        file << "<rciTools><zone id=\"residential_high\" name=\"Homes\" zoningType=\"high_density_residential\" colorR=\"0.1\" colorG=\"0.8\" colorB=\"0.2\" colorA=\"0.5\" minDepth=\"2\" preferedDepth=\"5\" maxDepth=\"8\" minWidth=\"2\" preferedWidth=\"16\" maxWidth=\"24\" /><rciType id=\"low_wealth_residential\" desirabilityOverlayStringId=\"overlay.desirability.low_wealth_residential\" demandParameterId=\"residents.low_wealth\" zoneTypes=\"high_density_residential\" /></rciTools>";
    }

    RciToolCatalog catalog;
    const bool loaded = catalog.loadFromXmlFile(filePath);
    const RciTool* tool = catalog.findTool("residential_high");
    const RciType* rciType = catalog.findRciType("low_wealth_residential");
    runner.expect(loaded && tool != 0 && rciType != 0, "RCI XML catalog loads zone and RCI type definitions");
    if (tool != 0) {
        runner.expect(tool->name() == "Homes", "RCI XML zone name is stored");
        runner.expect(tool->zoningType() == TileZoningResidentialHigh, "RCI XML high-density residential zone type is stored");
        runner.expect(tool->preferredDepth() == 5 && tool->preferredWidth() == 16, "RCI XML accepts prefered spelling aliases");
    }
    if (rciType != 0) {
        runner.expect(rciType->allowsZoningType(TileZoningResidentialHigh), "RCI type records allowed zone types");
    }

    std::remove(filePath);
}
}

int main() {
    TestRunner runner;
    TestTileStatePacking(runner);
    TestTileStateChunkPacking(runner);
    TestTileLiftChunkPacking(runner);
    TestRendererPayloadPacking(runner);
    TestOverlayGradientDirections(runner);
    TestZoningOverlayChunkPacking(runner);
    TestLandValueOverlayChunkPacking(runner);
    TestAirPollutionOverlayChunkPacking(runner);
    TestParkEffectOverlayChunkPacking(runner);
    TestRendererColorContract(runner);
    TestVulkanSwapchainFormatPreference(runner);
    TestUtf8Decoder(runner);
    TestSimulationDateCalculation(runner);
    TestWindowQuads(runner);
    TestHudTextQuads(runner);
    TestLoadingScreenQuads(runner);
    TestUiMenuQuadsAndHitTesting(runner);
    TestUiButtonIconXmlAndRendering(runner);
    TestRciToolXmlPlanning(runner);
    TestRciToolXmlLoading(runner);
    return runner.finish();
}
