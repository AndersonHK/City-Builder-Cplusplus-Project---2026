#include "TransportCostMap.h"

#include <algorithm>
#include <cmath>
#include <queue>

namespace {
const float kMinimumCongestedSpeedMultiplier = 0.01f;
const float kRouteJitterScale = 0.0001f;
const float kTransportCostUnitsPerCommuteTime = 1000.0f;
const float kCarStartCost = 2.0f * kTransportCostUnitsPerCommuteTime;
const float kPedestrianStartCost = 0.0f * kTransportCostUnitsPerCommuteTime;

std::uint16_t SaturatingAdd(std::uint16_t left, std::uint16_t right) {
    const unsigned int sum = static_cast<unsigned int>(left) + static_cast<unsigned int>(right);
    return sum > kTransportMaxLoad ? kTransportMaxLoad : static_cast<std::uint16_t>(sum);
}

std::uint16_t SaturatingSubtract(std::uint16_t left, std::uint16_t right) {
    return right > left ? 0u : static_cast<std::uint16_t>(left - right);
}

float CongestionSpeedMultiplier(const TransportCongestionCurve& congestionCurve, std::uint16_t load, std::uint16_t capacity) {
    if (capacity == 0u) {
        return 1.0f;
    }

    const float utilization = static_cast<float>(load) / static_cast<float>(capacity);
    if (congestionCurve.points.empty()) {
        return 1.0f;
    }

    if (utilization <= congestionCurve.points.front().utilization) {
        return std::max(kMinimumCongestedSpeedMultiplier, congestionCurve.points.front().speedMultiplier);
    }

    std::size_t pointIndex = 1;
    for (; pointIndex < congestionCurve.points.size(); ++pointIndex) {
        const TransportCongestionPoint& upperPoint = congestionCurve.points[pointIndex];
        if (utilization > upperPoint.utilization) {
            continue;
        }

        const TransportCongestionPoint& lowerPoint = congestionCurve.points[pointIndex - 1u];
        const float span = upperPoint.utilization - lowerPoint.utilization;
        if (span <= 0.0001f) {
            return std::max(kMinimumCongestedSpeedMultiplier, upperPoint.speedMultiplier);
        }

        const float t = (utilization - lowerPoint.utilization) / span;
        const float speedMultiplier = lowerPoint.speedMultiplier + ((upperPoint.speedMultiplier - lowerPoint.speedMultiplier) * t);
        return std::max(kMinimumCongestedSpeedMultiplier, speedMultiplier);
    }

    return std::max(kMinimumCongestedSpeedMultiplier, congestionCurve.points.back().speedMultiplier);
}

float TransportModeStartCost(TransportMode mode) {
    switch (mode) {
        case TransportMode::Car:
            return kCarStartCost;

        case TransportMode::Pedestrian:
            return kPedestrianStartCost;

        default:
            return 0.0f;
    }
}

struct HeapCompare {
    bool operator()(const TransportPathScratch::HeapEntry& left, const TransportPathScratch::HeapEntry& right) const {
        return left.priority > right.priority;
    }
};

void WriteTrafficOverlayPixel(const TransportCostMap& costMap, int tileIndex, std::vector<std::uint8_t>& overlayPixels) {
    const std::size_t pixelOffset = static_cast<std::size_t>(tileIndex) * 4u;
    overlayPixels[pixelOffset + 0u] = 0u;
    overlayPixels[pixelOffset + 1u] = 0u;
    overlayPixels[pixelOffset + 2u] = 0u;
    overlayPixels[pixelOffset + 3u] = 0u;

    bool relevant = false;
    float maxUtilization = 0.0f;

    std::size_t layerIndex = 0;
    for (; layerIndex < static_cast<std::size_t>(TransportLayerId::Count); ++layerIndex) {
        std::size_t modeIndex = 0;
        for (; modeIndex < static_cast<std::size_t>(TransportMode::Count); ++modeIndex) {
            const TransportCostCell& transportCell = costMap.cell(static_cast<TransportLayerId>(layerIndex), static_cast<TransportMode>(modeIndex), tileIndex);
            std::size_t directionIndex = 0;
            for (; directionIndex < kRoadDirectionCount; ++directionIndex) {
                if (transportCell.capacities[directionIndex] == 0u) {
                    continue;
                }

                relevant = true;
                std::size_t commuteTimeIndex = 0;
                for (; commuteTimeIndex < static_cast<std::size_t>(CommuteTimeOfDay::Count); ++commuteTimeIndex) {
                    const TransportTrafficLoadState& loadState = costMap.trafficLoadState(static_cast<CommuteTimeOfDay>(commuteTimeIndex));
                    const std::uint32_t nodeId = costMap.nodeId(static_cast<TransportLayerId>(layerIndex), static_cast<TransportMode>(modeIndex), tileIndex);
                    if (nodeId >= loadState.cells.size()) {
                        continue;
                    }

                    maxUtilization = std::max(maxUtilization, static_cast<float>(loadState.cells[nodeId].oldLoads[directionIndex]) / static_cast<float>(transportCell.capacities[directionIndex]));
                }
            }
        }
    }

    if (!relevant) {
        return;
    }

    const float clampedUtilization = std::max(0.0f, std::min(maxUtilization, 1.0f));
    overlayPixels[pixelOffset + 0u] = static_cast<std::uint8_t>(clampedUtilization * 255.0f + 0.5f);
    overlayPixels[pixelOffset + 1u] = static_cast<std::uint8_t>((1.0f - clampedUtilization) * 255.0f + 0.5f);
    overlayPixels[pixelOffset + 2u] = 0u;
    overlayPixels[pixelOffset + 3u] = kTrafficOverlayAlphaByte;
}
}

