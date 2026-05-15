#include "AssetLoader.h"
#include "CrashLogger.h"
#include "TransportNetwork.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
const int kInvalidLotId = -1;

struct TestRunner {
    int passed;
    int failed;

    TestRunner()
        : passed(0),
          failed(0) {
    }

    void expect(bool condition, const std::string& name) {
        if (condition) {
            ++passed;
            return;
        }

        ++failed;
        std::cout << "FAILED: " << name << std::endl;
    }
};

std::vector<ChunkRect> SingleChunkLayout(int width, int height) {
    ChunkRect chunkRect;
    chunkRect.startX = 0;
    chunkRect.startY = 0;
    chunkRect.width = width;
    chunkRect.height = height;
    return std::vector<ChunkRect>(1, chunkRect);
}

TransportNetwork MakeNetwork(int width, int height) {
    TransportNetwork network;
    network.initialize(width, height, SingleChunkLayout(width, height));
    return network;
}

RoadStrokeCommand MakeStroke(const Int2& startTile, const Int2& endTile, RoadFamily family, TransportLayerId layer, RoadDirectionMode directionMode = RoadDirectionMode::TwoWay, int laneCount = 1) {
    RoadStrokeCommand command;
    command.startTile = startTile;
    command.cornerTile = endTile;
    command.endTile = endTile;
    command.family = family;
    command.layer = layer;
    command.roadTemplate = TransportNetwork::makeRoadTemplate(family, layer, laneCount, RoadTrafficSide::RightHand, directionMode);
    return command;
}

RoadStrokeCommand MakeToolDragStroke(const Int2& startTile, const Int2& endTile, RoadFamily family, TransportLayerId layer, RoadDirectionMode directionMode = RoadDirectionMode::TwoWay, int laneCount = 1) {
    RoadStrokeCommand command;
    const int deltaX = endTile.x - startTile.x;
    const int deltaY = endTile.y - startTile.y;
    command.startTile = startTile;
    command.cornerTile = std::abs(deltaX) >= std::abs(deltaY) ? Int2(endTile.x, startTile.y) : Int2(startTile.x, endTile.y);
    command.endTile = endTile;
    command.family = family;
    command.layer = layer;
    command.roadTemplate = TransportNetwork::makeRoadTemplate(family, layer, laneCount, RoadTrafficSide::RightHand, directionMode);
    return command;
}

bool Place(TransportNetwork& network, const RoadStrokeCommand& command, std::vector<int>& lotOccupancy) {
    return network.placeRoadStroke(command, lotOccupancy, kInvalidLotId);
}

const ResolvedRoadCell& CellAt(const TransportNetwork& network, TransportLayerId layer, int tileX, int tileY) {
    const int tileIndex = tileY * network.width() + tileX;
    const std::size_t slot = TransportNetwork::slotIndex(layer, tileIndex, network.totalTileCount());
    return network.resolvedCells()[slot];
}

const TransportCostCell& CostCellAt(const TransportNetwork& network, TransportLayerId layer, TransportMode mode, int tileX, int tileY) {
    const int tileIndex = tileY * network.width() + tileX;
    return network.costMap().cell(layer, mode, tileIndex);
}

std::uint16_t DirectionCost(const TransportCostCell& cell, std::uint8_t roadDirection) {
    const int directionIndex = RoadDirectionIndex(roadDirection);
    return directionIndex < 0 ? 0u : cell.costs[directionIndex];
}

std::uint16_t DirectionCapacity(const TransportCostCell& cell, std::uint8_t roadDirection) {
    const int directionIndex = RoadDirectionIndex(roadDirection);
    return directionIndex < 0 ? 0u : cell.capacities[directionIndex];
}

TransportPathRequest MakePathRequest(std::uint32_t startNodeId, std::uint32_t goalNodeId, std::uint32_t routeSeed = 0) {
    TransportPathRequest request;
    request.startNodeIds.push_back(startNodeId);
    request.goalNodeIds.push_back(goalNodeId);
    request.routeSeed = routeSeed;
    return request;
}

std::uint8_t SidewalkEdges(const ResolvedRoadCell& cell) {
    return cell.surfaceEdgeMask & kRoadSurfaceSidewalkEdgeMask;
}

std::uint8_t CrosswalkEdges(const ResolvedRoadCell& cell) {
    return (cell.surfaceEdgeMask >> kRoadSurfaceCrosswalkShift) & kRoadSurfaceSidewalkEdgeMask;
}

bool RoadCellEmpty(const ResolvedRoadCell& cell) {
    return cell.family == static_cast<std::uint8_t>(RoadFamily::None);
}

std::uint8_t ArrowMask(const ResolvedRoadCell& cell) {
    return cell.arrowGlyph & (kLaneIntentNorth | kLaneIntentEast | kLaneIntentSouth | kLaneIntentWest);
}

int CardinalDirectionCount(std::uint8_t roadDirectionMask) {
    int count = 0;
    if ((roadDirectionMask & kRoadDirectionNorth) != 0) {
        ++count;
    }
    if ((roadDirectionMask & kRoadDirectionEast) != 0) {
        ++count;
    }
    if ((roadDirectionMask & kRoadDirectionSouth) != 0) {
        ++count;
    }
    if ((roadDirectionMask & kRoadDirectionWest) != 0) {
        ++count;
    }
    return count;
}

bool IsDebugArrow(const ResolvedRoadCell& cell) {
    return (cell.arrowGlyph & kRoadArrowDebugFlag) != 0;
}

bool IsTurnArrow(const ResolvedRoadCell& cell) {
    return cell.arrowGlyph != 0 && !IsDebugArrow(cell);
}

char ActiveCarAxisChar(std::uint8_t axisMask, const ResolvedRoadCell& cell) {
    const bool horizontal = (axisMask & static_cast<std::uint8_t>(RoadAxis::Horizontal)) != 0;
    const bool vertical = (axisMask & static_cast<std::uint8_t>(RoadAxis::Vertical)) != 0;
    if (horizontal && vertical) {
        if (cell.renderVariant == static_cast<std::uint8_t>(RoadRenderVariant::Corner)) {
            return 'L';
        }
        return '+';
    }
    if (horizontal) {
        return 'H';
    }
    if (vertical) {
        return 'V';
    }
    return '.';
}

std::vector<std::uint8_t> ActiveCarAxisMasks(const TransportNetwork& network) {
    std::vector<std::uint8_t> axisMasks(network.totalTileCount(), 0);
    const TransportNetworkSaveState saveState = network.exportSaveState();
    std::size_t savedTileIndex = 0;
    for (; savedTileIndex < saveState.tiles.size(); ++savedTileIndex) {
        const TransportTileSaveState& tile = saveState.tiles[savedTileIndex];
        if (tile.layer != TransportLayerId::Ground ||
            tile.tileIndex < 0 ||
            tile.tileIndex >= static_cast<int>(network.totalTileCount())) {
            continue;
        }

        std::size_t laneIndex = 0;
        for (; laneIndex < tile.lanes.size(); ++laneIndex) {
            const RoadLanePlacement& lane = tile.lanes[laneIndex];
            if (lane.active && lane.isCar()) {
                axisMasks[static_cast<std::size_t>(tile.tileIndex)] |= static_cast<std::uint8_t>(lane.axis);
            }
        }
    }

    return axisMasks;
}

std::string ActiveCarAxisGrid(const TransportNetwork& network, int minX, int minY, int maxX, int maxY) {
    const std::vector<std::uint8_t> axisMasks = ActiveCarAxisMasks(network);
    std::string grid;
    int tileY = minY;
    for (; tileY <= maxY; ++tileY) {
        if (tileY > minY) {
            grid += "\n";
        }

        int tileX = minX;
        for (; tileX <= maxX; ++tileX) {
            const int index = tileY * network.width() + tileX;
            grid += ActiveCarAxisChar(axisMasks[static_cast<std::size_t>(index)], CellAt(network, TransportLayerId::Ground, tileX, tileY));
        }
    }

    return grid;
}

char ResolvedCellChar(const ResolvedRoadCell& cell) {
    if (cell.family == static_cast<std::uint8_t>(RoadFamily::None)) {
        return '.';
    }

    const RoadRenderVariant variant = static_cast<RoadRenderVariant>(cell.renderVariant);
    if (variant == RoadRenderVariant::Corner) {
        return 'L';
    }
    if (variant == RoadRenderVariant::Tee) {
        return 'T';
    }
    if (variant == RoadRenderVariant::Cross) {
        return '+';
    }
    if (variant == RoadRenderVariant::DeadEnd) {
        return 'D';
    }
    if (variant == RoadRenderVariant::Isolated) {
        return 'I';
    }
    if (variant == RoadRenderVariant::Straight) {
        return (cell.junctionMask & (kRoadDirectionEast | kRoadDirectionWest)) != 0 ? 'H' : 'V';
    }

    return '?';
}

std::string ResolvedRoadGrid(const TransportNetwork& network, int minX, int minY, int maxX, int maxY) {
    std::string grid;
    int tileY = minY;
    for (; tileY <= maxY; ++tileY) {
        if (tileY > minY) {
            grid += "\n";
        }

        int tileX = minX;
        for (; tileX <= maxX; ++tileX) {
            grid += ResolvedCellChar(CellAt(network, TransportLayerId::Ground, tileX, tileY));
        }
    }

    return grid;
}

std::string CrosswalkGrid(const TransportNetwork& network, int minX, int minY, int maxX, int maxY) {
    std::string grid;
    int tileY = minY;
    for (; tileY <= maxY; ++tileY) {
        if (tileY > minY) {
            grid += "\n";
        }

        int tileX = minX;
        for (; tileX <= maxX; ++tileX) {
            grid += CrosswalkEdges(CellAt(network, TransportLayerId::Ground, tileX, tileY)) == 0 ? '.' : 'x';
        }
    }

    return grid;
}

char HexDigit(std::uint8_t value) {
    value = static_cast<std::uint8_t>(value & 0x0Fu);
    return value < 10u ? static_cast<char>('0' + value) : static_cast<char>('A' + (value - 10u));
}

std::uint8_t CostCellDirectionMask(const TransportCostCell& cell) {
    const std::uint8_t directions[] = {
        kRoadDirectionNorth,
        kRoadDirectionEast,
        kRoadDirectionSouth,
        kRoadDirectionWest
    };

    std::uint8_t directionMask = 0;
    std::size_t directionIndex = 0;
    for (; directionIndex < sizeof(directions) / sizeof(directions[0]); ++directionIndex) {
        const std::uint8_t direction = directions[directionIndex];
        if (DirectionCost(cell, direction) != kTransportNoCost) {
            directionMask |= direction;
        }
    }

    return directionMask;
}

std::string SidewalkLaneGrid(const TransportNetwork& network, int minX, int minY, int maxX, int maxY) {
    std::string grid;
    int tileY = minY;
    for (; tileY <= maxY; ++tileY) {
        if (tileY > minY) {
            grid += "\n";
        }

        int tileX = minX;
        for (; tileX <= maxX; ++tileX) {
            const TransportCostCell& cell = CostCellAt(network, TransportLayerId::Ground, TransportMode::Pedestrian, tileX, tileY);
            grid += HexDigit(CostCellDirectionMask(cell));
        }
    }

    return grid;
}

