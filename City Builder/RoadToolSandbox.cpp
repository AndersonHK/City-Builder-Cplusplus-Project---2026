#include "RoadToolSandbox.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {
const int kInvalidLotId = -1;

std::vector<ChunkRect> SingleChunkLayout(int width, int height) {
    ChunkRect chunkRect;
    chunkRect.startX = 0;
    chunkRect.startY = 0;
    chunkRect.width = width;
    chunkRect.height = height;
    return std::vector<ChunkRect>(1, chunkRect);
}

Int2 ToolCornerForDrag(const Int2& startTile, const Int2& endTile) {
    const int deltaX = endTile.x - startTile.x;
    const int deltaY = endTile.y - startTile.y;
    return std::abs(deltaX) >= std::abs(deltaY) ? Int2(endTile.x, startTile.y) : Int2(startTile.x, endTile.y);
}

RoadTemplateKind InferToolKind(RoadTemplateKind templateKind, int laneCount, RoadDirectionMode directionMode) {
    if (templateKind == RoadTemplateKind::Street && directionMode == RoadDirectionMode::TwoWay && laneCount >= 2) {
        return RoadTemplateKind::Avenue;
    }

    return templateKind;
}

const char* ToolKindName(RoadTemplateKind templateKind) {
    switch (templateKind) {
        case RoadTemplateKind::Avenue:
            return "avenue";
        case RoadTemplateKind::Highway:
            return "highway";
        default:
            return "street";
    }
}

RoadStrokeCommand MakeCornerStroke(const Int2& startTile, const Int2& cornerTile, const Int2& endTile, RoadDirectionMode directionMode, int laneCount, RoadTemplateKind templateKind) {
    templateKind = InferToolKind(templateKind, laneCount, directionMode);
    RoadStrokeCommand command;
    command.startTile = startTile;
    command.cornerTile = cornerTile;
    command.endTile = endTile;
    command.templateKind = templateKind;
    command.family = RoadFamily::LocalStreet;
    command.layer = TransportLayerId::Ground;
    command.roadTemplate = templateKind == RoadTemplateKind::Avenue
        ? TransportNetwork::makeRoadTemplate(templateKind, RoadTrafficSide::RightHand, RoadDirectionMode::TwoWay)
        : TransportNetwork::makeRoadTemplate(templateKind, RoadTrafficSide::RightHand, directionMode);
    return command;
}

bool Place(TransportNetwork& network, const RoadStrokeCommand& command, std::vector<int>& lotOccupancy) {
    return network.placeRoadStroke(command, lotOccupancy, kInvalidLotId);
}

const ResolvedRoadCell& CellAt(const TransportNetwork& network, TransportLayerId layer, int tileX, int tileY) {
    const int index = tileY * network.width() + tileX;
    const std::size_t slot = TransportNetwork::slotIndex(layer, index, network.totalTileCount());
    return network.resolvedCells()[slot];
}

const TransportCostCell& CostCellAt(const TransportNetwork& network, TransportLayerId layer, TransportMode mode, int tileX, int tileY) {
    const int index = tileY * network.width() + tileX;
    return network.costMap().cell(layer, mode, index);
}

std::uint16_t DirectionCost(const TransportCostCell& cell, std::uint8_t roadDirection) {
    const int directionIndex = RoadDirectionIndex(roadDirection);
    return directionIndex < 0 ? 0u : cell.costs[directionIndex];
}

std::uint8_t SidewalkEdges(const ResolvedRoadCell& cell) {
    return cell.surfaceEdgeMask & kRoadSurfaceSidewalkEdgeMask;
}