TransportCostCell::TransportCostCell()
    : buildingAccessMask(0) {
    clearCosts();
}

void TransportCostCell::clearCosts() {
    std::size_t directionIndex = 0;
    for (; directionIndex < kRoadDirectionCount; ++directionIndex) {
        costs[directionIndex] = kTransportNoCost;
        capacities[directionIndex] = 0u;
    }
    buildingAccessMask = 0;
}

TransportTrafficLoadCell::TransportTrafficLoadCell() {
    clearLoads();
}

void TransportTrafficLoadCell::clearLoads() {
    std::size_t directionIndex = 0;
    for (; directionIndex < kRoadDirectionCount; ++directionIndex) {
        oldLoads[directionIndex] = 0u;
        newLoads[directionIndex] = 0u;
    }
}

TransportTrafficTransferLoad::TransportTrafficTransferLoad()
    : oldLoad(0),
      newLoad(0) {
}

TransportTouchedMovementLoad::TransportTouchedMovementLoad()
    : nodeId(0),
      directionIndex(0u) {
}

TransportTouchedMovementLoad::TransportTouchedMovementLoad(std::uint32_t nodeIdValue, std::uint8_t directionIndexValue)
    : nodeId(nodeIdValue),
      directionIndex(directionIndexValue) {
}

TransportTrafficLoadState::TransportTrafficLoadState()
    : nextLoadStartsFromZero(false) {
}

void TransportTrafficLoadState::initialize(std::size_t nodeCount, std::size_t transferCount) {
    cells.assign(nodeCount, TransportTrafficLoadCell());
    transferLoads.assign(transferCount, TransportTrafficTransferLoad());
    touchedMovementLoads.clear();
    touchedTransferLoads.clear();
    touchedMovementFlags.assign(nodeCount * kRoadDirectionCount, false);
    touchedTransferFlags.assign(transferCount, false);
    nextLoadStartsFromZero = false;
}

void TransportTrafficLoadState::clearLoads() {
    std::size_t cellIndex = 0;
    for (; cellIndex < cells.size(); ++cellIndex) {
        cells[cellIndex].clearLoads();
    }

    std::size_t transferIndex = 0;
    for (; transferIndex < transferLoads.size(); ++transferIndex) {
        transferLoads[transferIndex].oldLoad = 0u;
        transferLoads[transferIndex].newLoad = 0u;
    }

    resetTouchedLoads(false);
}

void TransportTrafficLoadState::resetTouchedLoads(bool startsFromZero) {
    std::size_t touchIndex = 0;
    for (; touchIndex < touchedMovementLoads.size(); ++touchIndex) {
        const TransportTouchedMovementLoad& touchedLoad = touchedMovementLoads[touchIndex];
        const std::size_t flagIndex = static_cast<std::size_t>(touchedLoad.nodeId) * kRoadDirectionCount + touchedLoad.directionIndex;
        if (flagIndex < touchedMovementFlags.size()) {
            touchedMovementFlags[flagIndex] = false;
        }
    }

    for (touchIndex = 0; touchIndex < touchedTransferLoads.size(); ++touchIndex) {
        const std::uint32_t transferIndex = touchedTransferLoads[touchIndex];
        if (transferIndex < touchedTransferFlags.size()) {
            touchedTransferFlags[transferIndex] = false;
        }
    }

    touchedMovementLoads.clear();
    touchedTransferLoads.clear();
    nextLoadStartsFromZero = startsFromZero;
}

TransportCongestionPoint::TransportCongestionPoint()
    : utilization(0.0f),
      speedMultiplier(1.0f) {
}

TransportCongestionPoint::TransportCongestionPoint(float utilizationValue, float speedMultiplierValue)
    : utilization(utilizationValue),
      speedMultiplier(speedMultiplierValue) {
}

TransportCongestionCurve::TransportCongestionCurve() {
    points.push_back(TransportCongestionPoint(0.0f, 1.0f));
    points.push_back(TransportCongestionPoint(1.0f, 1.0f));
    points.push_back(TransportCongestionPoint(1.25f, 0.75f));
    points.push_back(TransportCongestionPoint(1.5f, 0.50f));
    points.push_back(TransportCongestionPoint(2.0f, 0.25f));
    points.push_back(TransportCongestionPoint(3.0f, 0.10f));
}

TransportTransferEdge::TransportTransferEdge()
    : fromNodeId(0),
      toNodeId(0),
      cost(0),
      capacity(0) {
}

TransportPathStep::TransportPathStep()
    : fromNodeId(0),
      toNodeId(0),
      roadDirection(0),
      kind(TransportPathStepKind::Movement),
      transferEdgeIndex(0) {
}

TransportPathRequest::TransportPathRequest()
    : routeSeed(0),
      demand(1),
      maximumCost(static_cast<float>(kTransportMaxCost)),
      useCongestion(true),
      commuteTimeOfDay(CommuteTimeOfDay::Morning) {
}

