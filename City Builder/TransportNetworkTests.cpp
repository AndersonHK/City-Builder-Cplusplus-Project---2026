#include "AssetLoader.h"
#include "TransportNetwork.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cmath>
#include <fstream>
#include <iostream>
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

RoadStrokeCommand MakeStroke(const Int2& startTile, const Int2& endTile, RoadFamily family, TransportLayerId layer, RoadDirectionMode directionMode = RoadDirectionMode::TwoWay) {
    RoadStrokeCommand command;
    command.startTile = startTile;
    command.cornerTile = endTile;
    command.endTile = endTile;
    command.family = family;
    command.layer = layer;
    command.roadTemplate = TransportNetwork::makeRoadTemplate(family, layer, 1, RoadTrafficSide::RightHand, directionMode);
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

bool InvalidAssetsRejected(const std::string& moduleXml, const std::string& lotXml, const CityParameterRegistry& registry) {
    const std::string root = MakeTempAssetDirectory("CityBuilderAssetInvalid");
    WriteTextAssetFile(root + "\\Modules\\test_module.xml", moduleXml);
    WriteTextAssetFile(root + "\\Lots\\test_lot.xml", lotXml);

    LoadedGameAssets assets;
    std::string errorMessage;
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
    runner.expect(cell.arrowGlyph == static_cast<std::uint8_t>(RoadArrowGlyph::East), "one-way street arrow points east");
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

void TestTSectionDoesNotPaintHalfCrosswalk(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "t-section horizontal street placement succeeds");
    runner.expect(Place(network, MakeStroke(Int2(5, 2), Int2(5, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "t-section vertical street placement succeeds");
    const ResolvedRoadCell& teeCell = CellAt(network, TransportLayerId::Ground, 5, 5);
    const ResolvedRoadCell& teeCellEast = CellAt(network, TransportLayerId::Ground, 6, 5);
    const ResolvedRoadCell& mainSouthCell = CellAt(network, TransportLayerId::Ground, 5, 6);
    const ResolvedRoadCell& mainSouthEastCell = CellAt(network, TransportLayerId::Ground, 6, 6);

    runner.expect(CrosswalkEdges(teeCell) == 0, "t-section endpoint does not render half crosswalk");
    runner.expect(CrosswalkEdges(teeCellEast) == 0, "t-section adjacent endpoint tile does not render half crosswalk");
    runner.expect(CrosswalkEdges(mainSouthCell) == 0, "t-section main-road second body tile stays sidewalk");
    runner.expect(CrosswalkEdges(mainSouthEastCell) == 0, "t-section main-road second adjacent body tile stays sidewalk");
    runner.expect((SidewalkEdges(teeCell) & kRoadDirectionWest) == 0, "t-section does not create west sidewalk halfway across main road");
    runner.expect((SidewalkEdges(teeCellEast) & kRoadDirectionEast) == 0, "t-section does not create east sidewalk halfway across main road");
    runner.expect((SidewalkEdges(teeCell) & kRoadDirectionNorth) != 0, "t-section keeps through main-road sidewalk");
}

void TestJoggedSidewalkDoesNotBecomeCrosswalk(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(14, 14);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(5, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "jogged west horizontal street placement succeeds");
    runner.expect(Place(network, MakeStroke(Int2(6, 4), Int2(10, 4), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "jogged east horizontal street placement succeeds");
    runner.expect(Place(network, MakeStroke(Int2(5, 2), Int2(5, 8), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "jogged vertical crossing street placement succeeds");

    const ResolvedRoadCell& jogCell = CellAt(network, TransportLayerId::Ground, 5, 5);
    runner.expect((SidewalkEdges(jogCell) & kRoadDirectionNorth) == 0, "jogged same-axis sidewalk is not created across occupied road edge");
    runner.expect((CrosswalkEdges(jogCell) & kRoadDirectionNorth) == 0, "jogged opposite-side sidewalk is not treated as through crosswalk");
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
        runner.expect(SameResolvedCell(directCell, upgradedCell), "corner upgrade resolved tile matches direct four-way");
        runner.expect(CrosswalkEdges(upgradedCell) == CrosswalkEdges(directCell), "corner upgrade crosswalk graphics match direct four-way");
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
        runner.expect(SameResolvedCell(directCell, upgradedCell), "missing-arm corner upgrade resolved tile matches direct four-way");
        runner.expect(CrosswalkEdges(upgradedCell) == CrosswalkEdges(directCell), "missing-arm corner upgrade crosswalk graphics match direct four-way");
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
    runner.expect(CrosswalkEdges(northStubEnd) == 0, "north stub end has no crosswalk across middle road body");
    runner.expect(CrosswalkEdges(southStubEnd) == 0, "south stub end has no crosswalk across middle road body");
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
    bool loaded = LoadGameAssets("Data", registry, assets, errorMessage);
    if (!loaded) {
        loaded = LoadGameAssets("City Builder\\Data", registry, assets, errorMessage);
    }

    runner.expect(loaded, "factory/house XML assets load");
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
    TestTSectionDoesNotPaintHalfCrosswalk(runner);
    TestJoggedSidewalkDoesNotBecomeCrosswalk(runner);
    TestCornerDoesNotRenderCrosswalks(runner);
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