std::string SidewalkEdgeGrid(const TransportNetwork& network, int minX, int minY, int maxX, int maxY) {
    std::string grid;
    int tileY = minY;
    for (; tileY <= maxY; ++tileY) {
        if (tileY > minY) {
            grid += "\n";
        }

        int tileX = minX;
        for (; tileX <= maxX; ++tileX) {
            grid += HexDigit(SidewalkEdges(CellAt(network, TransportLayerId::Ground, tileX, tileY)));
        }
    }

    return grid;
}

char MaterialCellChar(const ResolvedRoadCell& cell) {
    const bool hasRoad = (cell.laneTypeMask & kRoadLaneTypeCar) != 0 ||
        (cell.surfaceMask & kRoadSurfaceAsphalt) != 0;
    const bool hasSidewalk = (cell.laneTypeMask & kRoadLaneTypePedestrian) != 0;
    if (hasRoad && hasSidewalk) {
        return 'B';
    }
    if (hasRoad) {
        return 'R';
    }
    if (hasSidewalk) {
        return 'S';
    }
    return '.';
}

std::string MaterialGrid(const TransportNetwork& network, int minX, int minY, int maxX, int maxY) {
    std::string grid;
    int tileY = minY;
    for (; tileY <= maxY; ++tileY) {
        if (tileY > minY) {
            grid += "\n";
        }

        int tileX = minX;
        for (; tileX <= maxX; ++tileX) {
            grid += MaterialCellChar(CellAt(network, TransportLayerId::Ground, tileX, tileY));
        }
    }

    return grid;
}

std::string JunctionMaskGrid(const TransportNetwork& network, int minX, int minY, int maxX, int maxY) {
    std::string grid;
    int tileY = minY;
    for (; tileY <= maxY; ++tileY) {
        if (tileY > minY) {
            grid += "\n";
        }

        int tileX = minX;
        for (; tileX <= maxX; ++tileX) {
            grid += HexDigit(CellAt(network, TransportLayerId::Ground, tileX, tileY).junctionMask);
        }
    }

    return grid;
}

std::string SandboxSnapshot(const std::string& action, const TransportNetwork& network) {
    std::string snapshot = action;
    snapshot += "\nactive car axes:\n";
    snapshot += ActiveCarAxisGrid(network, 0, 0, network.width() - 1, network.height() - 1);
    snapshot += "\nresolved road:\n";
    snapshot += ResolvedRoadGrid(network, 0, 0, network.width() - 1, network.height() - 1);
    snapshot += "\ncrosswalks:\n";
    snapshot += CrosswalkGrid(network, 0, 0, network.width() - 1, network.height() - 1);
    snapshot += "\nsidewalk lanes:\n";
    snapshot += SidewalkLaneGrid(network, 0, 0, network.width() - 1, network.height() - 1);
    snapshot += "\nsidewalk edge graphics:\n";
    snapshot += SidewalkEdgeGrid(network, 0, 0, network.width() - 1, network.height() - 1);
    snapshot += "\nmaterials:\n";
    snapshot += MaterialGrid(network, 0, 0, network.width() - 1, network.height() - 1);
    snapshot += "\njunction masks:\n";
    snapshot += JunctionMaskGrid(network, 0, 0, network.width() - 1, network.height() - 1);
    return snapshot;
}

struct RoadToolSandbox {
    TransportNetwork network;
    std::vector<int> lotOccupancy;
    std::vector<std::string> snapshots;

    RoadToolSandbox(int width, int height)
        : lotOccupancy(static_cast<std::size_t>(width * height), kInvalidLotId) {
        network.initialize(width, height, SingleChunkLayout(width, height));
    }

    bool dragStreet(const Int2& startTile, const Int2& endTile, int laneCount = 1, RoadDirectionMode directionMode = RoadDirectionMode::TwoWay) {
        const RoadStrokeCommand command = MakeToolDragStroke(startTile, endTile, RoadFamily::LocalStreet, TransportLayerId::Ground, directionMode, laneCount);
        const bool placed = Place(network, command, lotOccupancy);
        std::string action = "drag street ";
        action += std::to_string(startTile.x) + "," + std::to_string(startTile.y);
        action += " -> " + std::to_string(endTile.x) + "," + std::to_string(endTile.y);
        action += " corner " + std::to_string(command.cornerTile.x) + "," + std::to_string(command.cornerTile.y);
        action += placed ? " accepted" : " rejected";
        snapshots.push_back(SandboxSnapshot(action, network));
        return placed;
    }

    bool bulldoze(int tileX, int tileY) {
        const bool removed = network.removeRoadAtTile(tileX, tileY);
        std::string action = "bulldoze ";
        action += std::to_string(tileX) + "," + std::to_string(tileY);
        action += removed ? " accepted" : " rejected";
        snapshots.push_back(SandboxSnapshot(action, network));
        return removed;
    }

    std::string log() const {
        std::string result;
        std::size_t snapshotIndex = 0;
        for (; snapshotIndex < snapshots.size(); ++snapshotIndex) {
            if (!result.empty()) {
                result += "\n\n";
            }
            result += snapshots[snapshotIndex];
        }

        return result;
    }
};

void ExpectTextEqual(TestRunner& runner, const std::string& actual, const std::string& expected, const std::string& name, const RoadToolSandbox& sandbox) {
    if (actual == expected) {
        runner.expect(true, name);
        return;
    }

    runner.expect(false, name + "\nExpected:\n" + expected + "\nActual:\n" + actual + "\nSandbox log:\n" + sandbox.log());
}

std::string TrimString(const std::string& text) {
    std::size_t startIndex = 0;
    while (startIndex < text.size() && std::isspace(static_cast<unsigned char>(text[startIndex])) != 0) {
        ++startIndex;
    }

    std::size_t endIndex = text.size();
    while (endIndex > startIndex && std::isspace(static_cast<unsigned char>(text[endIndex - 1])) != 0) {
        --endIndex;
    }

    return text.substr(startIndex, endIndex - startIndex);
}

bool StartsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

struct SandboxBounds {
    int minX;
    int minY;
    int maxX;
    int maxY;

    SandboxBounds()
        : minX(0),
          minY(0),
          maxX(15),
          maxY(15) {
    }
};

struct SandboxExpectedGrid {
    std::string kind;
    SandboxBounds bounds;
    std::string grid;
};

struct SandboxAction {
    bool isBulldoze;
    Int2 startTile;
    Int2 endTile;
    int laneCount;
    RoadDirectionMode directionMode;

    SandboxAction()
        : isBulldoze(false),
          startTile(0, 0),
          endTile(0, 0),
          laneCount(1),
          directionMode(RoadDirectionMode::TwoWay) {
    }
};

struct SandboxFixture {
    std::string name;
    std::string description;
    int width;
    int height;
    std::vector<SandboxAction> actions;
    std::vector<SandboxExpectedGrid> expectedGrids;

    SandboxFixture()
        : width(16),
          height(16) {
    }
};

std::string ReadWholeFile(const std::string& path) {
    std::ifstream stream(path.c_str(), std::ios::in | std::ios::binary);
    if (!stream) {
        return std::string();
    }

    std::ostringstream builder;
    builder << stream.rdbuf();
    return builder.str();
}

std::vector<std::string> ReadFixtureLines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream stream(path.c_str(), std::ios::in | std::ios::binary);
    if (!stream) {
        return lines;
    }

    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.erase(line.size() - 1);
        }

        lines.push_back(line);
    }

    return lines;
}

std::string FixtureDirectory(const std::string& relativePath) {
    const std::string localProbe = relativePath + "\\dead_end.txt";
    if (!ReadWholeFile(localProbe).empty()) {
        return relativePath;
    }

    return "City Builder\\" + relativePath;
}

std::vector<std::string> SandboxFixturePaths() {
    const std::string fixtureDirectory = FixtureDirectory("Data\\TransportNetwork\\SandboxCases");
    const std::string searchPattern = fixtureDirectory + "\\*.txt";
    WIN32_FIND_DATAA findData;
    HANDLE findHandle = FindFirstFileA(searchPattern.c_str(), &findData);
    std::vector<std::string> fixturePaths;
    if (findHandle == INVALID_HANDLE_VALUE) {
        return fixturePaths;
    }

    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            fixturePaths.push_back(fixtureDirectory + "\\" + findData.cFileName);
        }
    } while (FindNextFileA(findHandle, &findData) != 0);

    FindClose(findHandle);
    std::sort(fixturePaths.begin(), fixturePaths.end());
    return fixturePaths;
}

RoadDirectionMode ParseDirectionMode(const std::string& token) {
    if (token == "one_way_forward") {
        return RoadDirectionMode::OneWayForward;
    }
    if (token == "one_way_reverse") {
        return RoadDirectionMode::OneWayReverse;
    }

    return RoadDirectionMode::TwoWay;
}

SandboxAction ParseSandboxAction(const std::string& line) {
    std::istringstream stream(line);
    std::string command;
    stream >> command;

    SandboxAction action;
    if (command == "drag") {
        int startX = 0;
        int startY = 0;
        int endX = 0;
        int endY = 0;
        stream >> startX >> startY >> endX >> endY;
        action.startTile = Int2(startX, startY);
        action.endTile = Int2(endX, endY);

        std::string token;
        while (stream >> token) {
            if (StartsWith(token, "lanes=")) {
                action.laneCount = std::atoi(token.substr(6).c_str());
            } else if (StartsWith(token, "mode=")) {
                action.directionMode = ParseDirectionMode(token.substr(5));
            }
        }
    } else if (command == "bulldoze") {
        int tileX = 0;
        int tileY = 0;
        stream >> tileX >> tileY;
        action.isBulldoze = true;
        action.startTile = Int2(tileX, tileY);
    }

    return action;
}

bool ParseExpectedHeader(const std::string& line, SandboxExpectedGrid& expectedGrid) {
    std::string header = line.substr(std::string("expect ").size());
    if (!header.empty() && header[header.size() - 1] == ':') {
        header.erase(header.size() - 1);
    }

    std::istringstream stream(header);
    stream >> expectedGrid.kind;
    if (expectedGrid.kind.empty()) {
        return false;
    }

    stream >> expectedGrid.bounds.minX >> expectedGrid.bounds.minY >> expectedGrid.bounds.maxX >> expectedGrid.bounds.maxY;
    return true;
}