TransportPathResult::TransportPathResult()
    : success(false),
      totalCost(0.0f),
      reachedNodeId(0) {
}

TransportPathScratch::TransportPathScratch()
    : currentStamp(0) {
}

void TransportPathScratch::reset(std::size_t nodeCount) {
    if (costs.size() != nodeCount) {
        costs.assign(nodeCount, 0.0f);
        stamps.assign(nodeCount, 0u);
        closedStamps.assign(nodeCount, 0u);
        goalStamps.assign(nodeCount, 0u);
        parentNodes.assign(nodeCount, std::numeric_limits<std::uint32_t>::max());
        parentDirections.assign(nodeCount, 0u);
        parentKinds.assign(nodeCount, 0u);
        parentTransferEdges.assign(nodeCount, 0u);
        currentStamp = 0;
    }

    ++currentStamp;
    if (currentStamp == 0u) {
        std::fill(stamps.begin(), stamps.end(), 0u);
        std::fill(closedStamps.begin(), closedStamps.end(), 0u);
        std::fill(goalStamps.begin(), goalStamps.end(), 0u);
        currentStamp = 1u;
    }

    heap.clear();
}

TransportCostMap::TransportCostMap()
    : width_(0),
      height_(0),
      totalTileCount_(0),
      totalNodeCount_(0),
      transferOffsetsDirty_(false) {
}

void TransportCostMap::initialize(int width, int height) {
    width_ = width;
    height_ = height;
    totalTileCount_ = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    totalNodeCount_ = totalTileCount_ * static_cast<std::size_t>(TransportLayerId::Count) * static_cast<std::size_t>(TransportMode::Count);
    cells_.assign(totalNodeCount_, TransportCostCell());
    transferEdges_.clear();
    transferOffsets_.assign(totalNodeCount_ + 1u, 0u);
    std::size_t commuteTimeIndexValue = 0;
    for (; commuteTimeIndexValue < trafficLoadStates_.size(); ++commuteTimeIndexValue) {
        trafficLoadStates_[commuteTimeIndexValue].initialize(totalNodeCount_, 0u);
    }
    transferOffsetsDirty_ = false;
}

void TransportCostMap::clear() {
    clearCosts();
    clearLoads();
    clearTransferEdges();
}

void TransportCostMap::clearCosts() {
    std::size_t cellIndex = 0;
    for (; cellIndex < cells_.size(); ++cellIndex) {
        cells_[cellIndex].clearCosts();
    }
}

void TransportCostMap::clearCostsForTile(TransportLayerId layer, int tileIndex) {
    if (tileIndex < 0 || tileIndex >= static_cast<int>(totalTileCount_)) {
        return;
    }

    std::size_t modeIndex = 0;
    for (; modeIndex < static_cast<std::size_t>(TransportMode::Count); ++modeIndex) {
        cellForMutation(layer, static_cast<TransportMode>(modeIndex), tileIndex).clearCosts();
    }
}

void TransportCostMap::clearLoads() {
    std::size_t commuteTimeIndexValue = 0;
    for (; commuteTimeIndexValue < trafficLoadStates_.size(); ++commuteTimeIndexValue) {
        trafficLoadStates_[commuteTimeIndexValue].clearLoads();
    }
}

int TransportCostMap::width() const {
    return width_;
}

int TransportCostMap::height() const {
    return height_;
}

std::size_t TransportCostMap::totalTileCount() const {
    return totalTileCount_;
}

std::size_t TransportCostMap::totalNodeCount() const {
    return totalNodeCount_;
}

std::uint32_t TransportCostMap::nodeId(TransportLayerId layer, TransportMode mode, int tileIndex) const {
    const std::size_t layerOffset = static_cast<std::size_t>(layer) * static_cast<std::size_t>(TransportMode::Count) * totalTileCount_;
    const std::size_t modeOffset = static_cast<std::size_t>(mode) * totalTileCount_;
    return static_cast<std::uint32_t>(layerOffset + modeOffset + static_cast<std::size_t>(tileIndex));
}

std::uint32_t TransportCostMap::nodeId(TransportLayerId layer, TransportMode mode, int tileX, int tileY) const {
    return nodeId(layer, mode, (tileY * width_) + tileX);
}

int TransportCostMap::nodeTileIndex(std::uint32_t nodeIdValue) const {
    return static_cast<int>(nodeIdValue % static_cast<std::uint32_t>(totalTileCount_));
}

TransportMode TransportCostMap::nodeMode(std::uint32_t nodeIdValue) const {
    const std::uint32_t modeIndex = (nodeIdValue / static_cast<std::uint32_t>(totalTileCount_)) % static_cast<std::uint32_t>(TransportMode::Count);
    return static_cast<TransportMode>(modeIndex);
}

TransportLayerId TransportCostMap::nodeLayer(std::uint32_t nodeIdValue) const {
    const std::uint32_t layerIndex = nodeIdValue / static_cast<std::uint32_t>(totalTileCount_ * static_cast<std::size_t>(TransportMode::Count));
    return static_cast<TransportLayerId>(layerIndex);
}

const TransportCostCell& TransportCostMap::cell(TransportLayerId layer, TransportMode mode, int tileIndex) const {
    return cells_[nodeId(layer, mode, tileIndex)];
}

