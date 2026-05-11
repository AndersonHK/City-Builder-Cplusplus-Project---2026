#include "TransportNetwork.h"

#include <cstdint>
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

    std::cout << "TransportNetworkTests: " << runner.passed << " passed, " << runner.failed << " failed." << std::endl;
    return runner.failed == 0 ? 0 : 1;
}