std::uint8_t CrosswalkEdges(const ResolvedRoadCell& cell) {
    return (cell.surfaceEdgeMask >> kRoadSurfaceCrosswalkShift) & kRoadSurfaceSidewalkEdgeMask;
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

char DirectionLetter(std::uint8_t roadDirection) {
    switch (roadDirection) {
        case kRoadDirectionNorth:
            return 'N';
        case kRoadDirectionEast:
            return 'E';
        case kRoadDirectionSouth:
            return 'S';
        case kRoadDirectionWest:
            return 'W';
        default:
            return '?';
    }
}

std::string DirectionLetters(std::uint8_t roadDirectionMask) {
    const std::uint8_t straightVerticalMask = static_cast<std::uint8_t>(kRoadDirectionNorth | kRoadDirectionSouth);
    const std::uint8_t straightHorizontalMask = static_cast<std::uint8_t>(kRoadDirectionEast | kRoadDirectionWest);
    if (roadDirectionMask == straightVerticalMask) {
        return "SN";
    }
    if (roadDirectionMask == straightHorizontalMask) {
        return "WE";
    }

    std::string result;
    const std::uint8_t directions[] = {
        kRoadDirectionNorth,
        kRoadDirectionSouth,
        kRoadDirectionEast,
        kRoadDirectionWest
    };
    std::size_t directionIndex = 0;
    for (; directionIndex < sizeof(directions) / sizeof(directions[0]); ++directionIndex) {
        if ((roadDirectionMask & directions[directionIndex]) != 0) {
            result += DirectionLetter(directions[directionIndex]);
        }
    }

    return result.empty() ? "." : result;
}

std::string AxisDirectionLetters(std::uint8_t axisMask) {
    std::string result;
    if ((axisMask & AxisMaskFor(RoadAxis::Vertical)) != 0) {
        result += "SN";
    }
    if ((axisMask & AxisMaskFor(RoadAxis::Horizontal)) != 0) {
        result += "WE";
    }

    return result.empty() ? "." : result;
}

std::string AxisDirectionLettersFromRoadMask(std::uint8_t roadDirectionMask) {
    std::string result;
    if ((roadDirectionMask & (kRoadDirectionNorth | kRoadDirectionSouth)) != 0) {
        result += "SN";
    }
    if ((roadDirectionMask & (kRoadDirectionEast | kRoadDirectionWest)) != 0) {
        result += "WE";
    }

    return result.empty() ? "." : result;
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

int HexValue(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'A' && value <= 'F') {
        return 10 + value - 'A';
    }
    if (value >= 'a' && value <= 'f') {
        return 10 + value - 'a';
    }
    return 0;
}

std::string MedianDirectionGrid(const TransportNetwork& network, int minX, int minY, int maxX, int maxY) {
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
            if (!lane.active || !lane.isSeparator()) {
                continue;
            }

            std::uint8_t laneAxisMask = AxisMaskFor(lane.axis);
            if (laneAxisMask == 0) {
                if ((lane.opposingDirectionDividerMask & (kRoadDirectionNorth | kRoadDirectionSouth)) != 0) {
                    laneAxisMask |= AxisMaskFor(RoadAxis::Horizontal);
                }
                if ((lane.opposingDirectionDividerMask & (kRoadDirectionEast | kRoadDirectionWest)) != 0) {
                    laneAxisMask |= AxisMaskFor(RoadAxis::Vertical);
                }
            }
            axisMasks[static_cast<std::size_t>(tile.tileIndex)] |= laneAxisMask;
        }
    }

    std::string grid;
    int tileY = minY;
    for (; tileY <= maxY; ++tileY) {
        if (tileY > minY) {
            grid += "\n";
        }

        int tileX = minX;
        for (; tileX <= maxX; ++tileX) {
            if (tileX > minX) {
                grid += "\t";
            }

            const int index = tileY * network.width() + tileX;
            grid += AxisDirectionLetters(axisMasks[static_cast<std::size_t>(index)]);
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

RoadDirectionMode ParseDirectionMode(const std::string& token) {
    if (token == "one_way_forward") {
        return RoadDirectionMode::OneWayForward;
    }
    if (token == "one_way_reverse") {
        return RoadDirectionMode::OneWayReverse;
    }

    return RoadDirectionMode::TwoWay;
}

RoadTemplateKind ParseTemplateKind(const std::string& token) {
    if (token == "avenue") {
        return RoadTemplateKind::Avenue;
    }
    if (token == "highway") {
        return RoadTemplateKind::Highway;
    }
    return RoadTemplateKind::Street;
}

RoadToolSandboxAction ParseSandboxAction(const std::string& line) {
    std::istringstream stream(line);
    std::string command;
    stream >> command;

    RoadToolSandboxAction action;
    if (command == "drag" || command == "street" || command == "avenue") {
        if (command == "avenue") {
            action.templateKind = RoadTemplateKind::Avenue;
        }
        int startX = 0;
        int startY = 0;
        int endX = 0;
        int endY = 0;
        stream >> startX >> startY >> endX >> endY;
        action.startTile = Int2(startX, startY);
        action.endTile = Int2(endX, endY);
        action.cornerTile = ToolCornerForDrag(action.startTile, action.endTile);

        std::string token;
        while (stream >> token) {
            if (StartsWith(token, "lanes=")) {
                action.laneCount = std::atoi(token.substr(6).c_str());
            } else if (StartsWith(token, "mode=")) {
                action.directionMode = ParseDirectionMode(token.substr(5));
            } else if (StartsWith(token, "tool=")) {
                action.templateKind = ParseTemplateKind(token.substr(5));
            }
        }
        action.templateKind = InferToolKind(action.templateKind, action.laneCount, action.directionMode);
    } else if (command == "bulldoze") {
        int tileX = 0;
        int tileY = 0;
        stream >> tileX >> tileY;
        action.isBulldoze = true;
        action.startTile = Int2(tileX, tileY);
        action.cornerTile = action.startTile;
        action.endTile = action.startTile;
    }

    return action;
}

bool ParseExpectedHeader(const std::string& line, RoadToolSandboxExpectedGrid& expectedGrid) {
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

int NormalizeQuarterTurns(int clockwiseQuarterTurns) {
    int turns = clockwiseQuarterTurns % 4;
    if (turns < 0) {
        turns += 4;
    }
    return turns;
}

Int2 RotatePointClockwise(Int2 point, int width, int height, int clockwiseQuarterTurns) {
    int currentWidth = width;
    int currentHeight = height;
    Int2 result = point;
    const int turns = NormalizeQuarterTurns(clockwiseQuarterTurns);
    int turn = 0;
    for (; turn < turns; ++turn) {
        result = Int2(currentHeight - 1 - result.y, result.x);
        const int nextWidth = currentHeight;
        currentHeight = currentWidth;
        currentWidth = nextWidth;
    }

    return result;
}

void RotatedDimensions(int width, int height, int clockwiseQuarterTurns, int& rotatedWidth, int& rotatedHeight) {
    rotatedWidth = width;
    rotatedHeight = height;
    const int turns = NormalizeQuarterTurns(clockwiseQuarterTurns);
    if ((turns % 2) != 0) {
        rotatedWidth = height;
        rotatedHeight = width;
    }
}

RoadToolSandboxBounds RotateBoundsClockwise(const RoadToolSandboxBounds& bounds, int width, int height, int clockwiseQuarterTurns) {
    const Int2 corners[] = {
        Int2(bounds.minX, bounds.minY),
        Int2(bounds.maxX, bounds.minY),
        Int2(bounds.minX, bounds.maxY),
        Int2(bounds.maxX, bounds.maxY)
    };

    RoadToolSandboxBounds rotatedBounds;
    bool firstCorner = true;
    std::size_t cornerIndex = 0;
    for (; cornerIndex < sizeof(corners) / sizeof(corners[0]); ++cornerIndex) {
        const Int2 rotatedPoint = RotatePointClockwise(corners[cornerIndex], width, height, clockwiseQuarterTurns);
        if (firstCorner) {
            rotatedBounds.minX = rotatedPoint.x;
            rotatedBounds.maxX = rotatedPoint.x;
            rotatedBounds.minY = rotatedPoint.y;
            rotatedBounds.maxY = rotatedPoint.y;
            firstCorner = false;
        } else {
            rotatedBounds.minX = std::min(rotatedBounds.minX, rotatedPoint.x);
            rotatedBounds.maxX = std::max(rotatedBounds.maxX, rotatedPoint.x);
            rotatedBounds.minY = std::min(rotatedBounds.minY, rotatedPoint.y);
            rotatedBounds.maxY = std::max(rotatedBounds.maxY, rotatedPoint.y);
        }
    }

    return rotatedBounds;
}

std::uint8_t RotateRoadDirectionMaskClockwise(std::uint8_t mask, int clockwiseQuarterTurns) {
    std::uint8_t result = mask;
    const int turns = NormalizeQuarterTurns(clockwiseQuarterTurns);
    int turn = 0;
    for (; turn < turns; ++turn) {
        std::uint8_t next = 0;
        if ((result & kRoadDirectionNorth) != 0) {
            next |= kRoadDirectionEast;
        }
        if ((result & kRoadDirectionEast) != 0) {
            next |= kRoadDirectionSouth;
        }
        if ((result & kRoadDirectionSouth) != 0) {
            next |= kRoadDirectionWest;
        }
        if ((result & kRoadDirectionWest) != 0) {
            next |= kRoadDirectionNorth;
        }
        result = next;
    }

    return result;
}

std::uint8_t DirectionMaskFromLetters(const std::string& token) {
    std::uint8_t mask = 0;
    std::size_t charIndex = 0;
    for (; charIndex < token.size(); ++charIndex) {
        switch (token[charIndex]) {
            case 'N':
                mask |= kRoadDirectionNorth;
                break;
            case 'E':
                mask |= kRoadDirectionEast;
                break;
            case 'S':
                mask |= kRoadDirectionSouth;
                break;
            case 'W':
                mask |= kRoadDirectionWest;
                break;
            default:
                break;
        }
    }

    return mask;
}

bool GridUsesTabs(const std::string& kind) {
    return kind == "car_directions" ||
        kind == "sidewalk_directions" ||
        kind == "median_directions";
}

std::vector<std::string> SplitLines(const std::string& grid) {
    std::vector<std::string> lines;
    std::istringstream stream(grid);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line[line.size() - 1] == '\r') {
            line.erase(line.size() - 1);
        }
        lines.push_back(line);
    }
    if (grid.empty()) {
        lines.push_back(std::string());
    }

    return lines;
}

std::vector<std::string> SplitCells(const std::string& line, bool tabbed) {
    std::vector<std::string> cells;
    if (!tabbed) {
        std::size_t charIndex = 0;
        for (; charIndex < line.size(); ++charIndex) {
            cells.push_back(std::string(1, line[charIndex]));
        }
        return cells;
    }

    std::size_t startIndex = 0;
    for (;;) {
        const std::size_t tabIndex = line.find('\t', startIndex);
        if (tabIndex == std::string::npos) {
            cells.push_back(line.substr(startIndex));
            break;
        }
        cells.push_back(line.substr(startIndex, tabIndex - startIndex));
        startIndex = tabIndex + 1;
    }

    return cells;
}

char RotateAxisCell(char cell, int clockwiseQuarterTurns) {
    if ((NormalizeQuarterTurns(clockwiseQuarterTurns) % 2) == 0) {
        return cell;
    }
    if (cell == 'H') {
        return 'V';
    }
    if (cell == 'V') {
        return 'H';
    }
    return cell;
}

std::string RotateExpectedCell(const std::string& kind, const std::string& cell, int clockwiseQuarterTurns) {
    if (kind == "car_directions" || kind == "sidewalk_directions") {
        return DirectionLetters(RotateRoadDirectionMaskClockwise(DirectionMaskFromLetters(cell), clockwiseQuarterTurns));
    }
    if (kind == "median_directions") {
        return AxisDirectionLettersFromRoadMask(RotateRoadDirectionMaskClockwise(DirectionMaskFromLetters(cell), clockwiseQuarterTurns));
    }
    if (kind == "active_car_axes" || kind == "resolved") {
        return cell.empty() ? cell : std::string(1, RotateAxisCell(cell[0], clockwiseQuarterTurns));
    }
    if (kind == "sidewalk_edges" || kind == "junctions") {
        const std::uint8_t rotatedMask = RotateRoadDirectionMaskClockwise(static_cast<std::uint8_t>(HexValue(cell.empty() ? '0' : cell[0])), clockwiseQuarterTurns);
        return std::string(1, HexDigit(rotatedMask));
    }

    return cell;
}

std::string JoinRotatedCells(const std::vector<std::vector<std::string> >& cells, bool tabbed) {
    std::string grid;
    std::size_t rowIndex = 0;
    for (; rowIndex < cells.size(); ++rowIndex) {
        if (rowIndex > 0) {
            grid += "\n";
        }
        std::size_t columnIndex = 0;
        for (; columnIndex < cells[rowIndex].size(); ++columnIndex) {
            if (tabbed && columnIndex > 0) {
                grid += "\t";
            }
            grid += cells[rowIndex][columnIndex];
        }
    }

    return grid;
}

std::string RotateExpectedGridText(const RoadToolSandboxExpectedGrid& expectedGrid, int clockwiseQuarterTurns) {
    const int sourceWidth = (expectedGrid.bounds.maxX - expectedGrid.bounds.minX) + 1;
    const int sourceHeight = (expectedGrid.bounds.maxY - expectedGrid.bounds.minY) + 1;
    int rotatedWidth = sourceWidth;
    int rotatedHeight = sourceHeight;
    RotatedDimensions(sourceWidth, sourceHeight, clockwiseQuarterTurns, rotatedWidth, rotatedHeight);

    const bool tabbed = GridUsesTabs(expectedGrid.kind);
    std::vector<std::vector<std::string> > rotatedCells(static_cast<std::size_t>(rotatedHeight), std::vector<std::string>(static_cast<std::size_t>(rotatedWidth), "."));
    const std::vector<std::string> lines = SplitLines(expectedGrid.grid);
    int sourceY = 0;
    for (; sourceY < sourceHeight && sourceY < static_cast<int>(lines.size()); ++sourceY) {
        const std::vector<std::string> sourceCells = SplitCells(lines[static_cast<std::size_t>(sourceY)], tabbed);
        int sourceX = 0;
        for (; sourceX < sourceWidth && sourceX < static_cast<int>(sourceCells.size()); ++sourceX) {
            const Int2 rotatedPoint = RotatePointClockwise(Int2(sourceX, sourceY), sourceWidth, sourceHeight, clockwiseQuarterTurns);
            rotatedCells[static_cast<std::size_t>(rotatedPoint.y)][static_cast<std::size_t>(rotatedPoint.x)] =
                RotateExpectedCell(expectedGrid.kind, sourceCells[static_cast<std::size_t>(sourceX)], clockwiseQuarterTurns);
        }
    }

    return JoinRotatedCells(rotatedCells, tabbed);
}

RoadToolSandboxFixture RotateFixtureClockwise(const RoadToolSandboxFixture& fixture, int clockwiseQuarterTurns) {
    RoadToolSandboxFixture rotatedFixture = fixture;
    RotatedDimensions(fixture.width, fixture.height, clockwiseQuarterTurns, rotatedFixture.width, rotatedFixture.height);

    std::size_t actionIndex = 0;
    for (; actionIndex < rotatedFixture.actions.size(); ++actionIndex) {
        RoadToolSandboxAction& action = rotatedFixture.actions[actionIndex];
        action.startTile = RotatePointClockwise(fixture.actions[actionIndex].startTile, fixture.width, fixture.height, clockwiseQuarterTurns);
        action.cornerTile = RotatePointClockwise(fixture.actions[actionIndex].cornerTile, fixture.width, fixture.height, clockwiseQuarterTurns);
        action.endTile = RotatePointClockwise(fixture.actions[actionIndex].endTile, fixture.width, fixture.height, clockwiseQuarterTurns);
    }

    std::size_t expectedGridIndex = 0;
    for (; expectedGridIndex < rotatedFixture.expectedGrids.size(); ++expectedGridIndex) {
        RoadToolSandboxExpectedGrid& expectedGrid = rotatedFixture.expectedGrids[expectedGridIndex];
        expectedGrid.bounds = RotateBoundsClockwise(fixture.expectedGrids[expectedGridIndex].bounds, fixture.width, fixture.height, clockwiseQuarterTurns);
        expectedGrid.grid = RotateExpectedGridText(fixture.expectedGrids[expectedGridIndex], clockwiseQuarterTurns);
    }

    return rotatedFixture;
}

std::string RotationName(int clockwiseQuarterTurns) {
    switch (NormalizeQuarterTurns(clockwiseQuarterTurns)) {
        case 0:
            return "rot0";
        case 1:
            return "rot90";
        case 2:
            return "rot180";
        case 3:
            return "rot270";
        default:
            return "rot?";
    }
}

std::string SandboxGridForExpectation(const RoadToolSandbox& sandbox, const RoadToolSandboxExpectedGrid& expectedGrid) {
    if (expectedGrid.kind == "active_car_axes") {
        return ActiveCarAxisGrid(sandbox.network, expectedGrid.bounds.minX, expectedGrid.bounds.minY, expectedGrid.bounds.maxX, expectedGrid.bounds.maxY);
    }
    if (expectedGrid.kind == "resolved") {
        return ResolvedRoadGrid(sandbox.network, expectedGrid.bounds.minX, expectedGrid.bounds.minY, expectedGrid.bounds.maxX, expectedGrid.bounds.maxY);
    }
    if (expectedGrid.kind == "crosswalks") {
        return CrosswalkGrid(sandbox.network, expectedGrid.bounds.minX, expectedGrid.bounds.minY, expectedGrid.bounds.maxX, expectedGrid.bounds.maxY);
    }
    if (expectedGrid.kind == "car_directions") {
        return DirectionGridForMode(sandbox.network, TransportMode::Car, expectedGrid.bounds.minX, expectedGrid.bounds.minY, expectedGrid.bounds.maxX, expectedGrid.bounds.maxY);
    }
    if (expectedGrid.kind == "sidewalk_directions") {
        return DirectionGridForMode(sandbox.network, TransportMode::Pedestrian, expectedGrid.bounds.minX, expectedGrid.bounds.minY, expectedGrid.bounds.maxX, expectedGrid.bounds.maxY);
    }
    if (expectedGrid.kind == "median_directions") {
        return MedianDirectionGrid(sandbox.network, expectedGrid.bounds.minX, expectedGrid.bounds.minY, expectedGrid.bounds.maxX, expectedGrid.bounds.maxY);
    }
    if (expectedGrid.kind == "sidewalk_edges") {
        return SidewalkEdgeGrid(sandbox.network, expectedGrid.bounds.minX, expectedGrid.bounds.minY, expectedGrid.bounds.maxX, expectedGrid.bounds.maxY);
    }
    if (expectedGrid.kind == "materials") {
        return MaterialGrid(sandbox.network, expectedGrid.bounds.minX, expectedGrid.bounds.minY, expectedGrid.bounds.maxX, expectedGrid.bounds.maxY);
    }
    if (expectedGrid.kind == "junctions") {
        return JunctionMaskGrid(sandbox.network, expectedGrid.bounds.minX, expectedGrid.bounds.minY, expectedGrid.bounds.maxX, expectedGrid.bounds.maxY);
    }

    return std::string();
}
}