TransportCostCell& TransportCostMap::cellForMutation(TransportLayerId layer, TransportMode mode, int tileIndex) {
    return cells_[nodeId(layer, mode, tileIndex)];
}

const TransportTrafficLoadState& TransportCostMap::trafficLoadState(CommuteTimeOfDay commuteTimeOfDay) const {
    return trafficLoadStates_[commuteTimeIndex(commuteTimeOfDay)];
}

TransportTrafficLoadState& TransportCostMap::trafficLoadStateForMutation(CommuteTimeOfDay commuteTimeOfDay) {
    return trafficLoadStates_[commuteTimeIndex(commuteTimeOfDay)];
}

void TransportCostMap::addDirectionalCost(TransportLayerId layer, TransportMode mode, int tileIndex, std::uint8_t roadDirection, std::uint16_t cost, std::uint16_t capacity) {
    const int directionIndex = RoadDirectionIndex(roadDirection);
    if (tileIndex < 0 || tileIndex >= static_cast<int>(totalTileCount_) || directionIndex < 0 || cost == kTransportNoCost) {
        return;
    }

    TransportCostCell& transportCell = cellForMutation(layer, mode, tileIndex);
    if (transportCell.costs[directionIndex] == kTransportNoCost || cost < transportCell.costs[directionIndex]) {
        transportCell.costs[directionIndex] = cost;
    }
    transportCell.capacities[directionIndex] = SaturatingAdd(transportCell.capacities[directionIndex], capacity);
}

void TransportCostMap::addBuildingAccess(TransportLayerId layer, TransportMode mode, int tileIndex, std::uint8_t buildingAccessMask) {
    if (tileIndex < 0 || tileIndex >= static_cast<int>(totalTileCount_)) {
        return;
    }

    cellForMutation(layer, mode, tileIndex).buildingAccessMask |= buildingAccessMask;
}

std::uint32_t TransportCostMap::addTransferEdge(std::uint32_t fromNodeId, std::uint32_t toNodeId, std::uint16_t cost, std::uint16_t capacity) {
    if (fromNodeId >= totalNodeCount_ || toNodeId >= totalNodeCount_ || cost == kTransportNoCost) {
        return std::numeric_limits<std::uint32_t>::max();
    }

    TransportTransferEdge transferEdge;
    transferEdge.fromNodeId = fromNodeId;
    transferEdge.toNodeId = toNodeId;
    transferEdge.cost = cost;
    transferEdge.capacity = capacity;
    transferEdges_.push_back(transferEdge);
    transferOffsetsDirty_ = true;
    return static_cast<std::uint32_t>(transferEdges_.size() - 1u);
}

void TransportCostMap::clearTransferEdges() {
    transferEdges_.clear();
    transferOffsets_.assign(totalNodeCount_ + 1u, 0u);
    std::size_t commuteTimeIndexValue = 0;
    for (; commuteTimeIndexValue < trafficLoadStates_.size(); ++commuteTimeIndexValue) {
        trafficLoadStates_[commuteTimeIndexValue].transferLoads.clear();
        trafficLoadStates_[commuteTimeIndexValue].touchedTransferLoads.clear();
        trafficLoadStates_[commuteTimeIndexValue].touchedTransferFlags.clear();
    }
    transferOffsetsDirty_ = false;
}

void TransportCostMap::finalizeTransferEdges() {
    std::sort(transferEdges_.begin(), transferEdges_.end(), [](const TransportTransferEdge& left, const TransportTransferEdge& right) {
        if (left.fromNodeId != right.fromNodeId) {
            return left.fromNodeId < right.fromNodeId;
        }
        return left.toNodeId < right.toNodeId;
    });

    transferOffsets_.assign(totalNodeCount_ + 1u, 0u);
    std::size_t transferIndex = 0;
    for (; transferIndex < transferEdges_.size(); ++transferIndex) {
        ++transferOffsets_[static_cast<std::size_t>(transferEdges_[transferIndex].fromNodeId) + 1u];
    }

    std::size_t offsetIndex = 1;
    for (; offsetIndex < transferOffsets_.size(); ++offsetIndex) {
        transferOffsets_[offsetIndex] += transferOffsets_[offsetIndex - 1u];
    }

    transferOffsetsDirty_ = false;

    std::size_t commuteTimeIndexValue = 0;
    for (; commuteTimeIndexValue < trafficLoadStates_.size(); ++commuteTimeIndexValue) {
        TransportTrafficLoadState& loadState = trafficLoadStates_[commuteTimeIndexValue];
        loadState.transferLoads.assign(transferEdges_.size(), TransportTrafficTransferLoad());
        loadState.touchedTransferLoads.clear();
        loadState.touchedTransferFlags.assign(transferEdges_.size(), false);
    }
}