SandboxFixture LoadSandboxFixture(const std::string& path) {
    SandboxFixture fixture;
    const std::vector<std::string> lines = ReadFixtureLines(path);
    std::size_t lineIndex = 0;
    while (lineIndex < lines.size()) {
        const std::string line = TrimString(lines[lineIndex]);
        if (line.empty() || StartsWith(line, "#")) {
            ++lineIndex;
            continue;
        }

        if (StartsWith(line, "name:")) {
            fixture.name = TrimString(line.substr(5));
            ++lineIndex;
            continue;
        }

        if (StartsWith(line, "size:")) {
            std::istringstream stream(line.substr(5));
            stream >> fixture.width >> fixture.height;
            ++lineIndex;
            continue;
        }

        if (line == "description:") {
            ++lineIndex;
            while (lineIndex < lines.size() && TrimString(lines[lineIndex]) != "end") {
                if (!fixture.description.empty()) {
                    fixture.description += "\n";
                }
                fixture.description += lines[lineIndex];
                ++lineIndex;
            }
            if (lineIndex < lines.size()) {
                ++lineIndex;
            }
            continue;
        }

        if (line == "actions:") {
            ++lineIndex;
            while (lineIndex < lines.size() && TrimString(lines[lineIndex]) != "end") {
                const std::string actionLine = TrimString(lines[lineIndex]);
                if (!actionLine.empty() && !StartsWith(actionLine, "#")) {
                    fixture.actions.push_back(ParseSandboxAction(actionLine));
                }
                ++lineIndex;
            }
            if (lineIndex < lines.size()) {
                ++lineIndex;
            }
            continue;
        }

        if (StartsWith(line, "expect ")) {
            SandboxExpectedGrid expectedGrid;
            if (ParseExpectedHeader(line, expectedGrid)) {
                ++lineIndex;
                while (lineIndex < lines.size() && TrimString(lines[lineIndex]) != "end") {
                    if (!expectedGrid.grid.empty()) {
                        expectedGrid.grid += "\n";
                    }
                    expectedGrid.grid += lines[lineIndex];
                    ++lineIndex;
                }
                fixture.expectedGrids.push_back(expectedGrid);
            }
            if (lineIndex < lines.size()) {
                ++lineIndex;
            }
            continue;
        }

        ++lineIndex;
    }

    return fixture;
}

std::string SandboxGridForExpectation(const RoadToolSandbox& sandbox, const SandboxExpectedGrid& expectedGrid) {
    if (expectedGrid.kind == "active_car_axes") {
        return ActiveCarAxisGrid(sandbox.network, expectedGrid.bounds.minX, expectedGrid.bounds.minY, expectedGrid.bounds.maxX, expectedGrid.bounds.maxY);
    }
    if (expectedGrid.kind == "resolved") {
        return ResolvedRoadGrid(sandbox.network, expectedGrid.bounds.minX, expectedGrid.bounds.minY, expectedGrid.bounds.maxX, expectedGrid.bounds.maxY);
    }
    if (expectedGrid.kind == "crosswalks") {
        return CrosswalkGrid(sandbox.network, expectedGrid.bounds.minX, expectedGrid.bounds.minY, expectedGrid.bounds.maxX, expectedGrid.bounds.maxY);
    }
    if (expectedGrid.kind == "sidewalks") {
        return SidewalkLaneGrid(sandbox.network, expectedGrid.bounds.minX, expectedGrid.bounds.minY, expectedGrid.bounds.maxX, expectedGrid.bounds.maxY);
    }
    if (expectedGrid.kind == "sidewalk_edges") {
        return SidewalkEdgeGrid(sandbox.network, expectedGrid.bounds.minX, expectedGrid.bounds.minY, expectedGrid.bounds.maxX, expectedGrid.bounds.maxY);
    }
    if (expectedGrid.kind == "sidewalk_lanes") {
        return SidewalkLaneGrid(sandbox.network, expectedGrid.bounds.minX, expectedGrid.bounds.minY, expectedGrid.bounds.maxX, expectedGrid.bounds.maxY);
    }
    if (expectedGrid.kind == "materials") {
        return MaterialGrid(sandbox.network, expectedGrid.bounds.minX, expectedGrid.bounds.minY, expectedGrid.bounds.maxX, expectedGrid.bounds.maxY);
    }
    if (expectedGrid.kind == "junctions") {
        return JunctionMaskGrid(sandbox.network, expectedGrid.bounds.minX, expectedGrid.bounds.minY, expectedGrid.bounds.maxX, expectedGrid.bounds.maxY);
    }

    return std::string();
}

void RunSandboxFixture(TestRunner& runner, const std::string& path) {
    const SandboxFixture fixture = LoadSandboxFixture(path);
    runner.expect(!fixture.name.empty(), "sandbox fixture has a name: " + path);
    runner.expect(!fixture.actions.empty(), "sandbox fixture has tool actions: " + path);
    runner.expect(!fixture.expectedGrids.empty(), "sandbox fixture has expected grids: " + path);
    if (fixture.name.empty() || fixture.actions.empty() || fixture.expectedGrids.empty()) {
        return;
    }

    RoadToolSandbox sandbox(fixture.width, fixture.height);
    std::size_t actionIndex = 0;
    for (; actionIndex < fixture.actions.size(); ++actionIndex) {
        const SandboxAction& action = fixture.actions[actionIndex];
        const bool applied = action.isBulldoze
            ? sandbox.bulldoze(action.startTile.x, action.startTile.y)
            : sandbox.dragStreet(action.startTile, action.endTile, action.laneCount, action.directionMode);
        runner.expect(applied, "sandbox fixture action applies: " + fixture.name + " action " + std::to_string(actionIndex));
        if (!applied) {
            return;
        }
    }

    std::size_t expectedIndex = 0;
    for (; expectedIndex < fixture.expectedGrids.size(); ++expectedIndex) {
        const SandboxExpectedGrid& expectedGrid = fixture.expectedGrids[expectedIndex];
        const std::string actual = SandboxGridForExpectation(sandbox, expectedGrid);
        ExpectTextEqual(
            runner,
            actual,
            expectedGrid.grid,
            "sandbox fixture grid matches: " + fixture.name + " " + expectedGrid.kind + "\nExpected topology:\n" + fixture.description,
            sandbox);
    }
}

void ExpectCarCostsStayWithinJunctionMask(TestRunner& runner, const TransportNetwork& network, int tileX, int tileY, const std::string& name) {
    const ResolvedRoadCell& cell = CellAt(network, TransportLayerId::Ground, tileX, tileY);
    const TransportCostCell& costCell = CostCellAt(network, TransportLayerId::Ground, TransportMode::Car, tileX, tileY);
    const std::uint8_t directions[] = {
        kRoadDirectionNorth,
        kRoadDirectionEast,
        kRoadDirectionSouth,
        kRoadDirectionWest
    };

    int costDirectionCount = 0;
    std::size_t directionIndex = 0;
    for (; directionIndex < sizeof(directions) / sizeof(directions[0]); ++directionIndex) {
        const std::uint8_t direction = directions[directionIndex];
        const bool allowedCost = (cell.junctionMask & direction) != 0;
        const bool hasCost = DirectionCost(costCell, direction) > 0u;
        if (hasCost) {
            ++costDirectionCount;
        }
        runner.expect(
            !hasCost || allowedCost,
            name + " at " + std::to_string(tileX) + "," + std::to_string(tileY) + " direction " + std::to_string(static_cast<int>(direction)));
    }

    runner.expect(costDirectionCount > 0, name + " keeps at least one car movement");
    runner.expect(costDirectionCount <= 2, name + " never exposes a three-way car crossing");
}

bool SameResolvedCell(const ResolvedRoadCell& first, const ResolvedRoadCell& second) {
    return first.family == second.family &&
        first.baseGlyph == second.baseGlyph &&
        first.arrowGlyph == second.arrowGlyph &&
        first.laneTypeMask == second.laneTypeMask &&
        first.surfaceMask == second.surfaceMask &&
        first.travelMask == second.travelMask &&
        first.laneCount == second.laneCount &&
        first.surfaceEdgeMask == second.surfaceEdgeMask &&
        first.dividerMask == second.dividerMask &&
        first.exitMask == second.exitMask &&
        first.junctionMask == second.junctionMask &&
        first.renderVariant == second.renderVariant &&
        first.laneTypeCosts == second.laneTypeCosts;
}

std::string ResolvedCellSummary(const ResolvedRoadCell& cell) {
    return "variant=" + std::to_string(static_cast<int>(cell.renderVariant)) +
        " junction=" + std::to_string(static_cast<int>(cell.junctionMask)) +
        " exit=" + std::to_string(static_cast<int>(cell.exitMask)) +
        " surfaceEdges=" + std::to_string(static_cast<int>(cell.surfaceEdgeMask)) +
        " divider=" + std::to_string(static_cast<int>(cell.dividerMask)) +
        " laneTypes=" + std::to_string(static_cast<int>(cell.laneTypeMask)) +
        " travel=" + std::to_string(static_cast<int>(cell.travelMask));
}

const LotModule* FindModule(const LoadedGameAssets& assets, const std::string& id) {
    std::size_t moduleIndex = 0;
    for (; moduleIndex < assets.modules.size(); ++moduleIndex) {
        if (assets.modules[moduleIndex].id == id) {
            return &assets.modules[moduleIndex];
        }
    }

    return 0;
}

const LotAsset* FindLotAsset(const LoadedGameAssets& assets, const std::string& id) {
    std::size_t lotIndex = 0;
    for (; lotIndex < assets.lots.size(); ++lotIndex) {
        if (assets.lots[lotIndex].id == id) {
            return &assets.lots[lotIndex];
        }
    }

    return 0;
}

float ModuleParameterAmount(const LotModule& module, int parameterId) {
    float amount = 0.0f;
    std::size_t contributionIndex = 0;
    for (; contributionIndex < module.parameterContributions.size(); ++contributionIndex) {
        if (module.parameterContributions[contributionIndex].parameterId == parameterId) {
            amount += module.parameterContributions[contributionIndex].amount;
        }
    }

    return amount;
}

void WriteTextAssetFile(const std::string& path, const std::string& text) {
    std::ofstream stream(path.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
    stream << text;
}

std::string MakeTempAssetDirectory(const std::string& name) {
    char tempPath[MAX_PATH];
    const DWORD length = GetTempPathA(MAX_PATH, tempPath);
    std::string root(length == 0 ? "." : std::string(tempPath, tempPath + length));
    if (!root.empty() && root[root.size() - 1] != '\\' && root[root.size() - 1] != '/') {
        root += "\\";
    }

    root += name + "_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(GetTickCount());
    CreateDirectoryA(root.c_str(), 0);
    CreateDirectoryA((root + "\\Modules").c_str(), 0);
    CreateDirectoryA((root + "\\Lots").c_str(), 0);
    CreateDirectoryA((root + "\\TransportNetwork").c_str(), 0);
    return root;
}

bool DirectoryExists(const std::string& path) {
    const DWORD attributes = GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool InvalidAssetsRejected(const std::string& moduleXml, const std::string& lotXml, const CityParameterRegistry& registry) {
    const std::string root = MakeTempAssetDirectory("CityBuilderAssetInvalid");
    WriteTextAssetFile(root + "\\Modules\\test_module.xml", moduleXml);
    WriteTextAssetFile(root + "\\Lots\\test_lot.xml", lotXml);

    LoadedGameAssets assets;
    std::string errorMessage;
    ScopedCrashLogSuppression suppressExpectedAssetErrors;
    return !LoadGameAssets(root, registry, assets, errorMessage) && !errorMessage.empty();
}

void TestStraightTwoWayLocalStreet(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "two-way street placement succeeds");
    const ResolvedRoadCell& northLaneTile = CellAt(network, TransportLayerId::Ground, 4, 5);
    const ResolvedRoadCell& southLaneTile = CellAt(network, TransportLayerId::Ground, 4, 6);

    runner.expect(northLaneTile.family == static_cast<std::uint8_t>(RoadFamily::LocalStreet), "two-way street family resolves");
    runner.expect((northLaneTile.laneTypeMask & kRoadLaneTypePedestrian) != 0, "two-way street has pedestrian lane");
    runner.expect((northLaneTile.laneTypeMask & kRoadLaneTypeCar) != 0, "two-way street has car lane");
    runner.expect((northLaneTile.exitMask & (kRoadDirectionEast | kRoadDirectionWest)) == (kRoadDirectionEast | kRoadDirectionWest), "two-way street exits east and west");
    runner.expect((SidewalkEdges(northLaneTile) & kRoadDirectionNorth) != 0, "north sidewalk renders as sidewalk");
    runner.expect((SidewalkEdges(southLaneTile) & kRoadDirectionSouth) != 0, "south sidewalk renders as sidewalk");
    runner.expect(CrosswalkEdges(northLaneTile) == 0, "straight street has no crosswalk");
}

void TestOneWayLocalStreet(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground, RoadDirectionMode::OneWayForward), lotOccupancy), "one-way street placement succeeds");
    const ResolvedRoadCell& cell = CellAt(network, TransportLayerId::Ground, 4, 5);

    runner.expect((cell.exitMask & kRoadDirectionEast) != 0, "one-way street exits forward");
    runner.expect((cell.exitMask & kRoadDirectionWest) != 0, "one-way street keeps bidirectional pedestrian sidewalk exit");
    runner.expect(ArrowMask(cell) == kLaneIntentEast, "one-way street arrow points east");
    runner.expect(IsDebugArrow(cell), "one-way street arrow is tagged as debug graphics");
    runner.expect((SidewalkEdges(cell) & (kRoadDirectionNorth | kRoadDirectionSouth)) == (kRoadDirectionNorth | kRoadDirectionSouth), "one-way street keeps both sidewalk edges");
}

