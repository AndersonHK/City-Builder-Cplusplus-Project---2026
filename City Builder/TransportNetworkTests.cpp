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

void TestTSectionDoesNotPaintHalfCrosswalk(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "t-section horizontal street placement succeeds");
    runner.expect(Place(network, MakeStroke(Int2(5, 2), Int2(5, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "t-section vertical street placement succeeds");
    const ResolvedRoadCell& teeCell = CellAt(network, TransportLayerId::Ground, 5, 5);

    runner.expect(CrosswalkEdges(teeCell) == 0, "t-section endpoint does not render half crosswalk");
    runner.expect((SidewalkEdges(teeCell) & (kRoadDirectionNorth | kRoadDirectionWest)) != 0, "t-section endpoint keeps pedestrian graphics as sidewalks");
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
    TestTSectionDoesNotPaintHalfCrosswalk(runner);
    TestSameAxisOffsetRejects(runner);
    TestExactReplayDoesNotAdvanceRevision(runner);
    TestElevatedHighwayHasNoPedestrianGraphics(runner);
    TestGroundRoadRejectsLotOccupancy(runner);

    std::cout << "TransportNetworkTests: " << runner.passed << " passed, " << runner.failed << " failed." << std::endl;
    return runner.failed == 0 ? 0 : 1;
}