void TransportCostMap::collectBuildingAccessNodes(int footprintX, int footprintY, int footprintWidth, int footprintHeight, std::uint8_t allowedModeMask, std::vector<std::uint32_t>& nodeIds) const {
    nodeIds.clear();
    if (footprintWidth <= 0 || footprintHeight <= 0) {
        return;
    }

    const std::uint8_t cardinalDirections[] = {
        kRoadDirectionNorth,
        kRoadDirectionEast,
        kRoadDirectionSouth,
        kRoadDirectionWest
    };

    int localY = 0;
    for (; localY < footprintHeight; ++localY) {
        int localX = 0;
        for (; localX < footprintWidth; ++localX) {
            const int buildingTileX = footprintX + localX;
            const int buildingTileY = footprintY + localY;
            std::size_t directionIndex = 0;
            for (; directionIndex < sizeof(cardinalDirections) / sizeof(cardinalDirections[0]); ++directionIndex) {
                const std::uint8_t fromBuildingDirection = cardinalDirections[directionIndex];
                const int accessTileX = buildingTileX + RoadDirectionDeltaX(fromBuildingDirection);
                const int accessTileY = buildingTileY + RoadDirectionDeltaY(fromBuildingDirection);
                if (!isTileInsideMap(accessTileX, accessTileY)) {
                    continue;
                }
                if (accessTileX >= footprintX && accessTileX < footprintX + footprintWidth &&
                    accessTileY >= footprintY && accessTileY < footprintY + footprintHeight) {
                    continue;
                }

                const std::uint8_t accessDirectionTowardBuilding = OppositeRoadDirection(fromBuildingDirection);
                const int accessTileIndex = (accessTileY * width_) + accessTileX;
                std::size_t layerIndex = 0;
                for (; layerIndex < static_cast<std::size_t>(TransportLayerId::Count); ++layerIndex) {
                    std::size_t modeIndex = 0;
                    for (; modeIndex < static_cast<std::size_t>(TransportMode::Count); ++modeIndex) {
                        const TransportMode mode = static_cast<TransportMode>(modeIndex);
                        if ((allowedModeMask & TransportModeMaskFor(mode)) == 0u) {
                            continue;
                        }

                        const TransportCostCell& accessCell = cell(static_cast<TransportLayerId>(layerIndex), mode, accessTileIndex);
                        if ((accessCell.buildingAccessMask & accessDirectionTowardBuilding) != 0u) {
                            nodeIds.push_back(nodeId(static_cast<TransportLayerId>(layerIndex), mode, accessTileIndex));
                        }
                    }
                }
            }
        }
    }

    std::sort(nodeIds.begin(), nodeIds.end());
    nodeIds.erase(std::unique(nodeIds.begin(), nodeIds.end()), nodeIds.end());
}

void TransportCostMap::setCongestionCurve(const TransportCongestionCurve& congestionCurve) {
    congestionCurve_ = congestionCurve;
    std::sort(congestionCurve_.points.begin(), congestionCurve_.points.end(), [](const TransportCongestionPoint& left, const TransportCongestionPoint& right) {
        return left.utilization < right.utilization;
    });
}

void TransportCostMap::beginNextLoadFromOldLoad(CommuteTimeOfDay commuteTimeOfDay) {
    trafficLoadStateForMutation(commuteTimeOfDay).resetTouchedLoads(false);
}

void TransportCostMap::beginNextLoadFromZero(CommuteTimeOfDay commuteTimeOfDay) {
    trafficLoadStateForMutation(commuteTimeOfDay).resetTouchedLoads(true);
}

void TransportCostMap::commitNextLoad(CommuteTimeOfDay commuteTimeOfDay, std::vector<int>* touchedTileIndices) {
    TransportTrafficLoadState& loadState = trafficLoadStateForMutation(commuteTimeOfDay);
    if (touchedTileIndices != 0) {
        touchedTileIndices->clear();
    }

    if (loadState.nextLoadStartsFromZero) {
        std::size_t cellIndex = 0;
        for (; cellIndex < loadState.cells.size(); ++cellIndex) {
            std::size_t directionIndex = 0;
            for (; directionIndex < kRoadDirectionCount; ++directionIndex) {
                loadState.cells[cellIndex].oldLoads[directionIndex] = 0u;
            }
        }

        std::size_t transferIndex = 0;
        for (; transferIndex < loadState.transferLoads.size(); ++transferIndex) {
            loadState.transferLoads[transferIndex].oldLoad = 0u;
        }
    }

    std::size_t touchIndex = 0;
    for (; touchIndex < loadState.touchedMovementLoads.size(); ++touchIndex) {
        const TransportTouchedMovementLoad& touchedLoad = loadState.touchedMovementLoads[touchIndex];
        if (touchedLoad.nodeId >= loadState.cells.size() || touchedLoad.directionIndex >= kRoadDirectionCount) {
            continue;
        }

        loadState.cells[touchedLoad.nodeId].oldLoads[touchedLoad.directionIndex] = loadState.cells[touchedLoad.nodeId].newLoads[touchedLoad.directionIndex];
        if (touchedTileIndices != 0) {
            touchedTileIndices->push_back(nodeTileIndex(touchedLoad.nodeId));
        }
    }

    for (touchIndex = 0; touchIndex < loadState.touchedTransferLoads.size(); ++touchIndex) {
        const std::uint32_t transferIndex = loadState.touchedTransferLoads[touchIndex];
        if (transferIndex >= loadState.transferLoads.size()) {
            continue;
        }

        loadState.transferLoads[transferIndex].oldLoad = loadState.transferLoads[transferIndex].newLoad;
        if (touchedTileIndices != 0 && transferIndex < transferEdges_.size()) {
            touchedTileIndices->push_back(nodeTileIndex(transferEdges_[transferIndex].fromNodeId));
            touchedTileIndices->push_back(nodeTileIndex(transferEdges_[transferIndex].toNodeId));
        }
    }

    if (touchedTileIndices != 0) {
        if (loadState.nextLoadStartsFromZero) {
            int tileIndexValue = 0;
            for (; tileIndexValue < static_cast<int>(totalTileCount_); ++tileIndexValue) {
                touchedTileIndices->push_back(tileIndexValue);
            }
        }

        std::sort(touchedTileIndices->begin(), touchedTileIndices->end());
        touchedTileIndices->erase(std::unique(touchedTileIndices->begin(), touchedTileIndices->end()), touchedTileIndices->end());
    }

    loadState.resetTouchedLoads(false);
}