RoadToolSandboxBounds::RoadToolSandboxBounds()
    : minX(0),
      minY(0),
      maxX(15),
      maxY(15) {
}

RoadToolSandboxAction::RoadToolSandboxAction()
    : isBulldoze(false),
      startTile(0, 0),
      cornerTile(0, 0),
      endTile(0, 0),
      templateKind(RoadTemplateKind::Street),
      laneCount(1),
      directionMode(RoadDirectionMode::TwoWay) {
}

RoadToolSandboxFixture::RoadToolSandboxFixture()
    : width(16),
      height(16) {
}

RoadToolSandboxRunResult::RoadToolSandboxRunResult()
    : hasName(false),
      hasActions(false),
      hasExpectedGrids(false) {
}

RoadToolSandbox::RoadToolSandbox(int width, int height)
    : lotOccupancy(static_cast<std::size_t>(width * height), kInvalidLotId) {
    network.initialize(width, height, SingleChunkLayout(width, height));
}

bool RoadToolSandbox::dragStreet(const Int2& startTile, const Int2& cornerTile, const Int2& endTile, int laneCount, RoadDirectionMode directionMode, RoadTemplateKind templateKind) {
    const RoadStrokeCommand command = MakeCornerStroke(startTile, cornerTile, endTile, directionMode, laneCount, templateKind);
    const bool placed = Place(network, command, lotOccupancy);
    std::string action = std::string("drag ") + ToolKindName(command.templateKind) + " ";
    action += std::to_string(startTile.x) + "," + std::to_string(startTile.y);
    action += " -> " + std::to_string(endTile.x) + "," + std::to_string(endTile.y);
    action += " corner " + std::to_string(command.cornerTile.x) + "," + std::to_string(command.cornerTile.y);
    action += placed ? " accepted" : " rejected";
    snapshots.push_back(SandboxSnapshot(action, network));
    return placed;
}