void TestPerpendicularCrosswalkRequiresLaneContinuation(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "cross base horizontal street placement succeeds");
    runner.expect(Place(network, MakeStroke(Int2(5, 2), Int2(5, 8), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "cross base vertical street placement succeeds");
    const ResolvedRoadCell& crossingCell = CellAt(network, TransportLayerId::Ground, 5, 5);

    runner.expect((CrosswalkEdges(crossingCell) & kRoadDirectionNorth) != 0, "horizontal pedestrian lane renders north crosswalk at full crossing");
    runner.expect((CrosswalkEdges(crossingCell) & kRoadDirectionWest) != 0, "vertical pedestrian lane renders west crosswalk at full crossing");
    runner.expect((crossingCell.surfaceMask & kRoadSurfaceCrosswalk) != 0, "full crossing advertises crosswalk graphic surface");
}

void TestPerpendicularCrosswalkIsOrderIndependent(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(5, 2), Int2(5, 8), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "reverse cross vertical street placement succeeds first");
    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "reverse cross horizontal street placement succeeds second");
    const ResolvedRoadCell& crossingCell = CellAt(network, TransportLayerId::Ground, 5, 5);
    const ResolvedRoadCell& crossingCellEast = CellAt(network, TransportLayerId::Ground, 6, 5);

    runner.expect((CrosswalkEdges(crossingCell) & kRoadDirectionNorth) != 0, "reverse cross horizontal pedestrian lane renders north crosswalk");
    runner.expect((CrosswalkEdges(crossingCell) & kRoadDirectionWest) != 0, "reverse cross vertical pedestrian lane renders west crosswalk");
    runner.expect((CrosswalkEdges(crossingCellEast) & kRoadDirectionEast) != 0, "reverse cross preserves right-side pedestrian crosswalk from first-built road");
    runner.expect((crossingCell.surfaceMask & kRoadSurfaceCrosswalk) != 0, "reverse cross advertises crosswalk surface");
}

void TestTSectionRetexturesRealSidewalkCrosswalks(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "t-section horizontal street placement succeeds");
    runner.expect(Place(network, MakeStroke(Int2(5, 2), Int2(5, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "t-section vertical street placement succeeds");
    const ResolvedRoadCell& teeCell = CellAt(network, TransportLayerId::Ground, 5, 5);
    const ResolvedRoadCell& teeCellEast = CellAt(network, TransportLayerId::Ground, 6, 5);
    const ResolvedRoadCell& mainSouthCell = CellAt(network, TransportLayerId::Ground, 5, 6);
    const ResolvedRoadCell& mainSouthEastCell = CellAt(network, TransportLayerId::Ground, 6, 6);

    runner.expect(CrosswalkEdges(teeCell) != 0, "t-section endpoint retextures real sidewalk as crosswalk");
    runner.expect(CrosswalkEdges(teeCellEast) != 0, "t-section adjacent endpoint retextures real sidewalk as crosswalk");
    runner.expect(CrosswalkEdges(mainSouthCell) == 0, "t-section main-road second body tile stays sidewalk");
    runner.expect(CrosswalkEdges(mainSouthEastCell) == 0, "t-section main-road second adjacent body tile stays sidewalk");
    const std::uint8_t teePedestrianMask = CostCellDirectionMask(CostCellAt(network, TransportLayerId::Ground, TransportMode::Pedestrian, 5, 5));
    const std::uint8_t teeEastPedestrianMask = CostCellDirectionMask(CostCellAt(network, TransportLayerId::Ground, TransportMode::Pedestrian, 6, 5));
    runner.expect(teePedestrianMask == (kRoadDirectionNorth | kRoadDirectionEast | kRoadDirectionWest), "t-section pedestrian lane connects side-road mouth to main sidewalk");
    runner.expect(teeEastPedestrianMask == (kRoadDirectionNorth | kRoadDirectionEast | kRoadDirectionWest), "t-section adjacent pedestrian lane connects side-road mouth to main sidewalk");
}

void TestJoggedSidewalkDoesNotBecomeCrosswalk(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(14, 14);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(5, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "jogged west horizontal street placement succeeds");
    runner.expect(Place(network, MakeStroke(Int2(6, 4), Int2(10, 4), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "jogged east horizontal street placement succeeds");
    runner.expect(Place(network, MakeStroke(Int2(5, 2), Int2(5, 8), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "jogged vertical crossing street placement succeeds");

    const ResolvedRoadCell& jogCell = CellAt(network, TransportLayerId::Ground, 5, 5);
    runner.expect(
        (CrosswalkEdges(jogCell) & kRoadDirectionNorth) == 0,
        "jogged opposite-side sidewalk is not treated as through crosswalk\n" + SandboxSnapshot("jogged sidewalk unit test", network));
}

void TestCornerDoesNotRenderCrosswalks(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    RoadStrokeCommand command;
    command.startTile = Int2(2, 5);
    command.cornerTile = Int2(5, 5);
    command.endTile = Int2(5, 8);
    command.family = RoadFamily::LocalStreet;
    command.layer = TransportLayerId::Ground;
    command.roadTemplate = TransportNetwork::makeRoadTemplate(command.family, command.layer, 1, RoadTrafficSide::RightHand, RoadDirectionMode::TwoWay);
    runner.expect(Place(network, command, lotOccupancy), "corner road stroke placement succeeds");

    runner.expect(CrosswalkEdges(CellAt(network, TransportLayerId::Ground, 5, 5)) == 0, "corner northwest tile has no crosswalk");
    runner.expect(CrosswalkEdges(CellAt(network, TransportLayerId::Ground, 6, 5)) == 0, "corner northeast tile has no crosswalk");
    runner.expect(CrosswalkEdges(CellAt(network, TransportLayerId::Ground, 5, 6)) == 0, "corner southwest tile has no crosswalk");
    runner.expect(CrosswalkEdges(CellAt(network, TransportLayerId::Ground, 6, 6)) == 0, "corner southeast tile has no crosswalk");
}

void TestRoadToolSandboxFixtureCases(TestRunner& runner) {
    const std::vector<std::string> fixturePaths = SandboxFixturePaths();
    runner.expect(!fixturePaths.empty(), "sandbox fixture directory contains declared cases");

    std::size_t fixtureIndex = 0;
    for (; fixtureIndex < fixturePaths.size(); ++fixtureIndex) {
        RunSandboxFixture(runner, fixturePaths[fixtureIndex]);
    }
}

void TestTurnArrowsRenderAheadOfIntersectionsOnly(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "turn arrow horizontal street placement succeeds");
    runner.expect(Place(network, MakeStroke(Int2(5, 2), Int2(5, 8), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "turn arrow vertical street placement succeeds");

    const ResolvedRoadCell& westApproach = CellAt(network, TransportLayerId::Ground, 4, 6);
    const std::uint8_t expectedWestApproachTurns = kLaneIntentNorth | kLaneIntentEast | kLaneIntentSouth;
    runner.expect(ArrowMask(westApproach) == expectedWestApproachTurns, "west approach arrow reflects non-u-turn exits through its connected intersection node");
    runner.expect(IsTurnArrow(westApproach), "west approach turn arrow is not tagged as debug graphics");

    const ResolvedRoadCell& farWestLane = CellAt(network, TransportLayerId::Ground, 3, 6);
    runner.expect(ArrowMask(farWestLane) == kLaneIntentEast, "non-approach lane keeps ordinary direction arrow");
    runner.expect(IsDebugArrow(farWestLane), "non-approach lane arrow remains debug-only");

    const int intersectionTiles[][2] = {
        {5, 5},
        {6, 5},
        {5, 6},
        {6, 6}
    };

    std::size_t intersectionTileIndex = 0;
    for (; intersectionTileIndex < sizeof(intersectionTiles) / sizeof(intersectionTiles[0]); ++intersectionTileIndex) {
        const ResolvedRoadCell& intersectionCell = CellAt(
            network,
            TransportLayerId::Ground,
            intersectionTiles[intersectionTileIndex][0],
            intersectionTiles[intersectionTileIndex][1]);
        runner.expect(!IsTurnArrow(intersectionCell), "intersection collection tile does not receive turn arrow");
    }

    TransportNetwork cornerNetwork = MakeNetwork(12, 12);
    std::vector<int> cornerLotOccupancy(cornerNetwork.totalTileCount(), kInvalidLotId);
    RoadStrokeCommand cornerCommand;
    cornerCommand.startTile = Int2(2, 5);
    cornerCommand.cornerTile = Int2(5, 5);
    cornerCommand.endTile = Int2(5, 8);
    cornerCommand.family = RoadFamily::LocalStreet;
    cornerCommand.layer = TransportLayerId::Ground;
    cornerCommand.roadTemplate = TransportNetwork::makeRoadTemplate(cornerCommand.family, cornerCommand.layer, 1, RoadTrafficSide::RightHand, RoadDirectionMode::TwoWay);
    runner.expect(Place(cornerNetwork, cornerCommand, cornerLotOccupancy), "turn arrow corner stroke placement succeeds");

    const ResolvedRoadCell& cornerApproach = CellAt(cornerNetwork, TransportLayerId::Ground, 4, 6);
    runner.expect(ArrowMask(cornerApproach) == kLaneIntentEast, "corner approach does not receive intersection turn arrows");
    runner.expect(IsDebugArrow(cornerApproach), "corner approach arrow remains debug-only");
}

void TestSingleStrokeCornerCleanupUsesValidCornerMasks(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    RoadStrokeCommand command;
    command.startTile = Int2(2, 5);
    command.cornerTile = Int2(5, 5);
    command.endTile = Int2(5, 8);
    command.family = RoadFamily::LocalStreet;
    command.layer = TransportLayerId::Ground;
    command.roadTemplate = TransportNetwork::makeRoadTemplate(command.family, command.layer, 1, RoadTrafficSide::RightHand, RoadDirectionMode::TwoWay);
    runner.expect(Place(network, command, lotOccupancy), "single-stroke corner placement succeeds");

    const int cornerTiles[][2] = {
        {6, 5},
        {5, 6}
    };

    std::size_t cornerTileIndex = 0;
    for (; cornerTileIndex < sizeof(cornerTiles) / sizeof(cornerTiles[0]); ++cornerTileIndex) {
        const ResolvedRoadCell& cornerCell = CellAt(network, TransportLayerId::Ground, cornerTiles[cornerTileIndex][0], cornerTiles[cornerTileIndex][1]);
        runner.expect(cornerCell.renderVariant == static_cast<std::uint8_t>(RoadRenderVariant::Corner), "single-stroke corner tile resolves to corner variant");
        runner.expect(CardinalDirectionCount(cornerCell.junctionMask) == 2, "single-stroke corner tile has exactly two cleaned junction legs");
        runner.expect((cornerCell.junctionMask & (kRoadDirectionEast | kRoadDirectionWest)) != 0, "single-stroke corner keeps one horizontal leg");
        runner.expect((cornerCell.junctionMask & (kRoadDirectionNorth | kRoadDirectionSouth)) != 0, "single-stroke corner keeps one vertical leg");
        runner.expect(CrosswalkEdges(cornerCell) == 0, "single-stroke corner tile does not render crosswalks");
        runner.expect(!IsTurnArrow(cornerCell), "single-stroke corner tile does not receive turn arrow");
        ExpectCarCostsStayWithinJunctionMask(runner, network, cornerTiles[cornerTileIndex][0], cornerTiles[cornerTileIndex][1], "single-stroke corner car costs stay within cleaned corner legs");
    }
}

void TestRemoveRoadTileClearsTwoTileFootprint(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "two-tile road removal setup succeeds");
    runner.expect(network.removeRoadAtTile(4, 5), "two-tile road removal succeeds from one footprint tile");

    runner.expect(RoadCellEmpty(CellAt(network, TransportLayerId::Ground, 4, 5)), "two-tile road removal clears clicked footprint tile");
    runner.expect(RoadCellEmpty(CellAt(network, TransportLayerId::Ground, 4, 6)), "two-tile road removal clears paired footprint tile");
    runner.expect(!RoadCellEmpty(CellAt(network, TransportLayerId::Ground, 3, 5)), "two-tile road removal leaves previous slice intact");
    runner.expect(!RoadCellEmpty(CellAt(network, TransportLayerId::Ground, 5, 6)), "two-tile road removal leaves next slice intact");
}

void TestRemoveRoadTileClearsFourTileFootprint(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(14, 14);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 4), Int2(10, 4), RoadFamily::LocalStreet, TransportLayerId::Ground, RoadDirectionMode::TwoWay, 2), lotOccupancy), "four-tile road removal setup succeeds");
    runner.expect(network.removeRoadAtTile(6, 5), "four-tile road removal succeeds from interior footprint tile");

    int tileY = 4;
    for (; tileY <= 7; ++tileY) {
        runner.expect(RoadCellEmpty(CellAt(network, TransportLayerId::Ground, 6, tileY)), "four-tile road removal clears full footprint width");
        runner.expect(!RoadCellEmpty(CellAt(network, TransportLayerId::Ground, 5, tileY)), "four-tile road removal leaves previous slice intact");
        runner.expect(!RoadCellEmpty(CellAt(network, TransportLayerId::Ground, 7, tileY)), "four-tile road removal leaves next slice intact");
    }
}

void TestRemovingApproachDoesNotLeavePartialIntersectionCrosswalk(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "partial intersection base horizontal placement succeeds");
    runner.expect(Place(network, MakeStroke(Int2(5, 2), Int2(5, 8), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "partial intersection base vertical placement succeeds");
    runner.expect(network.removeRoadAtTile(5, 4), "partial intersection approach removal succeeds");

    const int intersectionTiles[][2] = {
        {5, 5},
        {6, 5},
        {5, 6},
        {6, 6}
    };

    std::size_t tileIndex = 0;
    for (; tileIndex < sizeof(intersectionTiles) / sizeof(intersectionTiles[0]); ++tileIndex) {
        const ResolvedRoadCell& cell = CellAt(network, TransportLayerId::Ground, intersectionTiles[tileIndex][0], intersectionTiles[tileIndex][1]);
        runner.expect(
            (CrosswalkEdges(cell) & kRoadDirectionNorth) == 0,
            "removed approach does not leave a north-facing partial-intersection crosswalk at " +
                std::to_string(intersectionTiles[tileIndex][0]) + "," +
                std::to_string(intersectionTiles[tileIndex][1]) +
                " mask " + std::to_string(static_cast<int>(CrosswalkEdges(cell))));
    }
}

void TestWideRoadCleanupPropagatesAcrossIntersectionBody(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(20, 20);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 8), Int2(16, 8), RoadFamily::LocalStreet, TransportLayerId::Ground, RoadDirectionMode::TwoWay, 2), lotOccupancy), "wide partial intersection horizontal avenue placement succeeds");
    runner.expect(Place(network, MakeStroke(Int2(8, 2), Int2(8, 16), RoadFamily::LocalStreet, TransportLayerId::Ground, RoadDirectionMode::TwoWay, 2), lotOccupancy), "wide partial intersection vertical avenue placement succeeds");
    runner.expect(network.removeRoadAtTile(8, 7), "wide partial intersection north approach removal succeeds");

    int tileY = 8;
    for (; tileY <= 11; ++tileY) {
        int tileX = 8;
        for (; tileX <= 11; ++tileX) {
            const ResolvedRoadCell& cell = CellAt(network, TransportLayerId::Ground, tileX, tileY);
            runner.expect((CrosswalkEdges(cell) & kRoadDirectionNorth) == 0, "wide cleanup does not keep a north-facing crosswalk in the removed approach");
            runner.expect((cell.junctionMask & kRoadDirectionNorth) == 0, "wide cleanup does not keep a corrected-through north lane");
        }
    }
}