void TransportCostMap::applyPathLoad(CommuteTimeOfDay commuteTimeOfDay, const TransportPathResult& pathResult, std::uint16_t demand, bool addLoad) {
    if (!pathResult.success || demand == 0u) {
        return;
    }

    TransportTrafficLoadState& loadState = trafficLoadStateForMutation(commuteTimeOfDay);
    std::size_t stepIndex = 0;
    for (; stepIndex < pathResult.steps.size(); ++stepIndex) {
        const TransportPathStep& step = pathResult.steps[stepIndex];
        if (step.kind == TransportPathStepKind::Movement) {
            const int directionIndex = RoadDirectionIndex(step.roadDirection);
            if (step.fromNodeId >= loadState.cells.size() || directionIndex < 0) {
                continue;
            }

            touchMovementLoad(loadState, step.fromNodeId, directionIndex);
            std::uint16_t& load = loadState.cells[step.fromNodeId].newLoads[directionIndex];
            load = addLoad ? SaturatingAdd(load, demand) : SaturatingSubtract(load, demand);
        } else if (step.transferEdgeIndex < loadState.transferLoads.size()) {
            touchTransferLoad(loadState, step.transferEdgeIndex);
            std::uint16_t& load = loadState.transferLoads[step.transferEdgeIndex].newLoad;
            load = addLoad ? SaturatingAdd(load, demand) : SaturatingSubtract(load, demand);
        }
    }
}

