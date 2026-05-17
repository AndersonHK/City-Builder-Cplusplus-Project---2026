#include "AssetLoader.h"
#include "CrashLogger.h"
#include "RoadToolSandbox.h"
#include "SimulationTime.h"
#include "TestAssetXml.h"
#include "TransportNetwork.h"

#include <cstdint>
#include <cmath>
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

std::vector<ChunkRect> TwoColumnChunkLayout(int width, int height) {
    std::vector<ChunkRect> chunks;
    const int chunkWidth = width / 2;
    int chunkX = 0;
    for (; chunkX < 2; ++chunkX) {
        ChunkRect chunkRect;
        chunkRect.startX = chunkX * chunkWidth;
        chunkRect.startY = 0;
        chunkRect.width = chunkX == 0 ? chunkWidth : width - chunkWidth;
        chunkRect.height = height;
        chunks.push_back(chunkRect);
    }
    return chunks;
}

TransportNetwork MakeNetwork(int width, int height) {
    TransportNetwork network;
    network.initialize(width, height, SingleChunkLayout(width, height));
    return network;
}

TransportNetwork MakeTwoColumnChunkNetwork(int width, int height) {
    TransportNetwork network;
    network.initialize(width, height, TwoColumnChunkLayout(width, height));
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

RoadStrokeCommand MakeCornerStroke(const Int2& startTile, const Int2& cornerTile, const Int2& endTile, RoadFamily family, TransportLayerId layer, RoadDirectionMode directionMode = RoadDirectionMode::TwoWay, int laneCount = 1) {
    RoadStrokeCommand command;
    command.startTile = startTile;
    command.cornerTile = cornerTile;
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

int ActiveSavedLaneCount(const TransportNetwork& network, RoadLaneTypeId laneType) {
    const TransportNetworkSaveState saveState = network.exportSaveState();
    int count = 0;
    std::size_t savedTileIndex = 0;
    for (; savedTileIndex < saveState.tiles.size(); ++savedTileIndex) {
        const TransportTileSaveState& tile = saveState.tiles[savedTileIndex];
        std::size_t laneIndex = 0;
        for (; laneIndex < tile.lanes.size(); ++laneIndex) {
            const RoadLanePlacement& lane = tile.lanes[laneIndex];
            if (lane.active && lane.laneType == laneType) {
                ++count;
            }
        }
    }

    return count;
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
        " base=" + std::to_string(static_cast<int>(cell.baseGlyph)) +
        " arrow=" + std::to_string(static_cast<int>(cell.arrowGlyph)) +
        " junction=" + std::to_string(static_cast<int>(cell.junctionMask)) +
        " exit=" + std::to_string(static_cast<int>(cell.exitMask)) +
        " surface=" + std::to_string(static_cast<int>(cell.surfaceMask)) +
        " surfaceEdges=" + std::to_string(static_cast<int>(cell.surfaceEdgeMask)) +
        " divider=" + std::to_string(static_cast<int>(cell.dividerMask)) +
        " laneTypes=" + std::to_string(static_cast<int>(cell.laneTypeMask)) +
        " laneCount=" + std::to_string(static_cast<int>(cell.laneCount)) +
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

const RciGrowthRule* FindGrowthRule(const LoadedGameAssets& assets, std::uint16_t zoningType) {
    std::size_t ruleIndex = 0;
    for (; ruleIndex < assets.rciGrowthRules.size(); ++ruleIndex) {
        if (assets.rciGrowthRules[ruleIndex].zoningType == zoningType) {
            return &assets.rciGrowthRules[ruleIndex];
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

float LotAssetCapacity(const LoadedGameAssets& assets, const LotAsset& lotAsset, int parameterId) {
    float capacity = 0.0f;
    std::size_t placementIndex = 0;
    for (; placementIndex < lotAsset.initialModules.size(); ++placementIndex) {
        const LotModule* module = FindModule(assets, lotAsset.initialModules[placementIndex].moduleId);
        if (module != 0) {
            capacity += ModuleParameterAmount(*module, parameterId);
        }
    }

    return capacity;
}

float InterpolatedDensity(const RciGrowthRule& rule, int population) {
    if (rule.densityPoints.empty()) {
        return 0.0f;
    }
    if (population <= rule.densityPoints.front().population || rule.densityPoints.size() == 1u) {
        return rule.densityPoints.front().maxDensityPerTile;
    }

    std::size_t pointIndex = 1;
    for (; pointIndex < rule.densityPoints.size(); ++pointIndex) {
        if (population > rule.densityPoints[pointIndex].population) {
            continue;
        }

        const RciDensityPoint& lower = rule.densityPoints[pointIndex - 1u];
        const RciDensityPoint& upper = rule.densityPoints[pointIndex];
        const float span = static_cast<float>(upper.population - lower.population);
        if (span <= 0.0f) {
            return upper.maxDensityPerTile;
        }

        const float t = static_cast<float>(population - lower.population) / span;
        return lower.maxDensityPerTile + ((upper.maxDensityPerTile - lower.maxDensityPerTile) * t);
    }

    return rule.densityPoints.back().maxDensityPerTile;
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

void TestLotConstructionDurationLoading(TestRunner& runner) {
    CityParameterRegistry registry;
    const std::string root = MakeTempAssetDirectory("CityBuilderAssetDurations");
    WriteTextAssetFile(
        root + "\\Modules\\test_module.xml",
        "<module id=\"test_module\">"
        "<size width=\"1\" height=\"1\" />"
        "<effects airPollution=\"0\" landValue=\"0\" />"
        "</module>");
    WriteTextAssetFile(
        root + "\\Lots\\days_lot.xml",
        "<lot id=\"days_lot\" constructionDays=\"3\">"
        "<anchor x=\"0\" y=\"0\" />"
        "<footprint x=\"0\" y=\"0\" width=\"1\" height=\"1\" />"
        "<modules><moduleRef id=\"test_module\" x=\"0\" y=\"0\" /></modules>"
        "</lot>");
    WriteTextAssetFile(
        root + "\\Lots\\ticks_lot.xml",
        "<lot id=\"ticks_lot\" constructionTicks=\"5\">"
        "<anchor x=\"0\" y=\"0\" />"
        "<footprint x=\"0\" y=\"0\" width=\"1\" height=\"1\" />"
        "<modules><moduleRef id=\"test_module\" x=\"0\" y=\"0\" /></modules>"
        "</lot>");

    LoadedGameAssets assets;
    std::string errorMessage;
    runner.expect(LoadGameAssets(root, registry, assets, errorMessage), "construction duration test assets load" + (errorMessage.empty() ? std::string() : "\nLoader error: " + errorMessage));
    const LotAsset* daysLot = FindLotAsset(assets, "days_lot");
    const LotAsset* ticksLot = FindLotAsset(assets, "ticks_lot");
    runner.expect(daysLot != 0 && daysLot->constructionTicks == static_cast<int>(SimulationTime::daysToTicks(3u)), "constructionDays converts to stored ticks");
    runner.expect(ticksLot != 0 && ticksLot->constructionTicks == 5, "legacy constructionTicks remains raw ticks");
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
    const ResolvedRoadCell& northCell = CellAt(network, TransportLayerId::Ground, 4, 5);
    const ResolvedRoadCell& southCell = CellAt(network, TransportLayerId::Ground, 4, 6);

    runner.expect((northCell.exitMask & kRoadDirectionEast) != 0, "one-way street exits forward on north tile");
    runner.expect((southCell.exitMask & kRoadDirectionEast) != 0, "one-way street exits forward on south tile");
    runner.expect((northCell.exitMask & kRoadDirectionWest) != 0, "one-way street keeps bidirectional pedestrian sidewalk exit on north tile");
    runner.expect((southCell.exitMask & kRoadDirectionWest) != 0, "one-way street keeps bidirectional pedestrian sidewalk exit on south tile");
    runner.expect(ArrowMask(northCell) == kLaneIntentEast, "one-way street north arrow points east");
    runner.expect(ArrowMask(southCell) == kLaneIntentEast, "one-way street south arrow points east");
    runner.expect(IsDebugArrow(northCell), "one-way street north arrow is tagged as debug graphics");
    runner.expect(IsDebugArrow(southCell), "one-way street south arrow is tagged as debug graphics");
    runner.expect((SidewalkEdges(northCell) & kRoadDirectionNorth) != 0, "one-way street keeps outer north sidewalk edge");
    runner.expect((SidewalkEdges(southCell) & kRoadDirectionSouth) != 0, "one-way street keeps outer south sidewalk edge");
}

void TestOneWayLaneMinimums(TestRunner& runner) {
    const RoadTemplate localOneWay = TransportNetwork::makeRoadTemplate(RoadFamily::LocalStreet, TransportLayerId::Ground, 1, RoadTrafficSide::RightHand, RoadDirectionMode::OneWayForward);
    runner.expect(localOneWay.laneCount == 2, "one-way local street promotes requested one lane to two lanes");
    runner.expect(localOneWay.identity.footprint == 2, "one-way local street promoted lane count occupies two tiles");

    const RoadTemplate localTwoWay = TransportNetwork::makeRoadTemplate(RoadFamily::LocalStreet, TransportLayerId::Ground, 1, RoadTrafficSide::RightHand, RoadDirectionMode::TwoWay);
    runner.expect(localTwoWay.laneCount == 1, "two-way local street still allows one lane per direction");

    const RoadTemplate highwayOneWay = TransportNetwork::makeRoadTemplate(RoadFamily::Highway, TransportLayerId::Elevated, 1, RoadTrafficSide::RightHand, RoadDirectionMode::OneWayForward);
    runner.expect(highwayOneWay.laneCount == 1, "one-way elevated highway still allows one lane");
}

void TestSeparatorLaneSandwichRules(TestRunner& runner) {
    TransportNetwork twoWayNetwork = MakeNetwork(12, 12);
    std::vector<int> twoWayLotOccupancy(twoWayNetwork.totalTileCount(), kInvalidLotId);

    runner.expect(Place(twoWayNetwork, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), twoWayLotOccupancy), "two-way separator street placement succeeds");
    runner.expect(ActiveSavedLaneCount(twoWayNetwork, RoadLaneTypeId::Separator) > 0, "two-way local street emits separator placements");
    runner.expect((CellAt(twoWayNetwork, TransportLayerId::Ground, 4, 5).dividerMask >> kRoadDividerYellowShift) != 0, "two-way separator publishes yellow divider above center seam");
    runner.expect((CellAt(twoWayNetwork, TransportLayerId::Ground, 4, 6).dividerMask >> kRoadDividerYellowShift) != 0, "two-way separator publishes yellow divider below center seam");

    const TransportCostCell& northCarCell = CostCellAt(twoWayNetwork, TransportLayerId::Ground, TransportMode::Car, 4, 5);
    const TransportCostCell& southCarCell = CostCellAt(twoWayNetwork, TransportLayerId::Ground, TransportMode::Car, 4, 6);
    runner.expect(DirectionCost(northCarCell, kRoadDirectionSouth) == 0u, "separator does not create southbound car edge across center seam");
    runner.expect(DirectionCost(southCarCell, kRoadDirectionNorth) == 0u, "separator does not create northbound car edge across center seam");

    TransportNetwork oneWayNetwork = MakeNetwork(12, 12);
    std::vector<int> oneWayLotOccupancy(oneWayNetwork.totalTileCount(), kInvalidLotId);
    runner.expect(Place(oneWayNetwork, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground, RoadDirectionMode::OneWayForward), oneWayLotOccupancy), "one-way separator street placement succeeds");
    runner.expect(ActiveSavedLaneCount(oneWayNetwork, RoadLaneTypeId::Separator) == 0, "one-way local street omits separator between same-direction lanes");
    runner.expect((CellAt(oneWayNetwork, TransportLayerId::Ground, 4, 5).dividerMask >> kRoadDividerYellowShift) == 0, "one-way local street publishes no opposing-direction separator divider");

    TransportNetwork crossingNetwork = MakeNetwork(12, 12);
    std::vector<int> crossingLotOccupancy(crossingNetwork.totalTileCount(), kInvalidLotId);
    runner.expect(Place(crossingNetwork, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), crossingLotOccupancy), "separator crossing horizontal placement succeeds");
    runner.expect(Place(crossingNetwork, MakeStroke(Int2(5, 2), Int2(5, 8), RoadFamily::LocalStreet, TransportLayerId::Ground), crossingLotOccupancy), "separator crossing vertical placement succeeds");
    runner.expect(CellAt(crossingNetwork, TransportLayerId::Ground, 5, 5).dividerMask == 0, "separator divider is suppressed inside intersection body");
    runner.expect(CellAt(crossingNetwork, TransportLayerId::Ground, 6, 6).dividerMask == 0, "separator divider is suppressed across the intersection body");
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

void TestRoadToolSandboxFixtureCases(TestRunner& runner, bool includeRotatedCases) {
    const std::vector<std::string> fixturePaths = SandboxFixturePaths();
    runner.expect(!fixturePaths.empty(), "sandbox fixture directory contains declared cases");

    std::size_t fixtureIndex = 0;
    for (; fixtureIndex < fixturePaths.size(); ++fixtureIndex) {
        int rotation = 0;
        const int rotationCount = includeRotatedCases ? 4 : 1;
        for (; rotation < rotationCount; ++rotation) {
            const RoadToolSandboxRunResult result = RunRoadToolSandboxFixture(fixturePaths[fixtureIndex], rotation);
            const std::string caseName = result.fixtureName.empty() ? fixturePaths[fixtureIndex] : result.fixtureName;
            const std::string rotationName = caseName + " " + result.rotationName;
            runner.expect(result.hasName, "sandbox fixture has a name: " + fixturePaths[fixtureIndex] + " " + result.rotationName);
            runner.expect(result.hasActions, "sandbox fixture has tool actions: " + fixturePaths[fixtureIndex] + " " + result.rotationName);
            runner.expect(result.hasExpectedGrids, "sandbox fixture has expected grids: " + fixturePaths[fixtureIndex] + " " + result.rotationName);
            if (!result.hasName || !result.hasActions || !result.hasExpectedGrids) {
                continue;
            }

            if (result.actionFailures.empty()) {
                runner.expect(true, "sandbox fixture actions apply: " + rotationName);
            } else {
                std::size_t failureIndex = 0;
                for (; failureIndex < result.actionFailures.size(); ++failureIndex) {
                    runner.expect(false, result.actionFailures[failureIndex].message);
                }
                continue;
            }

            if (result.expectationFailures.empty()) {
                runner.expect(true, "sandbox fixture grids match: " + rotationName);
            } else {
                std::size_t failureIndex = 0;
                for (; failureIndex < result.expectationFailures.size(); ++failureIndex) {
                    runner.expect(false, result.expectationFailures[failureIndex].message);
                }
            }
        }
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

void TestShortTwoTileRoadRemnantIsRemoved(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(10, 10);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 5), Int2(3, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "short two-tile road remnant setup succeeds");
    runner.expect(network.removeRoadAtTile(2, 5), "short two-tile road first slice removal succeeds");

    int tileY = 5;
    for (; tileY <= 6; ++tileY) {
        runner.expect(RoadCellEmpty(CellAt(network, TransportLayerId::Ground, 2, tileY)), "short two-tile road clears deleted slice");
        runner.expect(RoadCellEmpty(CellAt(network, TransportLayerId::Ground, 3, tileY)), "short two-tile road removes one-slice remnant");
    }
}

void TestShortFourTileRoadRemnantIsRemoved(TestRunner& runner) {
    TransportNetwork network = MakeNetwork(12, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(2, 4), Int2(5, 4), RoadFamily::LocalStreet, TransportLayerId::Ground, RoadDirectionMode::TwoWay, 2), lotOccupancy), "short four-tile road remnant setup succeeds");
    runner.expect(network.removeRoadAtTile(2, 5), "short four-tile road first slice removal succeeds");

    int tileX = 2;
    for (; tileX <= 5; ++tileX) {
        int tileY = 4;
        for (; tileY <= 7; ++tileY) {
            runner.expect(RoadCellEmpty(CellAt(network, TransportLayerId::Ground, tileX, tileY)), "short four-tile road removes three-slice remnant");
        }
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
        runner.expect(
            SameResolvedCell(directCell, extendedCell),
            "one-sided extension resolved tile matches direct crossing at " + std::to_string(tileX) + "," + std::to_string(tileY) +
                "\ndirect: " + ResolvedCellSummary(directCell) +
                "\nextended: " + ResolvedCellSummary(extendedCell));
        runner.expect(CrosswalkEdges(extendedCell) == CrosswalkEdges(directCell), "one-sided extension crosswalk graphics match direct crossing");
    }
}

void TestTwoWayStraightDirectionInvariant(TestRunner& runner) {
    TransportNetwork forwardNetwork = MakeNetwork(12, 12);
    TransportNetwork reverseNetwork = MakeNetwork(12, 12);
    std::vector<int> forwardLotOccupancy(forwardNetwork.totalTileCount(), kInvalidLotId);
    std::vector<int> reverseLotOccupancy(reverseNetwork.totalTileCount(), kInvalidLotId);

    runner.expect(Place(forwardNetwork, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), forwardLotOccupancy), "two-way forward straight placement succeeds");
    runner.expect(Place(reverseNetwork, MakeStroke(Int2(8, 5), Int2(2, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), reverseLotOccupancy), "two-way reverse straight placement succeeds");
    runner.expect(NetworkTopologySignature(forwardNetwork) == NetworkTopologySignature(reverseNetwork), "two-way straight road is direction invariant");
}

void TestTwoWayCornerDirectionInvariant(TestRunner& runner) {
    TransportNetwork forwardNetwork = MakeNetwork(12, 12);
    TransportNetwork reverseNetwork = MakeNetwork(12, 12);
    std::vector<int> forwardLotOccupancy(forwardNetwork.totalTileCount(), kInvalidLotId);
    std::vector<int> reverseLotOccupancy(reverseNetwork.totalTileCount(), kInvalidLotId);

    runner.expect(Place(forwardNetwork, MakeCornerStroke(Int2(2, 5), Int2(5, 5), Int2(5, 8), RoadFamily::LocalStreet, TransportLayerId::Ground), forwardLotOccupancy), "two-way forward corner placement succeeds");
    runner.expect(Place(reverseNetwork, MakeCornerStroke(Int2(5, 8), Int2(5, 5), Int2(2, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), reverseLotOccupancy), "two-way reverse corner placement succeeds");
    runner.expect(NetworkTopologySignature(forwardNetwork) == NetworkTopologySignature(reverseNetwork), "two-way corner road is direction invariant for identical owned tiles");
}

void TestCrossingRepaintOrderInvariant(TestRunner& runner) {
    TransportNetwork horizontalFirstNetwork = MakeNetwork(12, 12);
    TransportNetwork verticalFirstNetwork = MakeNetwork(12, 12);
    std::vector<int> horizontalFirstLotOccupancy(horizontalFirstNetwork.totalTileCount(), kInvalidLotId);
    std::vector<int> verticalFirstLotOccupancy(verticalFirstNetwork.totalTileCount(), kInvalidLotId);

    runner.expect(Place(horizontalFirstNetwork, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), horizontalFirstLotOccupancy), "crossing horizontal-first horizontal stroke succeeds");
    runner.expect(Place(horizontalFirstNetwork, MakeStroke(Int2(5, 2), Int2(5, 8), RoadFamily::LocalStreet, TransportLayerId::Ground), horizontalFirstLotOccupancy), "crossing horizontal-first vertical stroke succeeds");
    runner.expect(Place(verticalFirstNetwork, MakeStroke(Int2(5, 2), Int2(5, 8), RoadFamily::LocalStreet, TransportLayerId::Ground), verticalFirstLotOccupancy), "crossing vertical-first vertical stroke succeeds");
    runner.expect(Place(verticalFirstNetwork, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), verticalFirstLotOccupancy), "crossing vertical-first horizontal stroke succeeds");
    runner.expect(NetworkTopologySignature(horizontalFirstNetwork) == NetworkTopologySignature(verticalFirstNetwork), "overlap repaint crossing is order invariant");
}

void TestOneWayReverseDirectionDiffers(TestRunner& runner) {
    TransportNetwork forwardNetwork = MakeNetwork(12, 12);
    TransportNetwork reverseNetwork = MakeNetwork(12, 12);
    std::vector<int> forwardLotOccupancy(forwardNetwork.totalTileCount(), kInvalidLotId);
    std::vector<int> reverseLotOccupancy(reverseNetwork.totalTileCount(), kInvalidLotId);

    runner.expect(Place(forwardNetwork, MakeStroke(Int2(2, 5), Int2(8, 5), RoadFamily::LocalStreet, TransportLayerId::Ground, RoadDirectionMode::OneWayForward), forwardLotOccupancy), "one-way forward stroke succeeds");
    runner.expect(Place(reverseNetwork, MakeStroke(Int2(8, 5), Int2(2, 5), RoadFamily::LocalStreet, TransportLayerId::Ground, RoadDirectionMode::OneWayForward), reverseLotOccupancy), "one-way reversed stroke succeeds");
    runner.expect(DirectionGridForMode(forwardNetwork, TransportMode::Car, 0, 0, forwardNetwork.width() - 1, forwardNetwork.height() - 1) !=
        DirectionGridForMode(reverseNetwork, TransportMode::Car, 0, 0, reverseNetwork.width() - 1, reverseNetwork.height() - 1), "one-way reversed strokes intentionally differ");
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
    runner.expect(DirectionCost(pedestrianCell, kRoadDirectionEast) == 500u, "pedestrian lane uses 2 tiles per time at capacity");
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
    costMap.trafficLoadStateForMutation(CommuteTimeOfDay::Morning).cells[costMap.nodeId(TransportLayerId::Ground, TransportMode::Car, 0)].oldLoads[RoadDirectionIndex(kRoadDirectionEast)] = 20u;

    TransportPathScratch scratch;
    TransportPathResult result;
    runner.expect(costMap.findPath(MakePathRequest(costMap.nodeId(TransportLayerId::Ground, TransportMode::Car, 0), costMap.nodeId(TransportLayerId::Ground, TransportMode::Car, 1)), scratch, result), "congestion curve path succeeds");
    runner.expect(result.totalCost > 2199.0f && result.totalCost < 2201.0f, "congestion speed multiplier doubles cost after car start cost");
}

void TestTransportModeStartCosts(TestRunner& runner) {
    TransportCostMap costMap;
    costMap.initialize(2, 1);
    costMap.addDirectionalCost(TransportLayerId::Ground, TransportMode::Car, 0, kRoadDirectionEast, 100u, 100u);
    costMap.addDirectionalCost(TransportLayerId::Ground, TransportMode::Pedestrian, 0, kRoadDirectionEast, 100u, 100u);

    TransportPathScratch scratch;
    TransportPathResult result;

    TransportPathRequest carRequest = MakePathRequest(
        costMap.nodeId(TransportLayerId::Ground, TransportMode::Car, 0),
        costMap.nodeId(TransportLayerId::Ground, TransportMode::Car, 1));
    carRequest.useCongestion = false;
    runner.expect(costMap.findPath(carRequest, scratch, result), "car start cost path succeeds");
    runner.expect(result.totalCost > 2099.0f && result.totalCost < 2101.0f, "car start cost adds two commute-time units");

    TransportPathRequest pedestrianRequest = MakePathRequest(
        costMap.nodeId(TransportLayerId::Ground, TransportMode::Pedestrian, 0),
        costMap.nodeId(TransportLayerId::Ground, TransportMode::Pedestrian, 1));
    pedestrianRequest.useCongestion = false;
    runner.expect(costMap.findPath(pedestrianRequest, scratch, result), "walking start cost path succeeds");
    runner.expect(result.totalCost > 99.0f && result.totalCost < 101.0f, "walking start cost adds no commute-time units");

    carRequest.maximumCost = 2000.0f;
    runner.expect(!costMap.findPath(carRequest, scratch, result), "car start cost counts against maximum path cost");
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

    costMap.beginNextLoadFromOldLoad(CommuteTimeOfDay::Morning);
    costMap.applyPathLoad(CommuteTimeOfDay::Morning, result, 7u, true);
    std::vector<int> touchedTiles;
    costMap.commitNextLoad(CommuteTimeOfDay::Morning, &touchedTiles);
    runner.expect(costMap.trafficLoadState(CommuteTimeOfDay::Morning).cells[costMap.nodeId(TransportLayerId::Ground, TransportMode::Car, 0)].oldLoads[RoadDirectionIndex(kRoadDirectionEast)] == 7u, "morning path load adds to old load after commit");
    runner.expect(costMap.trafficLoadState(CommuteTimeOfDay::Evening).cells[costMap.nodeId(TransportLayerId::Ground, TransportMode::Car, 0)].oldLoads[RoadDirectionIndex(kRoadDirectionEast)] == 0u, "evening path load remains independent");
    runner.expect(touchedTiles.size() == 1u && touchedTiles[0] == 0, "path load commit reports touched movement tile");

    std::vector<std::uint8_t> overlayPixels;
    costMap.buildTrafficOverlay(overlayPixels);
    runner.expect(overlayPixels[3] == kTrafficOverlayAlphaByte, "traffic overlay marks relevant tile alpha");
    runner.expect(overlayPixels[0] > overlayPixels[1], "traffic overlay shifts toward red under load");

    costMap.beginNextLoadFromOldLoad(CommuteTimeOfDay::Evening);
    costMap.applyPathLoad(CommuteTimeOfDay::Evening, result, 10u, true);
    costMap.commitNextLoad(CommuteTimeOfDay::Evening);
    costMap.buildTrafficOverlay(overlayPixels);
    runner.expect(overlayPixels[0] == 255u, "traffic overlay uses worst morning/evening utilization");

    costMap.beginNextLoadFromOldLoad(CommuteTimeOfDay::Morning);
    costMap.applyPathLoad(CommuteTimeOfDay::Morning, result, 3u, false);
    costMap.commitNextLoad(CommuteTimeOfDay::Morning);
    runner.expect(costMap.trafficLoadState(CommuteTimeOfDay::Morning).cells[costMap.nodeId(TransportLayerId::Ground, TransportMode::Car, 0)].oldLoads[RoadDirectionIndex(kRoadDirectionEast)] == 4u, "path load subtraction removes previous assignment");
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
    costMap.trafficLoadStateForMutation(CommuteTimeOfDay::Morning).cells[costMap.nodeId(TransportLayerId::Ground, TransportMode::Car, 0)].oldLoads[RoadDirectionIndex(kRoadDirectionEast)] = 100u;
    costMap.trafficLoadStateForMutation(CommuteTimeOfDay::Morning).cells[costMap.nodeId(TransportLayerId::Ground, TransportMode::Car, 1)].oldLoads[RoadDirectionIndex(kRoadDirectionEast)] = 100u;

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

void TestRoadRemovalTouchesOnlyAffectedTrafficOverlayChunks(TestRunner& runner) {
    TransportNetwork network = MakeTwoColumnChunkNetwork(16, 12);
    std::vector<int> lotOccupancy(network.totalTileCount(), kInvalidLotId);

    runner.expect(Place(network, MakeStroke(Int2(1, 5), Int2(6, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "left traffic overlay road placement succeeds");
    runner.expect(Place(network, MakeStroke(Int2(10, 5), Int2(14, 5), RoadFamily::LocalStreet, TransportLayerId::Ground), lotOccupancy), "right traffic overlay road placement succeeds");
    runner.expect(network.trafficOverlayChunkRevisions().size() == 2u, "traffic overlay chunk test uses two chunks");

    const std::vector<std::uint64_t> revisionsBeforeRemove = network.trafficOverlayChunkRevisions();
    const int removedTileIndex = (5 * network.width()) + 3;
    const int retainedTileIndex = (5 * network.width()) + 12;
    const std::size_t removedPixelOffset = static_cast<std::size_t>(removedTileIndex) * 4u;
    const std::size_t retainedPixelOffset = static_cast<std::size_t>(retainedTileIndex) * 4u;

    runner.expect(network.trafficOverlayState()[removedPixelOffset + 3u] == kTrafficOverlayAlphaByte, "traffic overlay marks road before removal");
    runner.expect(network.trafficOverlayState()[retainedPixelOffset + 3u] == kTrafficOverlayAlphaByte, "traffic overlay marks unrelated road before removal");
    runner.expect(network.removeRoadAtTile(3, 5), "traffic overlay road removal succeeds");

    runner.expect(network.trafficOverlayState()[removedPixelOffset + 3u] == 0u, "traffic overlay clears removed road tile");
    runner.expect(network.trafficOverlayState()[retainedPixelOffset + 3u] == kTrafficOverlayAlphaByte, "traffic overlay keeps unrelated road tile");
    runner.expect(network.trafficOverlayChunkRevisions()[0] > revisionsBeforeRemove[0], "traffic overlay bumps affected chunk after road removal");
    runner.expect(network.trafficOverlayChunkRevisions()[1] == revisionsBeforeRemove[1], "traffic overlay leaves unaffected chunk revision unchanged");
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
    const LotModule* trailerModule = FindModule(assets, "trailer_module");
    const LotModule* largerHouseModule = FindModule(assets, "larger_house_module");
    const LotModule* smallApartmentModule = FindModule(assets, "small_apartment_module");
    const LotModule* workshopModule = FindModule(assets, "workshop_module");
    const LotModule* largeWarehouseModule = FindModule(assets, "large_warehouse_module");
    const LotModule* rowHouseModule = FindModule(assets, "row_house_module");
    const LotModule* walkupApartmentModule = FindModule(assets, "walkup_apartment_module");
    const LotModule* apartmentBlockModule = FindModule(assets, "apartment_block_module");
    const LotModule* freightWarehouseModule = FindModule(assets, "freight_warehouse_module");
    const LotModule* largeFactoryModule = FindModule(assets, "large_factory_module");
    const LotModule* distributionCenterModule = FindModule(assets, "distribution_center_module");
    const LotModule* smokestackRowModule = FindModule(assets, "smokestack_row_module");
    const LotAsset* factoryLot = FindLotAsset(assets, "factory_lot");
    const LotAsset* houseLot = FindLotAsset(assets, "house_lot");
    const LotAsset* residential4x4Lot = FindLotAsset(assets, "rci_residential_4x4_lot");
    const LotAsset* residential4x4CourtyardLot = FindLotAsset(assets, "rci_residential_4x4_courtyard_lot");
    const LotAsset* residential4x4WalkupLot = FindLotAsset(assets, "rci_residential_4x4_walkup_lot");
    const LotAsset* residential4x6Lot = FindLotAsset(assets, "rci_residential_4x6_lot");
    const LotAsset* residential4x8Lot = FindLotAsset(assets, "rci_residential_4x8_lot");
    const LotAsset* residential8x8Lot = FindLotAsset(assets, "rci_residential_8x8_lot");
    const LotAsset* industrial4x4Lot = FindLotAsset(assets, "rci_industrial_4x4_lot");
    const LotAsset* industrial4x4SmokestacksLot = FindLotAsset(assets, "rci_industrial_4x4_smokestacks_lot");
    const LotAsset* industrial4x6Lot = FindLotAsset(assets, "rci_industrial_4x6_lot");
    const LotAsset* industrial4x8Lot = FindLotAsset(assets, "rci_industrial_4x8_lot");
    const LotAsset* industrial8x8Lot = FindLotAsset(assets, "rci_industrial_8x8_lot");
    const RciGrowthRule* residentialGrowthRule = FindGrowthRule(assets, TileZoningResidential);
    const RciGrowthRule* industrialGrowthRule = FindGrowthRule(assets, TileZoningIndustrial);

    runner.expect(warehouseModule != 0, "warehouse module exists");
    runner.expect(smokestackModule != 0, "smokestack module exists");
    runner.expect(houseModule != 0, "house module exists");
    runner.expect(drivewayModule != 0, "driveway module exists");
    runner.expect(gardenModule != 0, "garden module exists");
    runner.expect(trailerModule != 0, "trailer module exists");
    runner.expect(largerHouseModule != 0, "larger house module exists");
    runner.expect(smallApartmentModule != 0, "small apartment module exists");
    runner.expect(workshopModule != 0, "workshop module exists");
    runner.expect(largeWarehouseModule != 0, "large warehouse module exists");
    runner.expect(rowHouseModule != 0, "row house module exists");
    runner.expect(walkupApartmentModule != 0, "walkup apartment module exists");
    runner.expect(apartmentBlockModule != 0, "apartment block module exists");
    runner.expect(freightWarehouseModule != 0, "freight warehouse module exists");
    runner.expect(largeFactoryModule != 0, "large factory module exists");
    runner.expect(distributionCenterModule != 0, "distribution center module exists");
    runner.expect(smokestackRowModule != 0, "smokestack row module exists");
    runner.expect(factoryLot != 0, "factory lot exists");
    runner.expect(houseLot != 0, "house lot exists");
    runner.expect(residential4x4Lot != 0, "residential 4x4 lot exists");
    runner.expect(residential4x4CourtyardLot != 0, "residential 4x4 courtyard variation exists");
    runner.expect(residential4x4WalkupLot != 0, "residential 4x4 walkup lot exists");
    runner.expect(residential4x6Lot != 0, "residential 4x6 lot exists");
    runner.expect(residential4x8Lot != 0, "residential 4x8 lot exists");
    runner.expect(residential8x8Lot != 0, "residential 8x8 lot exists");
    runner.expect(industrial4x4Lot != 0, "industrial 4x4 lot exists");
    runner.expect(industrial4x4SmokestacksLot != 0, "industrial 4x4 smokestacks variation exists");
    runner.expect(industrial4x6Lot != 0, "industrial 4x6 lot exists");
    runner.expect(industrial4x8Lot != 0, "industrial 4x8 lot exists");
    runner.expect(industrial8x8Lot != 0, "industrial 8x8 lot exists");
    runner.expect(residentialGrowthRule != 0, "residential RCI growth rule exists");
    runner.expect(industrialGrowthRule != 0, "industrial RCI growth rule exists");
    if (warehouseModule == 0 || smokestackModule == 0 || houseModule == 0 || drivewayModule == 0 || gardenModule == 0 || factoryLot == 0 || houseLot == 0) {
        return;
    }

    runner.expect(ModuleParameterAmount(*warehouseModule, registry.jobsDirtyIndustryId()) == 6.0f, "warehouse contributes six dirty industry jobs");
    runner.expect(ModuleParameterAmount(*houseModule, registry.residentsLowWealthId()) == 5.0f, "house contributes five low wealth residents");
    if (trailerModule != 0) {
        runner.expect(ModuleParameterAmount(*trailerModule, registry.residentsLowWealthId()) == 2.0f, "trailer contributes two low wealth residents");
    }
    if (largerHouseModule != 0) {
        runner.expect(ModuleParameterAmount(*largerHouseModule, registry.residentsLowWealthId()) == 7.0f, "larger house contributes seven low wealth residents");
    }
    if (smallApartmentModule != 0) {
        runner.expect(ModuleParameterAmount(*smallApartmentModule, registry.residentsLowWealthId()) == 14.0f, "small apartment contributes low wealth residents");
    }
    if (workshopModule != 0) {
        runner.expect(ModuleParameterAmount(*workshopModule, registry.jobsDirtyIndustryId()) == 4.0f, "workshop contributes four dirty industry jobs");
    }
    if (largeWarehouseModule != 0) {
        runner.expect(ModuleParameterAmount(*largeWarehouseModule, registry.jobsDirtyIndustryId()) == 12.0f, "large warehouse contributes twelve dirty industry jobs");
    }
    if (rowHouseModule != 0) {
        runner.expect(ModuleParameterAmount(*rowHouseModule, registry.residentsLowWealthId()) == 8.0f, "row house contributes eight low wealth residents");
    }
    if (walkupApartmentModule != 0) {
        runner.expect(ModuleParameterAmount(*walkupApartmentModule, registry.residentsLowWealthId()) == 16.0f, "walkup apartment contributes sixteen low wealth residents");
    }
    if (apartmentBlockModule != 0) {
        runner.expect(ModuleParameterAmount(*apartmentBlockModule, registry.residentsLowWealthId()) == 40.0f, "apartment block contributes forty low wealth residents");
    }
    if (freightWarehouseModule != 0) {
        runner.expect(ModuleParameterAmount(*freightWarehouseModule, registry.jobsDirtyIndustryId()) == 30.0f, "freight warehouse contributes thirty dirty industry jobs");
    }
    if (largeFactoryModule != 0) {
        runner.expect(ModuleParameterAmount(*largeFactoryModule, registry.jobsDirtyIndustryId()) == 40.0f, "large factory contributes forty dirty industry jobs");
    }
    if (distributionCenterModule != 0) {
        runner.expect(ModuleParameterAmount(*distributionCenterModule, registry.jobsDirtyIndustryId()) == 36.0f, "distribution center contributes dirty industry jobs");
    }
    if (smokestackRowModule != 0) {
        runner.expect(ModuleParameterAmount(*smokestackRowModule, registry.jobsDirtyIndustryId()) == 0.0f, "smokestack row is decorative capacity");
    }
    runner.expect(factoryLot->footprintWidth == 3 && factoryLot->footprintHeight == 2, "factory footprint fits warehouse and adjacent smokestack");
    runner.expect(houseLot->footprintWidth == 2 && houseLot->footprintHeight == 4, "house footprint is 2x4");
    runner.expect(factoryLot->accessDefinitions.size() == 8u, "factory declares car and pedestrian access around all warehouse edges");
    runner.expect(houseLot->accessDefinitions.size() == 2u, "house declares driveway and garden access");
    runner.expect(!assets.congestionCurve.points.empty() && assets.congestionCurve.points[0].speedMultiplier > 0.0f, "transport congestion XML loads");

    if (residentialGrowthRule != 0) {
        runner.expect(residentialGrowthRule->desirabilityThreshold == 60, "residential desirability threshold loads");
        runner.expect(residentialGrowthRule->densityPoints.size() == 3u, "residential density has three population points");
        runner.expect(std::fabs(InterpolatedDensity(*residentialGrowthRule, 0) - 1.0f) < 0.001f, "residential density starts at one per tile");
        runner.expect(std::fabs(InterpolatedDensity(*residentialGrowthRule, 2000) - 2.0f) < 0.001f, "residential density reaches two per tile at 2000 population");
        runner.expect(std::fabs(InterpolatedDensity(*residentialGrowthRule, 10000) - 3.0f) < 0.001f, "residential density reaches three per tile at 10000 population");
        runner.expect(std::fabs(InterpolatedDensity(*residentialGrowthRule, 6000) - 2.5f) < 0.001f, "residential density interpolates between XML points");
    }
    if (industrialGrowthRule != 0) {
        runner.expect(industrialGrowthRule->desirabilityThreshold == 60, "industrial desirability threshold loads");
        runner.expect(industrialGrowthRule->densityPoints.size() == 1u, "industrial density has one flat point");
        runner.expect(std::fabs(InterpolatedDensity(*industrialGrowthRule, 0) - 3.0f) < 0.001f, "industrial density starts at three per tile");
        runner.expect(std::fabs(InterpolatedDensity(*industrialGrowthRule, 10000) - 3.0f) < 0.001f, "industrial density remains flat");
    }

    if (residential4x4Lot != 0 && residential4x4CourtyardLot != 0 && residential4x4WalkupLot != 0 && residential4x6Lot != 0 && residential4x8Lot != 0 && residential8x8Lot != 0) {
        runner.expect(residential4x4Lot->footprintWidth == 4 && residential4x4Lot->footprintHeight == 4, "residential 4x4 footprint loads");
        runner.expect(residential4x4Lot->accessDefinitions.size() == 16u, "residential 4x4 perimeter access expands");
        runner.expect(residential8x8Lot->footprintWidth == 8 && residential8x8Lot->footprintHeight == 8, "residential 8x8 footprint loads");
        runner.expect(residential8x8Lot->accessDefinitions.size() == 32u, "residential 8x8 perimeter access expands");
        runner.expect(LotAssetCapacity(assets, *residential4x4Lot, registry.residentsLowWealthId()) == 16.0f, "residential 4x4 stays within zero-population density cap");
        runner.expect(LotAssetCapacity(assets, *residential4x4CourtyardLot, registry.residentsLowWealthId()) == 16.0f, "residential 4x4 decorative variation preserves capacity");
        runner.expect(LotAssetCapacity(assets, *residential4x4WalkupLot, registry.residentsLowWealthId()) == 32.0f, "residential 4x4 walkup is denser than row houses");
        runner.expect(LotAssetCapacity(assets, *residential4x6Lot, registry.residentsLowWealthId()) == 48.0f, "residential 4x6 capacity loads");
        runner.expect(LotAssetCapacity(assets, *residential4x8Lot, registry.residentsLowWealthId()) == 80.0f, "residential 4x8 capacity loads");
        runner.expect(LotAssetCapacity(assets, *residential8x8Lot, registry.residentsLowWealthId()) == 160.0f, "residential 8x8 capacity loads");
    }

    if (industrial4x4Lot != 0 && industrial4x4SmokestacksLot != 0 && industrial4x6Lot != 0 && industrial4x8Lot != 0 && industrial8x8Lot != 0) {
        runner.expect(industrial4x4Lot->footprintWidth == 4 && industrial4x4Lot->footprintHeight == 4, "industrial 4x4 footprint loads");
        runner.expect(industrial4x4Lot->accessDefinitions.size() == 16u, "industrial 4x4 perimeter access expands");
        runner.expect(industrial8x8Lot->footprintWidth == 8 && industrial8x8Lot->footprintHeight == 8, "industrial 8x8 footprint loads");
        runner.expect(industrial8x8Lot->accessDefinitions.size() == 32u, "industrial 8x8 perimeter access expands");
        runner.expect(LotAssetCapacity(assets, *industrial4x4Lot, registry.jobsDirtyIndustryId()) == 40.0f, "industrial 4x4 capacity loads");
        runner.expect(LotAssetCapacity(assets, *industrial4x4SmokestacksLot, registry.jobsDirtyIndustryId()) == 30.0f, "industrial smokestack variation capacity loads");
        runner.expect(LotAssetCapacity(assets, *industrial4x6Lot, registry.jobsDirtyIndustryId()) == 40.0f, "industrial 4x6 capacity loads");
        runner.expect(LotAssetCapacity(assets, *industrial4x8Lot, registry.jobsDirtyIndustryId()) == 80.0f, "industrial 4x8 capacity loads");
        runner.expect(LotAssetCapacity(assets, *industrial8x8Lot, registry.jobsDirtyIndustryId()) == 136.0f, "industrial 8x8 capacity loads under density cap");
    }

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

    int constructorWidth = 2;
    for (; constructorWidth <= 3; ++constructorWidth) {
        int constructorDepth = 2;
        for (; constructorDepth <= 8; ++constructorDepth) {
            std::ostringstream residentialId;
            residentialId << "rci_residential_" << constructorWidth << "x" << constructorDepth << "_lot";
            const LotAsset* residentialLot = FindLotAsset(assets, residentialId.str());
            runner.expect(residentialLot != 0, residentialId.str() + " exists");
            if (residentialLot != 0) {
                runner.expect(residentialLot->zoningType == TileZoningResidential, residentialId.str() + " is residential");
                runner.expect(residentialLot->footprintWidth == constructorWidth && residentialLot->footprintHeight == constructorDepth, residentialId.str() + " matches constructor footprint");
                runner.expect(residentialLot->accessDefinitions.size() == static_cast<std::size_t>((constructorWidth + constructorDepth) * 2), residentialId.str() + " expands perimeter access");
            }

            std::ostringstream industrialId;
            industrialId << "rci_industrial_" << constructorWidth << "x" << constructorDepth << "_lot";
            const LotAsset* industrialLot = FindLotAsset(assets, industrialId.str());
            runner.expect(industrialLot != 0, industrialId.str() + " exists");
            if (industrialLot != 0) {
                runner.expect(industrialLot->zoningType == TileZoningIndustrial, industrialId.str() + " is industrial");
                runner.expect(industrialLot->footprintWidth == constructorWidth && industrialLot->footprintHeight == constructorDepth, industrialId.str() + " matches constructor footprint");
                runner.expect(industrialLot->accessDefinitions.size() == static_cast<std::size_t>((constructorWidth + constructorDepth) * 2), industrialId.str() + " expands perimeter access");
            }
        }
    }
}

void TestPopulationParameterAggregation(TestRunner& runner) {
    CityParameterRegistry registry;
    std::vector<float> cityParameters(registry.count(), 0.0f);
    cityParameters[registry.residentsLowWealthId()] = 5.0f;
    cityParameters[registry.residentsMediumWealthId()] = 7.0f;
    cityParameters[registry.residentsHighWealthId()] = 11.0f;
    cityParameters[registry.jobsLowWealthId()] = 99.0f;

    runner.expect(CalculatePopulationFromCityParameters(cityParameters, registry) == 23, "population sums only resident wealth parameters");
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

    const std::string badZoningLot =
        "<lot id=\"test_lot\" zoningType=\"retail\">"
        "<anchor x=\"0\" y=\"0\" />"
        "<footprint x=\"0\" y=\"0\" width=\"1\" height=\"1\" />"
        "<modules><moduleRef id=\"test_module\" x=\"0\" y=\"0\" /></modules>"
        "</lot>";
    runner.expect(InvalidAssetsRejected(validModule, badZoningLot, registry), "unknown lot zoning type rejects at load");

    const std::string rciModule =
        "<module id=\"test_module\">"
        "<size width=\"1\" height=\"1\" />"
        "<effects airPollution=\"0\" landValue=\"0\" />"
        "<parameters><driver id=\"residents.low_wealth\" amount=\"1\" /></parameters>"
        "</module>";
    const std::string rciLot =
        "<lot id=\"test_lot\" zoningType=\"residential\">"
        "<anchor x=\"0\" y=\"0\" />"
        "<footprint x=\"0\" y=\"0\" width=\"1\" height=\"1\" />"
        "<modules><moduleRef id=\"test_module\" x=\"0\" y=\"0\" /></modules>"
        "</lot>";

    {
        const std::string root = MakeTempAssetDirectory("CityBuilderAssetMissingRciGrowth");
        WriteTextAssetFile(root + "\\Modules\\test_module.xml", rciModule);
        WriteTextAssetFile(root + "\\Lots\\test_lot.xml", rciLot);
        LoadedGameAssets assets;
        std::string errorMessage;
        ScopedCrashLogSuppression suppressExpectedAssetErrors;
        runner.expect(!LoadGameAssets(root, registry, assets, errorMessage) && !errorMessage.empty(), "constructor-enabled RCI lot without growth rule rejects at load");
    }

    {
        const std::string root = MakeTempAssetDirectory("CityBuilderAssetBadRciGrowth");
        WriteTextAssetFile(root + "\\Modules\\test_module.xml", rciModule);
        WriteTextAssetFile(root + "\\Lots\\test_lot.xml", rciLot);
        WriteTextAssetFile(
            root + "\\RCI\\rci_tools.xml",
            "<rciTools>"
            "<tool id=\"residential\" zoningType=\"residential\" />"
            "<rciGrowth zoningType=\"residential\" desirabilityThreshold=\"60\">"
            "</rciGrowth>"
            "</rciTools>");
        LoadedGameAssets assets;
        std::string errorMessage;
        ScopedCrashLogSuppression suppressExpectedAssetErrors;
        runner.expect(!LoadGameAssets(root, registry, assets, errorMessage) && !errorMessage.empty(), "RCI growth rule without density entries rejects at load");
    }

    {
        const std::string root = MakeTempAssetDirectory("CityBuilderAssetDuplicateRciGrowthDensity");
        WriteTextAssetFile(root + "\\Modules\\test_module.xml", rciModule);
        WriteTextAssetFile(root + "\\Lots\\test_lot.xml", rciLot);
        WriteTextAssetFile(
            root + "\\RCI\\rci_tools.xml",
            "<rciTools>"
            "<tool id=\"residential\" zoningType=\"residential\" />"
            "<rciGrowth zoningType=\"residential\" desirabilityThreshold=\"60\">"
            "<maxDensityPerTile population=\"0\" value=\"1\" />"
            "<maxDensityPerTile population=\"0\" value=\"2\" />"
            "</rciGrowth>"
            "</rciTools>");
        LoadedGameAssets assets;
        std::string errorMessage;
        ScopedCrashLogSuppression suppressExpectedAssetErrors;
        runner.expect(!LoadGameAssets(root, registry, assets, errorMessage) && !errorMessage.empty(), "duplicate RCI density population rejects at load");
    }
}
}

int main() {
    TestRunner runner;
    TestStraightTwoWayLocalStreet(runner);
    TestOneWayLocalStreet(runner);
    TestOneWayLaneMinimums(runner);
    TestSeparatorLaneSandwichRules(runner);
    TestPerpendicularCrosswalkRequiresLaneContinuation(runner);
    TestPerpendicularCrosswalkIsOrderIndependent(runner);
    TestTSectionRetexturesRealSidewalkCrosswalks(runner);
    TestJoggedSidewalkDoesNotBecomeCrosswalk(runner);
    TestCornerDoesNotRenderCrosswalks(runner);
    TestRoadToolSandboxFixtureCases(runner, false);
    TestTurnArrowsRenderAheadOfIntersectionsOnly(runner);
    TestSingleStrokeCornerCleanupUsesValidCornerMasks(runner);
    TestRemoveRoadTileClearsTwoTileFootprint(runner);
    TestRemoveRoadTileClearsFourTileFootprint(runner);
    TestShortTwoTileRoadRemnantIsRemoved(runner);
    TestShortFourTileRoadRemnantIsRemoved(runner);
    TestRemovingApproachDoesNotLeavePartialIntersectionCrosswalk(runner);
    TestWideRoadCleanupPropagatesAcrossIntersectionBody(runner);
    TestCornerUpgradeMatchesDirectFourWay(runner);
    TestCornerUpgradeWithOnlyMissingArmsMatchesDirectFourWay(runner);
    TestOpposingStubsDoNotConnectAcrossTwoLaneRoad(runner);
    TestOneSidedExtensionCarriesPedestrianLaneAcrossRoad(runner);
    TestTwoWayStraightDirectionInvariant(runner);
    TestTwoWayCornerDirectionInvariant(runner);
    TestCrossingRepaintOrderInvariant(runner);
    TestOneWayReverseDirectionDiffers(runner);
    TestSameAxisOffsetRejects(runner);
    TestExactReplayDoesNotAdvanceRevision(runner);
    TestElevatedHighwayHasNoPedestrianGraphics(runner);
    TestGroundRoadRejectsLotOccupancy(runner);
    TestDirectionalOneWayCostMap(runner);
    TestLocalLaneSpeedCosts(runner);
    TestCostMapLowerCostAndCapacityAccumulation(runner);
    TestCongestionCurveReducesSpeedFromTable(runner);
    TestTransportModeStartCosts(runner);
    TestHighwayDoesNotExposeBuildingAccess(runner);
    TestBuildingAccessCandidatesFromLocalStreet(runner);
    TestLayerIsolationWithoutTransfer(runner);
    TestExplicitTransferConnectsModesAndLayers(runner);
    TestPathLoadAssignmentAndOverlay(runner);
    TestCongestionReroutesPath(runner);
    TestEqualRouteJitterSpreadsChoices(runner);
    TestTrafficOverlayStartsGreenOnRoadCapacity(runner);
    TestRoadRemovalTouchesOnlyAffectedTrafficOverlayChunks(runner);
    TestLotConstructionDurationLoading(runner);
    TestFactoryHouseAssetsAndParameters(runner);
    TestPopulationParameterAggregation(runner);
    TestInvalidAssetValidation(runner);

    std::cout << "TransportNetworkTests: " << runner.passed << " passed, " << runner.failed << " failed." << std::endl;
    return runner.failed == 0 ? 0 : 1;
}