void TestCornerUpgradeMatchesDirectFourWay(TestRunner& runner) {
    TransportNetwork directNetwork = MakeNetwork(12, 12);
    TransportNetwork upgradedNetwork = MakeNetwork(12, 12);
    std::vector<int> directLotOccupancy(directNetwork.totalTileCount(), kInvalidLotId);
    std::vector<int> upgradedLotOccupancy(upgradedNetwork.totalTileCount(), kInvalidLotId);

    runner.expect(Place(directNetwork, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), directLotOccupancy), "direct four-way horizontal stroke succeeds");
    runner.expect(Place(directNetwork, MakeStroke(Int2(5, 2), Int2(5, 8), RoadFamily::LocalStreet, TransportLayerId::Ground), directLotOccupancy), "direct four-way vertical stroke succeeds");

    RoadStrokeCommand cornerCommand;
    cornerCommand.startTile = Int2(2, 5);
    cornerCommand.cornerTile = Int2(5, 5);
    cornerCommand.endTile = Int2(5, 8);
    cornerCommand.family = RoadFamily::LocalStreet;
    cornerCommand.layer = TransportLayerId::Ground;
    cornerCommand.roadTemplate = TransportNetwork::makeRoadTemplate(cornerCommand.family, cornerCommand.layer, 1, RoadTrafficSide::RightHand, RoadDirectionMode::TwoWay);
    runner.expect(Place(upgradedNetwork, cornerCommand, upgradedLotOccupancy), "upgraded four-way initial corner succeeds");
    runner.expect(Place(upgradedNetwork, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), upgradedLotOccupancy), "upgraded four-way horizontal replay stroke succeeds");
    runner.expect(Place(upgradedNetwork, MakeStroke(Int2(5, 2), Int2(5, 8), RoadFamily::LocalStreet, TransportLayerId::Ground), upgradedLotOccupancy), "upgraded four-way vertical replay stroke succeeds");

    const int testTiles[][2] = {
        {5, 5},
        {6, 5},
        {5, 6},
        {6, 6}
    };

    std::size_t testTileIndex = 0;
    for (; testTileIndex < sizeof(testTiles) / sizeof(testTiles[0]); ++testTileIndex) {
        const int tileX = testTiles[testTileIndex][0];
        const int tileY = testTiles[testTileIndex][1];
        const ResolvedRoadCell& directCell = CellAt(directNetwork, TransportLayerId::Ground, tileX, tileY);
        const ResolvedRoadCell& upgradedCell = CellAt(upgradedNetwork, TransportLayerId::Ground, tileX, tileY);
        runner.expect(
            SameResolvedCell(directCell, upgradedCell),
            "corner upgrade resolved tile matches direct four-way at " + std::to_string(tileX) + "," + std::to_string(tileY) +
                "\ndirect: " + ResolvedCellSummary(directCell) +
                "\nupgraded: " + ResolvedCellSummary(upgradedCell));
        runner.expect(CrosswalkEdges(upgradedCell) == CrosswalkEdges(directCell), "corner upgrade crosswalk graphics match direct four-way at " + std::to_string(tileX) + "," + std::to_string(tileY));
    }
}

