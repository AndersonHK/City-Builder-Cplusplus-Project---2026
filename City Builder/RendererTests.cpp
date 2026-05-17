#include "RendererAlgorithms.h"
#include "RciTool.h"
#include "SimulationDate.h"
#include "SimulationTime.h"

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

void TestLandValueOverlayChunkPacking(TestRunner& runner) {
    std::vector<Tile> tiles(16);
    tiles[5].landValue = 10;
    tiles[6].landValue = 20;
    tiles[9].landValue = 30;
    tiles[10].landValue = 20;

    int minimumLandValue = 0;
    int maximumLandValue = 0;
    runner.expect(RendererFindLandValueRange(tiles, minimumLandValue, maximumLandValue), "land value overlay range finds populated tile values");
    runner.expect(minimumLandValue == 10 && maximumLandValue == 160000, "land value range includes default tile values");

    ChunkRect chunk;
    chunk.startX = 1;
    chunk.startY = 1;
    chunk.width = 2;
    chunk.height = 2;

    std::vector<std::uint8_t> pixels;
    RendererFillLandValueOverlayChunkPixels(tiles, 4, chunk, 10, 30, 89u, pixels);

    runner.expect(pixels.size() == 16u, "land value overlay writes RGBA bytes per tile");
    runner.expect(pixels[0] == 255u && pixels[1] == 0u && pixels[3] == 89u, "lowest land value packs red with overlay alpha");
    runner.expect(pixels[4] == 255u && pixels[5] == 255u && pixels[7] == 89u, "middle land value packs yellow with overlay alpha");
    runner.expect(pixels[8] == 0u && pixels[9] == 255u && pixels[11] == 89u, "highest land value packs green with overlay alpha");
    runner.expect(pixels[12] == 255u && pixels[13] == 255u && pixels[15] == 89u, "matching middle land value repeats yellow");

    RendererFillLandValueOverlayChunkPixels(tiles, 4, chunk, 20, 20, 89u, pixels);
    runner.expect(pixels[0] == 255u && pixels[1] == 255u && pixels[3] == 89u, "flat land value range packs neutral yellow");
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
    runner.expect(SimulationTime::ticksPerDay() == 2u, "simulation days are two ticks long");
    runner.expect(SimulationTime::daysToTicks(31u) == 62u, "logical days convert to ticks");
    runner.expect(SimulationTime::tickToDay(63u) == 31u, "tick converts back to whole elapsed day");

    SimulationDate startDate = CalculateSimulationDate(0u);
    runner.expect(startDate.year == 1900 && startDate.month == 1 && startDate.day == 1, "tick zero starts on January 1 1900");

    SimulationDate firstHalfTickDate = CalculateSimulationDate(1u);
    runner.expect(firstHalfTickDate.year == 1900 && firstHalfTickDate.month == 1 && firstHalfTickDate.day == 1, "first tick stays on January 1 with two ticks per day");

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
    InGameWindow window;
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
    UiLayout layout;
    std::string action;
    std::vector<std::string> regionMenuIds;
    regionMenuIds.push_back("region_exit");
    runner.expect(layout.hitTestAction(24.0, 24.0, 1024, 768, regionMenuIds, action) && action == "open_exit_confirm", "fallback region exit button hit tests in filtered region UI");
    runner.expect(layout.hitTestAction(24.0, 434.0, 1024, 768, action) && action == "select_bulldozer", "fallback side-menu button hit tests by screen position");
    runner.expect(layout.hitTestAction(24.0, 664.0, 1024, 768, action) && action == "select_rci_unzone", "fallback side-menu exposes unzone button");

    std::vector<UiQuadInstanceData> quads = RendererBuildUiMenuQuads(layout, 1024, 768, "select_road_street");
    runner.expect(!quads.empty(), "visible UI menus produce quads");
    runner.expect(AlmostEqual(quads[0].x, 16.0f), "side menu resolves to left edge");
    runner.expect(quads[0].colorA >= 0.0f, "menu background alpha is stable");

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
    TestLandValueOverlayChunkPacking(runner);
    TestUtf8Decoder(runner);
    TestSimulationDateCalculation(runner);
    TestWindowQuads(runner);
    TestHudTextQuads(runner);
    TestLoadingScreenQuads(runner);
    TestUiMenuQuadsAndHitTesting(runner);
    TestUiButtonIconXmlAndRendering(runner);
    TestRciToolFallbacksAndPlanning(runner);
    TestRciToolXmlLoading(runner);
    return runner.finish();
}