bool RoadToolSandbox::bulldoze(int tileX, int tileY) {
    const bool removed = network.removeRoadAtTile(tileX, tileY);
    std::string action = "bulldoze ";
    action += std::to_string(tileX) + "," + std::to_string(tileY);
    action += removed ? " accepted" : " rejected";
    snapshots.push_back(SandboxSnapshot(action, network));
    return removed;
}

std::string RoadToolSandbox::log() const {
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

std::string DirectionGridForMode(const TransportNetwork& network, TransportMode mode, int minX, int minY, int maxX, int maxY) {
    std::string grid;
    int tileY = minY;
    for (; tileY <= maxY; ++tileY) {
        if (tileY > minY) {
            grid += "\n";
        }

        int tileX = minX;
        for (; tileX <= maxX; ++tileX) {
            if (tileX > minX) {
                grid += "\t";
            }

            const TransportCostCell& cell = CostCellAt(network, TransportLayerId::Ground, mode, tileX, tileY);
            grid += DirectionLetters(CostCellDirectionMask(cell));
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
    snapshot += "\ncar directions:\n";
    snapshot += DirectionGridForMode(network, TransportMode::Car, 0, 0, network.width() - 1, network.height() - 1);
    snapshot += "\nsidewalk directions:\n";
    snapshot += DirectionGridForMode(network, TransportMode::Pedestrian, 0, 0, network.width() - 1, network.height() - 1);
    snapshot += "\nmedian directions:\n";
    snapshot += MedianDirectionGrid(network, 0, 0, network.width() - 1, network.height() - 1);
    snapshot += "\nsidewalk edge graphics:\n";
    snapshot += SidewalkEdgeGrid(network, 0, 0, network.width() - 1, network.height() - 1);
    snapshot += "\nmaterials:\n";
    snapshot += MaterialGrid(network, 0, 0, network.width() - 1, network.height() - 1);
    snapshot += "\njunction masks:\n";
    snapshot += JunctionMaskGrid(network, 0, 0, network.width() - 1, network.height() - 1);
    return snapshot;
}

std::string NetworkTopologySignature(const TransportNetwork& network) {
    return SandboxSnapshot("network topology signature", network);
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

RoadToolSandboxFixture LoadSandboxFixture(const std::string& path) {
    RoadToolSandboxFixture fixture;
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
            RoadToolSandboxExpectedGrid expectedGrid;
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

RoadToolSandboxRunResult RunRoadToolSandboxFixture(const std::string& path, int clockwiseQuarterTurns) {
    RoadToolSandboxRunResult result;
    result.fixturePath = path;
    result.rotationName = RotationName(clockwiseQuarterTurns);

    const RoadToolSandboxFixture fixture = LoadSandboxFixture(path);
    result.fixtureName = fixture.name;
    result.fixtureDescription = fixture.description;
    result.hasName = !fixture.name.empty();
    result.hasActions = !fixture.actions.empty();
    result.hasExpectedGrids = !fixture.expectedGrids.empty();
    if (!result.hasName || !result.hasActions || !result.hasExpectedGrids) {
        return result;
    }

    const RoadToolSandboxFixture rotatedFixture = RotateFixtureClockwise(fixture, clockwiseQuarterTurns);
    RoadToolSandbox sandbox(rotatedFixture.width, rotatedFixture.height);
    std::size_t actionIndex = 0;
    for (; actionIndex < rotatedFixture.actions.size(); ++actionIndex) {
        const RoadToolSandboxAction& action = rotatedFixture.actions[actionIndex];
        const bool applied = action.isBulldoze
            ? sandbox.bulldoze(action.startTile.x, action.startTile.y)
            : sandbox.dragStreet(action.startTile, action.cornerTile, action.endTile, action.laneCount, action.directionMode, action.templateKind);
        if (!applied) {
            RoadToolSandboxFailure failure;
            failure.message = "sandbox fixture action applies: " + fixture.name + " " + result.rotationName + " action " + std::to_string(actionIndex) + "\nSandbox log:\n" + sandbox.log();
            result.actionFailures.push_back(failure);
            return result;
        }
    }

    std::size_t expectedIndex = 0;
    for (; expectedIndex < rotatedFixture.expectedGrids.size(); ++expectedIndex) {
        const RoadToolSandboxExpectedGrid& expectedGrid = rotatedFixture.expectedGrids[expectedIndex];
        const std::string actual = SandboxGridForExpectation(sandbox, expectedGrid);
        if (actual != expectedGrid.grid) {
            RoadToolSandboxFailure failure;
            failure.message =
                "sandbox fixture grid matches: " + fixture.name + " " + result.rotationName + " " + expectedGrid.kind +
                "\nExpected topology:\n" + fixture.description +
                "\nExpected:\n" + expectedGrid.grid +
                "\nActual:\n" + actual +
                "\nSandbox log:\n" + sandbox.log();
            result.expectationFailures.push_back(failure);
        }
    }

    return result;
}