void TestCornerUpgradeWithOnlyMissingArmsMatchesDirectFourWay(TestRunner& runner) {
    TransportNetwork directNetwork = MakeNetwork(12, 12);
    TransportNetwork upgradedNetwork = MakeNetwork(12, 12);
    std::vector<int> directLotOccupancy(directNetwork.totalTileCount(), kInvalidLotId);
    std::vector<int> upgradedLotOccupancy(upgradedNetwork.totalTileCount(), kInvalidLotId);

    runner.expect(Place(directNetwork, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), directLotOccupancy), "direct missing-arm baseline horizontal stroke succeeds");
    runner.expect(Place(directNetwork, MakeStroke(Int2(5, 2), Int2(5, 8), RoadFamily::LocalStreet, TransportLayerId::Ground), directLotOccupancy), "direct missing-arm baseline vertical stroke succeeds");

    RoadStrokeCommand cornerCommand;
    cornerCommand.startTile = Int2(2, 5);
    cornerCommand.cornerTile = Int2(5, 5);
    cornerCommand.endTile = Int2(5, 8);
    cornerCommand.family = RoadFamily::LocalStreet;
    cornerCommand.layer = TransportLayerId::Ground;
    cornerCommand.roadTemplate = TransportNetwork::makeRoadTemplate(cornerCommand.family, cornerCommand.layer, 1, RoadTrafficSide::RightHand, RoadDirectionMode::TwoWay);
    runner.expect(Place(upgradedNetwork, cornerCommand, upgradedLotOccupancy), "missing-arm upgrade initial corner succeeds");
    runner.expect(Place(upgradedNetwork, MakeStroke(Int2(5, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), upgradedLotOccupancy), "missing-arm upgrade east arm succeeds");
    runner.expect(Place(upgradedNetwork, MakeStroke(Int2(5, 2), Int2(5, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), upgradedLotOccupancy), "missing-arm upgrade north arm succeeds");

    const int testTiles[][2] = {
        {5, 5},
        {6, 5},
        {5, 6},
        {6, 6}
    };

    std::size_t testTileIndex = 0;
    for (; testTileIndex < sizeof(testTiles) / sizeof(testTiles[0]); ++testTileIndex) {
        const int tileX = testTiles[testTileIndex][0];
        const int tileY = testTiles[testTileIndex][1];
        const ResolvedRoadCell& directCell = CellAt(directNetwork, TransportLayerId::Ground, tileX, tileY);
        const ResolvedRoadCell& upgradedCell = CellAt(upgradedNetwork, TransportLayerId::Ground, tileX, tileY);
        runner.expect(
            SameResolvedCell(directCell, upgradedCell),
            "missing-arm corner upgrade resolved tile matches direct four-way at " + std::to_string(tileX) + "," + std::to_string(tileY) +
                "\ndirect: " + ResolvedCellSummary(directCell) +
                "\nupgraded: " + ResolvedCellSummary(upgradedCell));
        runner.expect(CrosswalkEdges(upgradedCell) == CrosswalkEdges(directCell), "missing-arm corner upgrade crosswalk graphics match direct four-way at " + std::to_string(tileX) + "," + std::to_string(tileY));
    }
}

void TestOpposingStubsDoNotConnectAcrossTwoLaneRoad(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "middle road placement succeeds");
    runner.expect(Place(network, MakeStroke(Int2(5, 2), Int2(5, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "north stub placement succeeds");
    runner.expect(Place(network, MakeStroke(Int2(5, 6), Int2(5, 9), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "south stub placement succeeds");

    const ResolvedRoadCell& northStubEnd = CellAt(network, TransportLayerId::Ground, 5, 5);
    const ResolvedRoadCell& southStubEnd = CellAt(network, TransportLayerId::Ground, 5, 6);
    runner.expect((northStubEnd.exitMask & kRoadDirectionSouth) == 0, "north stub does not exit south across middle road body");
    runner.expect((southStubEnd.exitMask & kRoadDirectionNorth) == 0, "south stub does not exit north across middle road body");
    runner.expect((CrosswalkEdges(northStubEnd) & kRoadDirectionSouth) == 0, "north stub end has no south-facing crosswalk across middle road body");
    runner.expect((CrosswalkEdges(southStubEnd) & kRoadDirectionNorth) == 0, "south stub end has no north-facing crosswalk across middle road body");
}

void TestOneSidedExtensionCarriesPedestrianLaneAcrossRoad(TestRunner& runner) {
    TransportNetwork directNetwork = MakeNetwork(12, 12);
    TransportNetwork extendedNetwork = MakeNetwork(12, 12);
    std::vector<int> directLotOccupancy(directNetwork.totalTileCount(), kInvalidLotId);
    std::vector<int> extendedLotOccupancy(extendedNetwork.totalTileCount(), kInvalidLotId);

    runner.expect(Place(directNetwork, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), directLotOccupancy), "direct extension baseline horizontal stroke succeeds");
    runner.expect(Place(directNetwork, MakeStroke(Int2(5, 2), Int2(5, 8), RoadFamily::LocalStreet, TransportLayerId::Ground), directLotOccupancy), "direct extension baseline vertical stroke succeeds");

    runner.expect(Place(extendedNetwork, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), extendedLotOccupancy), "one-sided extension horizontal stroke succeeds");
    runner.expect(Place(extendedNetwork, MakeStroke(Int2(5, 2), Int2(5, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), extendedLotOccupancy), "one-sided extension initial north stub succeeds");
    runner.expect(Place(extendedNetwork, MakeStroke(Int2(5, 5), Int2(5, 8), RoadFamily::LocalStreet, TransportLayerId::Ground), extendedLotOccupancy), "one-sided extension through-stroke succeeds");

    const int testTiles[][2] = {
        {5, 5},
        {6, 5},
        {5, 6},
        {6, 6}
    };

    std::size_t testTileIndex = 0;
    for (; testTileIndex < sizeof(testTiles) / sizeof(testTiles[0]); ++testTileIndex) {
        const int tileX = testTiles[testTileIndex][0];
        const int tileY = testTiles[testTileIndex][1];
        const ResolvedRoadCell& directCell = CellAt(directNetwork, TransportLayerId::Ground, tileX, tileY);
        const ResolvedRoadCell& extendedCell = CellAt(extendedNetwork, TransportLayerId::Ground, tileX, tileY);
        runner.expect(SameResolvedCell(directCell, extendedCell), "one-sided extension resolved tile matches direct crossing");
        runner.expect(CrosswalkEdges(extendedCell) == CrosswalkEdges(directCell), "one-sided extension crosswalk graphics match direct crossing");
    }
}

void TestSameAxisOffsetRejects(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "offset base street placement succeeds");
    runner.expect(!Place(network, MakeStroke(Int2(2, 6), Int2(8, 6), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "same-axis shifted overlap rejects");
}

void TestExactReplayDoesNotAdvanceRevision(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);
    const RoadStrokeCommand command = MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground);

    runner.expect(Place(network, command, lotOccupancy), "replay base street placement succeeds");
    const std::uint64_t revisionAfterFirstPlacement = network.revision();
    runner.expect(Place(network, command, lotOccupancy), "exact replay succeeds");
    runner.expect(network.revision() == revisionAfterFirstPlacement, "exact replay does not advance revision");
}

void TestElevatedHighwayHasNoPedestrianGraphics(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::Highway, TransportLayerId::Elevated), lotOccupancy), "elevated highway placement succeeds");
    const ResolvedRoadCell& cell = CellAt(network, TransportLayerId::Elevated, 4, 5);

    runner.expect((cell.laneTypeMask & kRoadLaneTypePedestrian) == 0, "elevated highway has no pedestrian lanes");
    runner.expect(cell.surfaceEdgeMask == 0, "elevated highway has no sidewalk or crosswalk graphics");
}

void TestGroundRoadRejectsLotOccupancy(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);
    lotOccupancy[5 * network.width() + 2] = 7;

    runner.expect(!Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "ground road rejects occupied lot tile");
}

void TestDirectionalOneWayCostMap(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground, RoadDirectionMode::OneWayForward), lotOccupancy), "directional one-way placement succeeds");
    const TransportCostCell& carCell = CostCellAt(network, TransportLayerId::Ground, TransportMode::Car, 4, 5);
    const TransportCostCell& pedestrianCell = CostCellAt(network, TransportLayerId::Ground, TransportMode::Pedestrian, 4, 5);

    runner.expect(DirectionCost(carCell, kRoadDirectionEast) > 0u, "one-way car cost exists east");
    runner.expect(DirectionCost(carCell, kRoadDirectionWest) == 0u, "one-way car cost does not exist west");
    runner.expect(DirectionCost(pedestrianCell, kRoadDirectionEast) > 0u, "one-way street pedestrian cost exists east");
    runner.expect(DirectionCost(pedestrianCell, kRoadDirectionWest) > 0u, "one-way street pedestrian cost exists west");
}

void TestLocalLaneSpeedCosts(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground, RoadDirectionMode::OneWayForward), lotOccupancy), "speed cost local street placement succeeds");
    const TransportCostCell& carCell = CostCellAt(network, TransportLayerId::Ground, TransportMode::Car, 4, 5);
    const TransportCostCell& pedestrianCell = CostCellAt(network, TransportLayerId::Ground, TransportMode::Pedestrian, 4, 5);

    runner.expect(DirectionCost(carCell, kRoadDirectionEast) == 100u, "local car lane uses 10 tiles per time at capacity");
    runner.expect(DirectionCost(pedestrianCell, kRoadDirectionEast) == 1000u, "pedestrian lane uses 1 tile per time at capacity");
}

void TestCostMapLowerCostAndCapacityAccumulation(TestRunner& runner) {
    TransportCostMap costMap;
    costMap.initialize(2, 1);
    const int tileIndex = 0;

    costMap.addDirectionalCost(TransportLayerId::Ground, TransportMode::Car, tileIndex, kRoadDirectionEast, 20u, 10u);
    costMap.addDirectionalCost(TransportLayerId::Ground, TransportMode::Car, tileIndex, kRoadDirectionEast, 10u, 20u);
    const TransportCostCell& cell = costMap.cell(TransportLayerId::Ground, TransportMode::Car, tileIndex);

    runner.expect(DirectionCost(cell, kRoadDirectionEast) == 10u, "cost map keeps lower directional cost");
    runner.expect(DirectionCapacity(cell, kRoadDirectionEast) == 30u, "cost map accumulates compatible directional capacity");
}

void TestCongestionCurveReducesSpeedFromTable(TestRunner& runner) {
    TransportCostMap costMap;
    costMap.initialize(2, 1);

    TransportCongestionCurve congestionCurve;
    congestionCurve.points.clear();
    congestionCurve.points.push_back(TransportCongestionPoint(0.0f, 1.0f));
    congestionCurve.points.push_back(TransportCongestionPoint(1.0f, 1.0f));
    congestionCurve.points.push_back(TransportCongestionPoint(2.0f, 0.5f));
    costMap.setCongestionCurve(congestionCurve);

    costMap.addDirectionalCost(TransportLayerId::Ground, TransportMode::Car, 0, kRoadDirectionEast, 100u, 10u);
    costMap.cellForMutation(TransportLayerId::Ground, TransportMode::Car, 0).oldLoads[RoadDirectionIndex(kRoadDirectionEast)] = 20u;

    TransportPathScratch scratch;
    TransportPathResult result;
    runner.expect(costMap.findPath(MakePathRequest(costMap.nodeId(TransportLayerId::Ground, TransportMode::Car, 0), costMap.nodeId(TransportLayerId::Ground, TransportMode::Car, 1)), scratch, result), "congestion curve path succeeds");
    runner.expect(result.totalCost > 199.0f && result.totalCost < 201.0f, "congestion speed multiplier doubles cost at 200 percent use");
}

void TestHighwayDoesNotExposeBuildingAccess(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::Highway, TransportLayerId::Elevated), lotOccupancy), "highway access test placement succeeds");
    const TransportCostCell& highwayCell = CostCellAt(network, TransportLayerId::Elevated, TransportMode::Car, 4, 5);
    runner.expect(highwayCell.buildingAccessMask == 0u, "elevated highway has no adjacent building access");
}

void TestBuildingAccessCandidatesFromLocalStreet(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "building access local street placement succeeds");

    std::vector<std::uint32_t> accessNodes;
    network.costMap().collectBuildingAccessNodes(4, 4, 1, 1, kTransportModeCar | kTransportModePedestrian, accessNodes);
    bool foundCar = false;
    bool foundPedestrian = false;
    std::size_t nodeIndex = 0;
    for (; nodeIndex < accessNodes.size(); ++nodeIndex) {
        foundCar = foundCar || accessNodes[nodeIndex] == network.costMap().nodeId(TransportLayerId::Ground, TransportMode::Car, 4, 5);
        foundPedestrian = foundPedestrian || accessNodes[nodeIndex] == network.costMap().nodeId(TransportLayerId::Ground, TransportMode::Pedestrian, 4, 5);
    }

    runner.expect(foundCar, "building access collects adjacent car node from local street");
    runner.expect(foundPedestrian, "building access collects adjacent pedestrian node from local street");
}

void TestLayerIsolationWithoutTransfer(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "layer isolation ground road succeeds");
    runner.expect(Place(network, MakeStroke(Int2(5, 2), Int2(5, 8), RoadFamily::Highway, TransportLayerId::Elevated), lotOccupancy), "layer isolation elevated road succeeds");

    TransportPathScratch scratch;
    TransportPathResult result;
    const std::uint32_t groundNode = network.costMap().nodeId(TransportLayerId::Ground, TransportMode::Car, 5, 5);
    const std::uint32_t elevatedNode = network.costMap().nodeId(TransportLayerId::Elevated, TransportMode::Car, 5, 5);
    runner.expect(!network.costMap().findPath(MakePathRequest(groundNode, elevatedNode), scratch, result), "overlapping ground and elevated roads do not connect implicitly");
}

