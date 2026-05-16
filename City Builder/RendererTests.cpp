#include "RendererAlgorithms.h"
#include "RciTool.h"
#include "SimulationDate.h"

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

void TestTileStatePacking(TestRunner& runner) {
    runner.expect(RendererPackTileStateScalar(0) == 0, "zero scalar packs to zero");
    runner.expect(RendererPackTileStateScalar(640000) == 32767, "positive full-scale scalar packs to max snorm");
    runner.expect(RendererPackTileStateScalar(1280000) == 32767, "positive over-scale scalar clamps to max snorm");
    runner.expect(RendererPackTileStateScalar(-640000) == -32768, "negative full-scale scalar packs to min snorm");
    runner.expect(RendererPackTileStateScalar(-1280000) == -32768, "negative over-scale scalar clamps to min snorm");
    runner.expect(RendererPackTileStateScalar(320000) == 16384, "half-scale scalar rounds consistently");
}

void TestTileStateChunkPacking(TestRunner& runner) {
    std::vector<Tile> tiles(16);
    tiles[5].airPollution = 640000;
    tiles[5].landValue = -640000;
    tiles[6].airPollution = 320000;
    tiles[6].landValue = 0;
    tiles[9].airPollution = -320000;
    tiles[9].landValue = 640000;
    tiles[10].airPollution = 0;
    tiles[10].landValue = 320000;

    ChunkRect chunk;
    chunk.startX = 1;
    chunk.startY = 1;
    chunk.width = 2;
    chunk.height = 2;

    std::vector<std::int16_t> pixels;
    RendererFillTileStateChunkPixels(tiles, 4, chunk, pixels);

    runner.expect(pixels.size() == 8u, "tile-state chunk writes two channels per tile");
    runner.expect(pixels[0] == 32767 && pixels[1] == -32768, "first chunk tile packs pollution and land value");
    runner.expect(pixels[2] == 16384 && pixels[3] == 0, "second chunk tile preserves row-major order");
    runner.expect(pixels[4] == -16384 && pixels[5] == 32767, "third chunk tile packs negative half-scale values");
    runner.expect(pixels[6] == 0 && pixels[7] == 16384, "fourth chunk tile completes row-major packing");
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
    runner.expect(pixels[0] == 255u, "occupied tile lifts to full mask");
    runner.expect(pixels[1] == 0u, "empty tile keeps zero lift mask");
    runner.expect(pixels[2] == 0u, "empty row-major tile keeps zero lift mask");
    runner.expect(pixels[3] == 255u, "occupied row-major tile lifts to full mask");
}

void TestZoningOverlayChunkPacking(TestRunner& runner) {
    std::vector<Tile> tiles(16);
    tiles[5].zoningType = TileZoningResidential;
    tiles[6].zoningType = TileZoningIndustrial;

    ChunkRect chunk;
    chunk.startX = 1;
    chunk.startY = 1;
    chunk.width = 2;
    chunk.height = 2;

    std::vector<std::uint8_t> pixels;
    RendererFillZoningOverlayChunkPixels(tiles, 4, chunk, pixels);

    runner.expect(pixels.size() == 16u, "zoning overlay writes RGBA bytes per tile");
    runner.expect(pixels[0] == 50u && pixels[1] == 210u && pixels[3] > 0u, "residential zoning packs green tint");
    runner.expect(pixels[4] == 238u && pixels[5] == 211u && pixels[7] > 0u, "industrial zoning packs yellow tint");
    runner.expect(pixels[11] == 0u && pixels[15] == 0u, "un-zoned tiles pack transparent overlay");
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
    SimulationDate startDate = CalculateSimulationDate(0u);
    runner.expect(startDate.year == 1900 && startDate.month == 1 && startDate.day == 1, "tick zero starts on January 1 1900");

    SimulationDate februaryDate = CalculateSimulationDate(31u);
    runner.expect(februaryDate.year == 1900 && februaryDate.month == 2 && februaryDate.day == 1, "tick 31 advances to February 1 1900");

    SimulationDate nextYearDate = CalculateSimulationDate(365u);
    runner.expect(nextYearDate.year == 1901 && nextYearDate.month == 1 && nextYearDate.day == 1, "1900 is not treated as a leap year");

    SimulationDate leapDate = CalculateSimulationDate(1519u);
    runner.expect(leapDate.year == 1904 && leapDate.month == 2 && leapDate.day == 29, "1904 leap day is reachable");
    runner.expect(FormatSimulationDate(leapDate, dateSettings) == "1904/02/29", "default simulation date formats as YYYY/MM/DD");

    dateSettings.format = SimulationDateFormat::MonthDayYear;
    runner.expect(FormatSimulationDate(leapDate, dateSettings) == "02/29/1904", "simulation date supports MM/DD/YYYY");

    dateSettings.format = SimulationDateFormat::DayMonthYear;
    runner.expect(FormatSimulationDate(leapDate, dateSettings) == "29/02/1904", "simulation date supports DD/MM/YYYY");
}