bool TransportCostMap::findPath(const TransportPathRequest& request, TransportPathScratch& scratch, TransportPathResult& result) const {
    result = TransportPathResult();
    if (request.startNodeIds.empty() || request.goalNodeIds.empty() || transferOffsetsDirty_) {
        return false;
    }

    const TransportTrafficLoadState& loadState = trafficLoadState(request.commuteTimeOfDay);
    scratch.reset(totalNodeCount_);
    const std::uint32_t stamp = scratch.currentStamp;

    std::size_t goalIndex = 0;
    for (; goalIndex < request.goalNodeIds.size(); ++goalIndex) {
        if (request.goalNodeIds[goalIndex] < totalNodeCount_) {
            scratch.goalStamps[request.goalNodeIds[goalIndex]] = stamp;
        }
    }

    std::size_t startIndex = 0;
    for (; startIndex < request.startNodeIds.size(); ++startIndex) {
        const std::uint32_t startNodeId = request.startNodeIds[startIndex];
        if (startNodeId >= totalNodeCount_) {
            continue;
        }

        const float startCost = TransportModeStartCost(nodeMode(startNodeId));
        if (startCost > request.maximumCost) {
            continue;
        }

        scratch.stamps[startNodeId] = stamp;
        scratch.costs[startNodeId] = startCost;
        scratch.parentNodes[startNodeId] = std::numeric_limits<std::uint32_t>::max();
        TransportPathScratch::HeapEntry heapEntry;
        heapEntry.nodeId = startNodeId;
        heapEntry.costSoFar = startCost;
        heapEntry.priority = startCost;
        scratch.heap.push_back(heapEntry);
    }

    std::make_heap(scratch.heap.begin(), scratch.heap.end(), HeapCompare());

    while (!scratch.heap.empty()) {
        std::pop_heap(scratch.heap.begin(), scratch.heap.end(), HeapCompare());
        const TransportPathScratch::HeapEntry currentEntry = scratch.heap.back();
        scratch.heap.pop_back();

        const std::uint32_t currentNodeId = currentEntry.nodeId;
        if (scratch.closedStamps[currentNodeId] == stamp) {
            continue;
        }
        if (scratch.stamps[currentNodeId] != stamp || currentEntry.costSoFar > scratch.costs[currentNodeId] + 0.0001f) {
            continue;
        }

        scratch.closedStamps[currentNodeId] = stamp;
        if (scratch.goalStamps[currentNodeId] == stamp) {
            result.success = true;
            result.reachedNodeId = currentNodeId;
            result.totalCost = scratch.costs[currentNodeId];
            reconstructPath(currentNodeId, scratch, result);
            return true;
        }

        const TransportCostCell& currentCell = cells_[currentNodeId];
        const TransportLayerId currentLayer = nodeLayer(currentNodeId);
        const TransportMode currentMode = nodeMode(currentNodeId);
        const int currentTileIndex = nodeTileIndex(currentNodeId);

        std::size_t directionIndex = 0;
        for (; directionIndex < kRoadDirectionCount; ++directionIndex) {
            if (currentCell.costs[directionIndex] == kTransportNoCost) {
                continue;
            }

            int neighborTileIndex = 0;
            const std::uint8_t roadDirection = RoadDirectionFromIndex(static_cast<int>(directionIndex));
            if (!tryNeighborTile(currentTileIndex, roadDirection, neighborTileIndex)) {
                continue;
            }

            const std::uint32_t neighborNodeId = nodeId(currentLayer, currentMode, neighborTileIndex);
            const float edgeCost = request.useCongestion
                ? movementCostWithCongestion(currentCell, loadState, static_cast<int>(directionIndex), request.routeSeed, currentNodeId)
                : static_cast<float>(currentCell.costs[directionIndex]) + routeJitter(request.routeSeed, currentNodeId, roadDirection);
            const float candidateCost = scratch.costs[currentNodeId] + edgeCost;
            if (candidateCost > request.maximumCost) {
                continue;
            }

            if (scratch.stamps[neighborNodeId] != stamp || candidateCost < scratch.costs[neighborNodeId]) {
                scratch.stamps[neighborNodeId] = stamp;
                scratch.costs[neighborNodeId] = candidateCost;
                scratch.parentNodes[neighborNodeId] = currentNodeId;
                scratch.parentDirections[neighborNodeId] = roadDirection;
                scratch.parentKinds[neighborNodeId] = static_cast<std::uint8_t>(TransportPathStepKind::Movement);
                scratch.parentTransferEdges[neighborNodeId] = 0u;

                TransportPathScratch::HeapEntry heapEntry;
                heapEntry.nodeId = neighborNodeId;
                heapEntry.costSoFar = candidateCost;
                heapEntry.priority = candidateCost;
                scratch.heap.push_back(heapEntry);
                std::push_heap(scratch.heap.begin(), scratch.heap.end(), HeapCompare());
            }
        }

        const std::uint32_t transferStart = transferOffsets_[currentNodeId];
        const std::uint32_t transferEnd = transferOffsets_[currentNodeId + 1u];
        std::uint32_t transferIndex = transferStart;
        for (; transferIndex < transferEnd; ++transferIndex) {
            const TransportTransferEdge& transferEdge = transferEdges_[transferIndex];
            const TransportTrafficTransferLoad emptyTransferLoad;
            const TransportTrafficTransferLoad& transferLoad = transferIndex < loadState.transferLoads.size() ? loadState.transferLoads[transferIndex] : emptyTransferLoad;
            const float edgeCost = request.useCongestion
                ? transferCostWithCongestion(transferEdge, transferLoad, request.routeSeed, currentNodeId)
                : static_cast<float>(transferEdge.cost) + routeJitter(request.routeSeed, currentNodeId, transferIndex + 257u);
            const float candidateCost = scratch.costs[currentNodeId] + edgeCost;
            const std::uint32_t neighborNodeId = transferEdge.toNodeId;
            if (candidateCost > request.maximumCost) {
                continue;
            }

            if (scratch.stamps[neighborNodeId] != stamp || candidateCost < scratch.costs[neighborNodeId]) {
                scratch.stamps[neighborNodeId] = stamp;
                scratch.costs[neighborNodeId] = candidateCost;
                scratch.parentNodes[neighborNodeId] = currentNodeId;
                scratch.parentDirections[neighborNodeId] = 0u;
                scratch.parentKinds[neighborNodeId] = static_cast<std::uint8_t>(TransportPathStepKind::Transfer);
                scratch.parentTransferEdges[neighborNodeId] = transferIndex;

                TransportPathScratch::HeapEntry heapEntry;
                heapEntry.nodeId = neighborNodeId;
                heapEntry.costSoFar = candidateCost;
                heapEntry.priority = candidateCost;
                scratch.heap.push_back(heapEntry);
                std::push_heap(scratch.heap.begin(), scratch.heap.end(), HeapCompare());
            }
        }
    }

    return false;
}

void TransportCostMap::buildTrafficOverlay(std::vector<std::uint8_t>& overlayPixels) const {
    overlayPixels.assign(totalTileCount_ * 4u, 0u);

    int tileIndex = 0;
    for (; tileIndex < static_cast<int>(totalTileCount_); ++tileIndex) {
        WriteTrafficOverlayPixel(*this, tileIndex, overlayPixels);
    }
}

void TransportCostMap::buildTrafficOverlayForTiles(const std::vector<int>& tileIndices, std::vector<std::uint8_t>& overlayPixels) const {
    if (overlayPixels.size() != totalTileCount_ * 4u) {
        overlayPixels.assign(totalTileCount_ * 4u, 0u);
    }

    std::vector<int> validTileIndices;
    validTileIndices.reserve(tileIndices.size());
    std::size_t dirtyIndex = 0;
    for (; dirtyIndex < tileIndices.size(); ++dirtyIndex) {
        const int dirtyTileIndex = tileIndices[dirtyIndex];
        if (dirtyTileIndex >= 0 && dirtyTileIndex < static_cast<int>(totalTileCount_)) {
            validTileIndices.push_back(dirtyTileIndex);
        }
    }

    std::sort(validTileIndices.begin(), validTileIndices.end());
    validTileIndices.erase(std::unique(validTileIndices.begin(), validTileIndices.end()), validTileIndices.end());

    for (dirtyIndex = 0; dirtyIndex < validTileIndices.size(); ++dirtyIndex) {
        WriteTrafficOverlayPixel(*this, validTileIndices[dirtyIndex], overlayPixels);
    }
}