void TestExplicitTransferConnectsModesAndLayers(TestRunner& runner) {
    TransportCostMap costMap;
    costMap.initialize(1, 1);
    const std::uint32_t groundPedestrianNode = costMap.nodeId(TransportLayerId::Ground, TransportMode::Pedestrian, 0, 0);
    const std::uint32_t elevatedCarNode = costMap.nodeId(TransportLayerId::Elevated, TransportMode::Car, 0, 0);
    costMap.addTransferEdge(groundPedestrianNode, elevatedCarNode, 5u, 100u);
    costMap.finalizeTransferEdges();

    TransportPathScratch scratch;
    TransportPathResult result;
    runner.expect(costMap.findPath(MakePathRequest(groundPedestrianNode, elevatedCarNode), scratch, result), "explicit transfer edge connects modes and layers");
    runner.expect(result.steps.size() == 1u && result.steps[0].kind == TransportPathStepKind::Transfer, "explicit transfer path records transfer step");
}

void TestPathLoadAssignmentAndOverlay(TestRunner& runner) {
    TransportCostMap costMap;
    costMap.initialize(2, 1);
    costMap.addDirectionalCost(TransportLayerId::Ground, TransportMode::Car, 0, kRoadDirectionEast, 1u, 10u);

    TransportPathScratch scratch;
    TransportPathResult result;
    runner.expect(costMap.findPath(MakePathRequest(costMap.nodeId(TransportLayerId::Ground, TransportMode::Car, 0), costMap.nodeId(TransportLayerId::Ground, TransportMode::Car, 1)), scratch, result), "load assignment path finds simple edge");

    costMap.beginNextLoadFromOldLoad();
    costMap.applyPathLoad(result, 7u, true);
    costMap.commitNextLoad();
    runner.expect(costMap.cell(TransportLayerId::Ground, TransportMode::Car, 0).oldLoads[RoadDirectionIndex(kRoadDirectionEast)] == 7u, "path load adds to old load after commit");

    std::vector<std::uint8_t> overlayPixels;
    costMap.buildTrafficOverlay(overlayPixels);
    runner.expect(overlayPixels[3] == kTrafficOverlayAlphaByte, "traffic overlay marks relevant tile alpha");
    runner.expect(overlayPixels[0] > overlayPixels[1], "traffic overlay shifts toward red under load");

    costMap.beginNextLoadFromOldLoad();
    costMap.applyPathLoad(result, 3u, false);
    costMap.commitNextLoad();
    runner.expect(costMap.cell(TransportLayerId::Ground, TransportMode::Car, 0).oldLoads[RoadDirectionIndex(kRoadDirectionEast)] == 4u, "path load subtraction removes previous assignment");
}

void TestCongestionReroutesPath(TestRunner& runner) {
    TransportCostMap costMap;
    costMap.initialize(3, 2);

    costMap.addDirectionalCost(TransportLayerId::Ground, TransportMode::Car, 0, kRoadDirectionEast, 1u, 10u);
    costMap.addDirectionalCost(TransportLayerId::Ground, TransportMode::Car, 1, kRoadDirectionEast, 1u, 10u);
    costMap.addDirectionalCost(TransportLayerId::Ground, TransportMode::Car, 0, kRoadDirectionSouth, 1u, 100u);
    costMap.addDirectionalCost(TransportLayerId::Ground, TransportMode::Car, 3, kRoadDirectionEast, 1u, 100u);
    costMap.addDirectionalCost(TransportLayerId::Ground, TransportMode::Car, 4, kRoadDirectionEast, 1u, 100u);
    costMap.addDirectionalCost(TransportLayerId::Ground, TransportMode::Car, 5, kRoadDirectionNorth, 1u, 100u);
    costMap.cellForMutation(TransportLayerId::Ground, TransportMode::Car, 0).oldLoads[RoadDirectionIndex(kRoadDirectionEast)] = 100u;
    costMap.cellForMutation(TransportLayerId::Ground, TransportMode::Car, 1).oldLoads[RoadDirectionIndex(kRoadDirectionEast)] = 100u;

    TransportPathScratch scratch;
    TransportPathResult result;
    runner.expect(costMap.findPath(MakePathRequest(costMap.nodeId(TransportLayerId::Ground, TransportMode::Car, 0), costMap.nodeId(TransportLayerId::Ground, TransportMode::Car, 2)), scratch, result), "congestion reroute path succeeds");
    runner.expect(!result.steps.empty() && result.steps[0].roadDirection == kRoadDirectionSouth, "congestion reroutes away from overloaded direct edge");
}

void TestEqualRouteJitterSpreadsChoices(TestRunner& runner) {
    TransportCostMap costMap;
    costMap.initialize(2, 2);
    costMap.addDirectionalCost(TransportLayerId::Ground, TransportMode::Car, 0, kRoadDirectionEast, 1u, 100u);
    costMap.addDirectionalCost(TransportLayerId::Ground, TransportMode::Car, 1, kRoadDirectionSouth, 1u, 100u);
    costMap.addDirectionalCost(TransportLayerId::Ground, TransportMode::Car, 0, kRoadDirectionSouth, 1u, 100u);
    costMap.addDirectionalCost(TransportLayerId::Ground, TransportMode::Car, 2, kRoadDirectionEast, 1u, 100u);

    int eastFirstCount = 0;
    int southFirstCount = 0;
    int seed = 0;
    for (; seed < 128; ++seed) {
        TransportPathScratch scratch;
        TransportPathResult result;
        const bool found = costMap.findPath(MakePathRequest(costMap.nodeId(TransportLayerId::Ground, TransportMode::Car, 0), costMap.nodeId(TransportLayerId::Ground, TransportMode::Car, 3), static_cast<std::uint32_t>(seed)), scratch, result);
        if (found && !result.steps.empty()) {
            if (result.steps[0].roadDirection == kRoadDirectionEast) {
                ++eastFirstCount;
            } else if (result.steps[0].roadDirection == kRoadDirectionSouth) {
                ++southFirstCount;
            }
        }
    }

    runner.expect(eastFirstCount > 0, "equal route jitter chooses east-first routes sometimes");
    runner.expect(southFirstCount > 0, "equal route jitter chooses south-first routes sometimes");
}

void TestTrafficOverlayStartsGreenOnRoadCapacity(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "traffic overlay road placement succeeds");
    const int tileIndex = 5 * network.width() + 4;
    const std::size_t pixelOffset = static_cast<std::size_t>(tileIndex) * 4u;
    runner.expect(network.trafficOverlayState()[pixelOffset + 0u] == 0u, "traffic overlay starts with no red load");
    runner.expect(network.trafficOverlayState()[pixelOffset + 1u] == 255u, "traffic overlay starts green where capacity exists");
    runner.expect(network.trafficOverlayState()[pixelOffset + 3u] == kTrafficOverlayAlphaByte, "traffic overlay starts visible where capacity exists");
}