void TestWindowQuads(TestRunner& runner) {
    InGameWindow window;
    window.setVisible(false);
    window.setText("title", "House");
    window.updateLayout();
    runner.expect(RendererBuildWindowQuads(window).empty(), "hidden window produces no UI quads");

    window.setVisible(true);
    std::vector<UiQuadInstanceData> quads = RendererBuildWindowQuads(window);
    runner.expect(quads.size() > 5u, "visible window produces frame and text quads");
    runner.expect(AlmostEqual(quads[0].x, 24.0f) && AlmostEqual(quads[0].y, 24.0f), "window background starts at fallback origin");
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
}

void TestHudTextQuads(TestRunner& runner) {
    std::vector<UiQuadInstanceData> quads;
    RendererAppendTextQuads("Population: 12,345", 0.0f, 0.0f, 240.0f, 24.0f, UiColor(1.0f, 1.0f, 1.0f, 1.0f), false, quads);
    runner.expect(!quads.empty(), "HUD text helper emits bitmap glyph quads");
}

void TestUiMenuQuadsAndHitTesting(TestRunner& runner) {
    UiLayout layout;
    std::string action;
    runner.expect(layout.hitTestAction(24.0, 480.0, 1024, 768, action) && action == "select_bulldozer", "fallback side-menu button hit tests by screen position");

    std::vector<UiQuadInstanceData> quads = RendererBuildUiMenuQuads(layout, 1024, 768, "select_road_street");
    runner.expect(!quads.empty(), "visible UI menus produce quads");
    runner.expect(AlmostEqual(quads[0].x, 16.0f), "side menu resolves to left edge");
    runner.expect(quads[0].colorA >= 0.0f, "menu background alpha is stable");

    const std::size_t visibleQuadCount = quads.size();
    layout.toggleMenu("side_tools");
    std::vector<UiQuadInstanceData> hiddenQuads = RendererBuildUiMenuQuads(layout, 1024, 768, std::string());
    runner.expect(hiddenQuads.size() < visibleQuadCount, "hidden side menu removes its button quads");
    runner.expect(layout.hitTestAction(24.0, 740.0, 1024, 768, action) && action == "toggle_side_menu", "bottom-left menu toggle remains clickable");
}

void TestRciToolFallbacksAndPlanning(TestRunner& runner) {
    RciToolCatalog catalog;
    const RciTool* residential = catalog.findTool("residential");
    const RciTool* industrial = catalog.findTool("industrial");
    runner.expect(residential != 0 && industrial != 0, "fallback RCI catalog exposes residential and industrial tools");
    if (residential == 0 || industrial == 0) {
        return;
    }

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

    RciPlan industrialRoadPlan;
    runner.expect(industrial->buildPlan(0, 0, 17, 17, RciPlanMode::LotsAndRoads, 1024, 1024, industrialRoadPlan), "industrial RCI lots-and-roads mode builds a plan");
    if (!industrialRoadPlan.lots.empty()) {
        runner.expect(industrialRoadPlan.lots[0].rect.height() == 8, "industrial RCI lots prefer eight-tile depth");
    }
}

void TestRciToolXmlLoading(TestRunner& runner) {
    const char* filePath = "rci_tool_test.xml";
    {
        std::ofstream file(filePath, std::ios::out | std::ios::trunc);
        file << "<rciTools><tool id=\"residential\" name=\"Homes\" zoningType=\"residential\" colorR=\"0.1\" colorG=\"0.8\" colorB=\"0.2\" colorA=\"0.5\" minDepth=\"2\" preferedDepth=\"5\" maxDepth=\"8\" minWidth=\"2\" preferedWidth=\"16\" maxWidth=\"24\" /></rciTools>";
    }

    RciToolCatalog catalog;
    const bool loaded = catalog.loadFromXmlFile(filePath);
    const RciTool* tool = catalog.findTool("residential");
    runner.expect(loaded && tool != 0, "RCI XML catalog loads tool definitions");
    if (tool != 0) {
        runner.expect(tool->name() == "Homes", "RCI XML tool name is stored");
        runner.expect(tool->preferredDepth() == 5 && tool->preferredWidth() == 16, "RCI XML accepts prefered spelling aliases");
    }

    std::remove(filePath);
}
}

int main() {
    TestRunner runner;
    TestTileStatePacking(runner);
    TestTileStateChunkPacking(runner);
    TestTileLiftChunkPacking(runner);
    TestZoningOverlayChunkPacking(runner);
    TestUtf8Decoder(runner);
    TestSimulationDateCalculation(runner);
    TestWindowQuads(runner);
    TestHudTextQuads(runner);
    TestUiMenuQuadsAndHitTesting(runner);
    TestRciToolFallbacksAndPlanning(runner);
    TestRciToolXmlLoading(runner);
    return runner.finish();
}