bool TransportCostMap::isTileInsideMap(int tileX, int tileY) const {
    return tileX >= 0 && tileX < width_ && tileY >= 0 && tileY < height_;
}

bool TransportCostMap::tryNeighborTile(int tileIndex, std::uint8_t roadDirection, int& neighborTileIndex) const {
    const int tileY = tileIndex / width_;
    const int tileX = tileIndex - (tileY * width_);
    const int neighborX = tileX + RoadDirectionDeltaX(roadDirection);
    const int neighborY = tileY + RoadDirectionDeltaY(roadDirection);
    if (!isTileInsideMap(neighborX, neighborY)) {
        return false;
    }

    neighborTileIndex = (neighborY * width_) + neighborX;
    return true;
}

float TransportCostMap::movementCostWithCongestion(const TransportCostCell& cell, const TransportTrafficLoadState& loadState, int directionIndex, std::uint32_t routeSeed, std::uint32_t nodeIdValue) const {
    const float baseCost = static_cast<float>(cell.costs[directionIndex]);
    const std::uint16_t load = nodeIdValue < loadState.cells.size() ? loadState.cells[nodeIdValue].oldLoads[directionIndex] : 0u;
    return (baseCost / CongestionSpeedMultiplier(congestionCurve_, load, cell.capacities[directionIndex])) +
        routeJitter(routeSeed, nodeIdValue, RoadDirectionFromIndex(directionIndex));
}

float TransportCostMap::transferCostWithCongestion(const TransportTransferEdge& transferEdge, const TransportTrafficTransferLoad& transferLoad, std::uint32_t routeSeed, std::uint32_t nodeIdValue) const {
    const float baseCost = static_cast<float>(transferEdge.cost);
    return (baseCost / CongestionSpeedMultiplier(congestionCurve_, transferLoad.oldLoad, transferEdge.capacity)) +
        routeJitter(routeSeed, nodeIdValue, transferEdge.toNodeId);
}

float TransportCostMap::routeJitter(std::uint32_t routeSeed, std::uint32_t nodeIdValue, std::uint32_t edgeSalt) const {
    std::uint32_t value = routeSeed ^ (nodeIdValue * 0x9E3779B9u) ^ (edgeSalt * 0x85EBCA6Bu);
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return static_cast<float>(value & 1023u) * kRouteJitterScale;
}

void TransportCostMap::reconstructPath(std::uint32_t reachedNodeId, const TransportPathScratch& scratch, TransportPathResult& result) const {
    result.steps.clear();
    std::uint32_t currentNodeId = reachedNodeId;
    while (currentNodeId < scratch.parentNodes.size() &&
        scratch.parentNodes[currentNodeId] != std::numeric_limits<std::uint32_t>::max()) {
        TransportPathStep step;
        step.fromNodeId = scratch.parentNodes[currentNodeId];
        step.toNodeId = currentNodeId;
        step.roadDirection = scratch.parentDirections[currentNodeId];
        step.kind = static_cast<TransportPathStepKind>(scratch.parentKinds[currentNodeId]);
        step.transferEdgeIndex = scratch.parentTransferEdges[currentNodeId];
        result.steps.push_back(step);
        currentNodeId = step.fromNodeId;
    }

    std::reverse(result.steps.begin(), result.steps.end());
}

std::size_t TransportCostMap::commuteTimeIndex(CommuteTimeOfDay commuteTimeOfDay) const {
    const std::size_t index = static_cast<std::size_t>(commuteTimeOfDay);
    return index < trafficLoadStates_.size() ? index : 0u;
}

void TransportCostMap::touchMovementLoad(TransportTrafficLoadState& loadState, std::uint32_t nodeIdValue, int directionIndex) {
    if (nodeIdValue >= loadState.cells.size() || directionIndex < 0 || directionIndex >= static_cast<int>(kRoadDirectionCount)) {
        return;
    }

    const std::size_t flagIndex = static_cast<std::size_t>(nodeIdValue) * kRoadDirectionCount + static_cast<std::size_t>(directionIndex);
    if (flagIndex >= loadState.touchedMovementFlags.size() || loadState.touchedMovementFlags[flagIndex]) {
        return;
    }

    loadState.touchedMovementFlags[flagIndex] = true;
    loadState.touchedMovementLoads.push_back(TransportTouchedMovementLoad(nodeIdValue, static_cast<std::uint8_t>(directionIndex)));
    loadState.cells[nodeIdValue].newLoads[directionIndex] = loadState.nextLoadStartsFromZero ? 0u : loadState.cells[nodeIdValue].oldLoads[directionIndex];
}

void TransportCostMap::touchTransferLoad(TransportTrafficLoadState& loadState, std::uint32_t transferEdgeIndex) {
    if (transferEdgeIndex >= loadState.transferLoads.size()) {
        return;
    }

    if (transferEdgeIndex >= loadState.touchedTransferFlags.size() || loadState.touchedTransferFlags[transferEdgeIndex]) {
        return;
    }

    loadState.touchedTransferFlags[transferEdgeIndex] = true;
    loadState.touchedTransferLoads.push_back(transferEdgeIndex);
    loadState.transferLoads[transferEdgeIndex].newLoad = loadState.nextLoadStartsFromZero ? 0u : loadState.transferLoads[transferEdgeIndex].oldLoad;
}