void TestFactoryHouseAssetsAndParameters(TestRunner& runner) {
    CityParameterRegistry registry;
    LoadedGameAssets assets;
    std::string errorMessage;
    bool loaded = false;
    const char* dataDirectories[] = {
        "Data",
        "City Builder\\Data"
    };

    std::size_t dataDirectoryIndex = 0;
    for (; !loaded && dataDirectoryIndex < sizeof(dataDirectories) / sizeof(dataDirectories[0]); ++dataDirectoryIndex) {
        if (DirectoryExists(dataDirectories[dataDirectoryIndex])) {
            loaded = LoadGameAssets(dataDirectories[dataDirectoryIndex], registry, assets, errorMessage);
        }
    }

    runner.expect(loaded, "factory/house XML assets load" + (errorMessage.empty() ? std::string() : "\nLoader error: " + errorMessage));
    if (!loaded) {
        return;
    }

    const LotModule* warehouseModule = FindModule(assets, "warehouse_module");
    const LotModule* smokestackModule = FindModule(assets, "smokestack_module");
    const LotModule* houseModule = FindModule(assets, "house_module");
    const LotModule* drivewayModule = FindModule(assets, "driveway_module");
    const LotModule* gardenModule = FindModule(assets, "garden_module");
    const LotAsset* factoryLot = FindLotAsset(assets, "factory_lot");
    const LotAsset* houseLot = FindLotAsset(assets, "house_lot");

    runner.expect(warehouseModule != 0, "warehouse module exists");
    runner.expect(smokestackModule != 0, "smokestack module exists");
    runner.expect(houseModule != 0, "house module exists");
    runner.expect(drivewayModule != 0, "driveway module exists");
    runner.expect(gardenModule != 0, "garden module exists");
    runner.expect(factoryLot != 0, "factory lot exists");
    runner.expect(houseLot != 0, "house lot exists");
    if (warehouseModule == 0 || smokestackModule == 0 || houseModule == 0 || drivewayModule == 0 || gardenModule == 0 || factoryLot == 0 || houseLot == 0) {
        return;
    }

    runner.expect(ModuleParameterAmount(*warehouseModule, registry.jobsDirtyIndustryId()) == 6.0f, "warehouse contributes six dirty industry jobs");
    runner.expect(ModuleParameterAmount(*houseModule, registry.residentsLowWealthId()) == 5.0f, "house contributes five low wealth residents");
    runner.expect(factoryLot->footprintWidth == 3 && factoryLot->footprintHeight == 2, "factory footprint fits warehouse and adjacent smokestack");
    runner.expect(houseLot->footprintWidth == 2 && houseLot->footprintHeight == 4, "house footprint is 2x4");
    runner.expect(factoryLot->accessDefinitions.size() == 8u, "factory declares car and pedestrian access around all warehouse edges");
    runner.expect(houseLot->accessDefinitions.size() == 2u, "house declares driveway and garden access");
    runner.expect(!assets.congestionCurve.points.empty() && assets.congestionCurve.points[0].speedMultiplier > 0.0f, "transport congestion XML loads");

    bool foundFactoryWarehousePlacement = false;
    bool foundFactorySmokestackPlacement = false;
    std::size_t factoryPlacementIndex = 0;
    for (; factoryPlacementIndex < factoryLot->initialModules.size(); ++factoryPlacementIndex) {
        const LotModulePlacementDefinition& placement = factoryLot->initialModules[factoryPlacementIndex];
        if (placement.moduleId == "warehouse_module" && placement.localOrigin.x == 0 && placement.localOrigin.y == 0) {
            foundFactoryWarehousePlacement = true;
        }
        if (placement.moduleId == "smokestack_module" && placement.localOrigin.x == 2 && placement.localOrigin.y == 0) {
            foundFactorySmokestackPlacement = true;
        }
    }
    runner.expect(foundFactoryWarehousePlacement, "factory includes warehouse module");
    runner.expect(foundFactorySmokestackPlacement, "factory includes smokestack module");

    bool foundFactoryNorthwestAccess = false;
    bool foundFactoryWestNorthAccess = false;
    bool foundFactoryNortheastAccess = false;
    bool foundFactoryEastNorthAccess = false;
    bool foundFactorySouthwestAccess = false;
    bool foundFactoryWestSouthAccess = false;
    bool foundFactorySoutheastAccess = false;
    bool foundFactoryEastSouthAccess = false;
    const std::uint8_t factoryModeMask = static_cast<std::uint8_t>(kTransportModeCar | kTransportModePedestrian);
    std::size_t factoryAccessIndex = 0;
    for (; factoryAccessIndex < factoryLot->accessDefinitions.size(); ++factoryAccessIndex) {
        const LotAccessDefinition& access = factoryLot->accessDefinitions[factoryAccessIndex];
        if (access.modeMask != factoryModeMask) {
            continue;
        }

        if (access.localTile.x == 0 && access.localTile.y == 0 && access.direction == kRoadDirectionNorth) {
            foundFactoryNorthwestAccess = true;
        }
        if (access.localTile.x == 0 && access.localTile.y == 0 && access.direction == kRoadDirectionWest) {
            foundFactoryWestNorthAccess = true;
        }
        if (access.localTile.x == 1 && access.localTile.y == 0 && access.direction == kRoadDirectionNorth) {
            foundFactoryNortheastAccess = true;
        }
        if (access.localTile.x == 2 && access.localTile.y == 0 && access.direction == kRoadDirectionEast) {
            foundFactoryEastNorthAccess = true;
        }
        if (access.localTile.x == 0 && access.localTile.y == 1 && access.direction == kRoadDirectionSouth) {
            foundFactorySouthwestAccess = true;
        }
        if (access.localTile.x == 0 && access.localTile.y == 1 && access.direction == kRoadDirectionWest) {
            foundFactoryWestSouthAccess = true;
        }
        if (access.localTile.x == 1 && access.localTile.y == 1 && access.direction == kRoadDirectionSouth) {
            foundFactorySoutheastAccess = true;
        }
        if (access.localTile.x == 2 && access.localTile.y == 1 && access.direction == kRoadDirectionEast) {
            foundFactoryEastSouthAccess = true;
        }
    }
    runner.expect(foundFactoryNorthwestAccess && foundFactoryWestNorthAccess &&
        foundFactoryNortheastAccess && foundFactoryEastNorthAccess &&
        foundFactorySouthwestAccess && foundFactoryWestSouthAccess &&
        foundFactorySoutheastAccess && foundFactoryEastSouthAccess,
        "factory access accepts car and pedestrians on every exterior factory edge");

    Lot rotatedFactoryInstance(2, factoryLot->id, 20, 20, 1);
    rotatedFactoryInstance.setExplicitFootprint(Int2(-1, 0), 2, 3, 64);
    rotatedFactoryInstance.addModule(*warehouseModule, Int2(-1, 0), 64, 2, 2);
    rotatedFactoryInstance.addModule(*smokestackModule, Int2(-1, 2), 64, 2, 1);
    std::vector<LotRenderInstance> rotatedFactoryRenderInstances;
    rotatedFactoryInstance.buildRenderInstances(rotatedFactoryRenderInstances);
    bool foundRotatedWarehouseRender = false;
    bool foundRotatedSmokestackRender = false;
    std::size_t renderInstanceIndex = 0;
    for (; renderInstanceIndex < rotatedFactoryRenderInstances.size(); ++renderInstanceIndex) {
        const LotRenderInstance& renderInstance = rotatedFactoryRenderInstances[renderInstanceIndex];
        if (renderInstance.originX == 19 && renderInstance.originY == 20 && renderInstance.width == 2 && renderInstance.height == 2) {
            foundRotatedWarehouseRender = true;
        }
        if (renderInstance.originX == 19 && renderInstance.originY == 22 && renderInstance.width == 2 && renderInstance.height == 1) {
            foundRotatedSmokestackRender = true;
        }
    }
    runner.expect(foundRotatedWarehouseRender, "rotated factory renders warehouse in rotated footprint");
    runner.expect(foundRotatedSmokestackRender, "rotated factory renders smokestack beside warehouse");

    bool foundHousePlacement = false;
    bool foundDrivewayAccess = false;
    bool foundGardenAccess = false;
    std::size_t placementIndex = 0;
    for (; placementIndex < houseLot->initialModules.size(); ++placementIndex) {
        const LotModulePlacementDefinition& placement = houseLot->initialModules[placementIndex];
        if (placement.moduleId == "house_module" && placement.localOrigin.x == 0 && placement.localOrigin.y == 1) {
            foundHousePlacement = true;
        }
    }
    std::size_t accessIndex = 0;
    for (; accessIndex < houseLot->accessDefinitions.size(); ++accessIndex) {
        const LotAccessDefinition& access = houseLot->accessDefinitions[accessIndex];
        if (access.localTile.x == 1 && access.localTile.y == 0 && access.direction == kRoadDirectionNorth && access.modeMask == kTransportModeCar) {
            foundDrivewayAccess = true;
        }
        if (access.localTile.x == 0 && access.localTile.y == 0 && access.direction == kRoadDirectionNorth && access.modeMask == kTransportModePedestrian) {
            foundGardenAccess = true;
        }
    }
    runner.expect(foundHousePlacement, "house module is centered behind the frontage row");
    runner.expect(foundDrivewayAccess, "driveway exposes car access from front-right tile");
    runner.expect(foundGardenAccess, "garden exposes pedestrian access from front-left tile");

    Lot houseInstance(1, houseLot->id, 10, 10);
    houseInstance.setExplicitFootprint(houseLot->footprintOrigin, houseLot->footprintWidth, houseLot->footprintHeight, 64);
    houseInstance.addModule(*houseModule, houseLot->initialModules[0].localOrigin, 64);
    runner.expect(houseInstance.occupiedTileIndices().size() == 8u, "house lot occupies full 2x4 footprint");
    runner.expect(houseInstance.parameterContributions().size() == 1u && houseInstance.parameterContributions()[0].amount == 5.0f, "house lot aggregates resident driver");
}

void TestInvalidAssetValidation(TestRunner& runner) {
    CityParameterRegistry registry;
    const std::string validModule =
        "<module id=\"test_module\">"
        "<size width=\"1\" height=\"1\" />"
        "<effects airPollution=\"0\" landValue=\"0\" />"
        "</module>";
    const std::string validLot =
        "<lot id=\"test_lot\">"
        "<anchor x=\"0\" y=\"0\" />"
        "<footprint x=\"0\" y=\"0\" width=\"1\" height=\"1\" />"
        "<modules><moduleRef id=\"test_module\" x=\"0\" y=\"0\" /></modules>"
        "</lot>";

    const std::string badParameterModule =
        "<module id=\"test_module\">"
        "<size width=\"1\" height=\"1\" />"
        "<effects airPollution=\"0\" landValue=\"0\" />"
        "<parameters><driver id=\"jobs.imaginary\" amount=\"1\" /></parameters>"
        "</module>";
    runner.expect(InvalidAssetsRejected(badParameterModule, validLot, registry), "invalid parameter id rejects at load");

    const std::string badFootprintLot =
        "<lot id=\"test_lot\">"
        "<anchor x=\"0\" y=\"0\" />"
        "<footprint x=\"0\" y=\"0\" width=\"0\" height=\"1\" />"
        "<modules><moduleRef id=\"test_module\" x=\"0\" y=\"0\" /></modules>"
        "</lot>";
    runner.expect(InvalidAssetsRejected(validModule, badFootprintLot, registry), "invalid footprint dimensions reject at load");

    const std::string outsideFootprintLot =
        "<lot id=\"test_lot\">"
        "<anchor x=\"0\" y=\"0\" />"
        "<footprint x=\"0\" y=\"0\" width=\"1\" height=\"1\" />"
        "<modules><moduleRef id=\"test_module\" x=\"1\" y=\"0\" /></modules>"
        "</lot>";
    runner.expect(InvalidAssetsRejected(validModule, outsideFootprintLot, registry), "module outside footprint rejects at load");

    const std::string unknownModuleLot =
        "<lot id=\"test_lot\">"
        "<anchor x=\"0\" y=\"0\" />"
        "<footprint x=\"0\" y=\"0\" width=\"1\" height=\"1\" />"
        "<modules><moduleRef id=\"missing_module\" x=\"0\" y=\"0\" /></modules>"
        "</lot>";
    runner.expect(InvalidAssetsRejected(validModule, unknownModuleLot, registry), "unknown module ref rejects at load");

    const std::string inwardAccessLot =
        "<lot id=\"test_lot\">"
        "<anchor x=\"0\" y=\"0\" />"
        "<footprint x=\"0\" y=\"0\" width=\"2\" height=\"1\" />"
        "<modules><moduleRef id=\"test_module\" x=\"0\" y=\"0\" /></modules>"
        "<access><connection x=\"0\" y=\"0\" direction=\"east\" modes=\"car\" /></access>"
        "</lot>";
    runner.expect(InvalidAssetsRejected(validModule, inwardAccessLot, registry), "access direction pointing inside footprint rejects at load");
}
}

int main() {
    TestRunner runner;
    TestStraightTwoWayLocalStreet(runner);
    TestOneWayLocalStreet(runner);
    TestPerpendicularCrosswalkRequiresLaneContinuation(runner);
    TestPerpendicularCrosswalkIsOrderIndependent(runner);
    TestTSectionRetexturesRealSidewalkCrosswalks(runner);
    TestJoggedSidewalkDoesNotBecomeCrosswalk(runner);
    TestCornerDoesNotRenderCrosswalks(runner);
    TestRoadToolSandboxFixtureCases(runner);
    TestTurnArrowsRenderAheadOfIntersectionsOnly(runner);
    TestSingleStrokeCornerCleanupUsesValidCornerMasks(runner);
    TestRemoveRoadTileClearsTwoTileFootprint(runner);
    TestRemoveRoadTileClearsFourTileFootprint(runner);
    TestRemovingApproachDoesNotLeavePartialIntersectionCrosswalk(runner);
    TestWideRoadCleanupPropagatesAcrossIntersectionBody(runner);
    TestCornerUpgradeMatchesDirectFourWay(runner);
    TestCornerUpgradeWithOnlyMissingArmsMatchesDirectFourWay(runner);
    TestOpposingStubsDoNotConnectAcrossTwoLaneRoad(runner);
    TestOneSidedExtensionCarriesPedestrianLaneAcrossRoad(runner);
    TestSameAxisOffsetRejects(runner);
    TestExactReplayDoesNotAdvanceRevision(runner);
    TestElevatedHighwayHasNoPedestrianGraphics(runner);
    TestGroundRoadRejectsLotOccupancy(runner);
    TestDirectionalOneWayCostMap(runner);
    TestLocalLaneSpeedCosts(runner);
    TestCostMapLowerCostAndCapacityAccumulation(runner);
    TestCongestionCurveReducesSpeedFromTable(runner);
    TestHighwayDoesNotExposeBuildingAccess(runner);
    TestBuildingAccessCandidatesFromLocalStreet(runner);
    TestLayerIsolationWithoutTransfer(runner);
    TestExplicitTransferConnectsModesAndLayers(runner);
    TestPathLoadAssignmentAndOverlay(runner);
    TestCongestionReroutesPath(runner);
    TestEqualRouteJitterSpreadsChoices(runner);
    TestTrafficOverlayStartsGreenOnRoadCapacity(runner);
    TestFactoryHouseAssetsAndParameters(runner);
    TestInvalidAssetValidation(runner);

    std::cout << "TransportNetworkTests: " << runner.passed << " passed, " << runner.failed << " failed." << std::endl;
    return runner.failed == 0 ? 0 : 1;
}
