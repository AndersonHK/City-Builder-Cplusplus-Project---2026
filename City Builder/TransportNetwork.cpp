#include "TransportNetwork.h"

#include <algorithm>
#include <cassert>
#include <cstddef>

namespace {
struct PendingTileUpdate {
    std::size_t slot;
    TransportTile tile;
    bool changed;

    PendingTileUpdate()
        : slot(0),
          changed(false) {
    }
};

int DeltaXForDirection(std::uint8_t roadDirection) {
    if (roadDirection == kRoadDirectionEast) {
        return 1;
    }
    if (roadDirection == kRoadDirectionWest) {
        return -1;
    }
    return 0;
}

int DeltaYForDirection(std::uint8_t roadDirection) {
    if (roadDirection == kRoadDirectionSouth) {
        return 1;
    }
    if (roadDirection == kRoadDirectionNorth) {
        return -1;
    }
    return 0;
}

std::uint8_t OppositeCardinal(std::uint8_t roadDirection) {
    return OppositeRoadDirection(roadDirection);
}

const std::uint8_t kCardinalDirections[] = {
    kRoadDirectionNorth,
    kRoadDirectionEast,
    kRoadDirectionSouth,
    kRoadDirectionWest
};
const int kRoadImmediateDirtyRadius = 1;

int CountCardinalDirections(std::uint8_t directionMask) {
    int directionCount = 0;
    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        if ((directionMask & kCardinalDirections[directionIndex]) == 0) {
            continue;
        }

        ++directionCount;
    }
    return directionCount;
}

std::uint8_t LaneIntentFromCardinalRoadMask(std::uint8_t directionMask) {
    std::uint8_t laneIntentMask = 0;
    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        if ((directionMask & direction) != 0) {
            laneIntentMask |= LaneIntentFromRoadDirection(direction);
        }
    }

    return laneIntentMask;
}

std::uint8_t RoadDirectionMaskForLane(const RoadLanePlacement& lanePlacement) {
    std::uint8_t directionMask = 0;
    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        if (lanePlacement.hasTravelDirection(direction)) {
            directionMask |= direction;
        }
    }

    return directionMask;
}

std::uint16_t TraversalCostForLaneType(RoadLaneTypeId laneType, RoadFamily family, int laneIndex) {
    RoadTemplateElement costElement;
    costElement.laneType = laneType;
    RoadLane costLane(costElement, laneIndex);
    return costLane.traversalCost(family);
}

std::uint16_t TraversalCostForLane(const RoadLanePlacement& lanePlacement) {
    return TraversalCostForLaneType(lanePlacement.laneType, lanePlacement.family, lanePlacement.laneIndex);
}

RoadDirectionMode DirectionModeFromTemplateId(std::uint16_t templateId) {
    return static_cast<RoadDirectionMode>((templateId >> 6) & 0x3u);
}

RoadTemplateKind TemplateKindFromTemplateId(std::uint16_t templateId) {
    return static_cast<RoadTemplateKind>((templateId >> 14) & 0x3u);
}

std::uint8_t LeftDirectionForTravelDirection(std::uint8_t roadDirection) {
    if (roadDirection == kRoadDirectionNorth) {
        return kRoadDirectionWest;
    }
    if (roadDirection == kRoadDirectionEast) {
        return kRoadDirectionNorth;
    }
    if (roadDirection == kRoadDirectionSouth) {
        return kRoadDirectionEast;
    }
    if (roadDirection == kRoadDirectionWest) {
        return kRoadDirectionSouth;
    }
    return 0;
}

bool CarLaneAllowsCapReturn(const RoadLanePlacement& lanePlacement) {
    return !lanePlacement.isCar() || DirectionModeFromTemplateId(lanePlacement.templateId) == RoadDirectionMode::TwoWay;
}
}

TransportNetwork::TransportNetwork()
    : width_(0),
      height_(0),
      totalTileCount_(0),
      chunkWidth_(1),
      chunkHeight_(1),
      chunksPerRow_(1),
      revision_(0),
      trafficOverlayRevision_(0) {
}

void TransportNetwork::initialize(int width, int height, const std::vector<ChunkRect>& chunkLayout) {
    width_ = width;
    height_ = height;
    totalTileCount_ = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    chunkLayout_ = chunkLayout;
    chunkWidth_ = chunkLayout_.empty() ? std::max(1, width_) : std::max(1, chunkLayout_.front().width);
    chunkHeight_ = chunkLayout_.empty() ? std::max(1, height_) : std::max(1, chunkLayout_.front().height);
    chunksPerRow_ = std::max(1, width_ / chunkWidth_);

    transportTiles_.assign(totalTileCount_ * layerCount(), TransportTile());
    resolvedCells_.assign(totalTileCount_ * layerCount(), ResolvedRoadCell());
    costMap_.initialize(width_, height_);
    groundRoadRenderState_.assign(totalTileCount_ * kGroundRoadRenderChannelsPerTile, 0);
    trafficOverlayState_.assign(totalTileCount_ * 4u, 0);
    groundChunkRevisions_.assign(chunkLayout_.size(), 1);
    elevatedChunkRevisions_.assign(chunkLayout_.size(), 1);
    trafficOverlayChunkRevisions_.assign(chunkLayout_.size(), 1);
    revision_ = 0;
    trafficOverlayRevision_ = 0;
}

void TransportNetwork::clear() {
    transportTiles_.assign(totalTileCount_ * layerCount(), TransportTile());
    resolvedCells_.assign(totalTileCount_ * layerCount(), ResolvedRoadCell());
    costMap_.initialize(width_, height_);
    groundRoadRenderState_.assign(totalTileCount_ * kGroundRoadRenderChannelsPerTile, 0);
    trafficOverlayState_.assign(totalTileCount_ * 4u, 0);
    groundChunkRevisions_.assign(chunkLayout_.size(), 1);
    elevatedChunkRevisions_.assign(chunkLayout_.size(), 1);
    trafficOverlayChunkRevisions_.assign(chunkLayout_.size(), 1);
    revision_ = 0;
    trafficOverlayRevision_ = 0;
}

bool TransportNetwork::placeRoadStroke(const RoadStrokeCommand& roadStrokeCommand, const std::vector<int>& lotOccupancy, int invalidLotId, std::vector<int>* topologyDirtyTileIndices) {
    if (topologyDirtyTileIndices != 0) {
        topologyDirtyTileIndices->clear();
    }

    if (roadStrokeCommand.operation != RoadStrokeOperation::Place || roadStrokeCommand.family == RoadFamily::None) {
        return false;
    }

    RoadTemplate roadTemplate = roadStrokeCommand.roadTemplate;
    if (roadTemplate.elements.empty()) {
        RoadTemplateKind templateKind = roadStrokeCommand.templateKind;
        if (roadStrokeCommand.family == RoadFamily::Highway || roadStrokeCommand.layer == TransportLayerId::Elevated) {
            templateKind = RoadTemplateKind::Highway;
        }
        roadTemplate = Road::makeTemplate(templateKind, RoadTrafficSide::RightHand, RoadDirectionMode::TwoWay);
    } else {
        roadTemplate.family = roadStrokeCommand.family;
        roadTemplate.layer = roadStrokeCommand.layer;
    }

    Road road(roadTemplate);
    std::vector<RoadTilePlacement> placements;
    placements.reserve(4096);
    if (!road.appendStrokePlacements(roadStrokeCommand.startTile, roadStrokeCommand.cornerTile, roadStrokeCommand.endTile, width_, height_, placements)) {
        return false;
    }

    if (placements.empty()) {
        return false;
    }

    bool madeChange = false;
    if (!validateAndApplyPlacements(roadStrokeCommand.layer, placements, lotOccupancy, invalidLotId, madeChange)) {
        return false;
    }

    if (!madeChange) {
        return true;
    }

    std::vector<int> dirtyTileIndices;
    markDirtyNeighborhood(placements, dirtyTileIndices);
    expandDirtyRoadDependencies(roadStrokeCommand.layer, dirtyTileIndices);

    std::size_t dirtyIndex = 0;
    for (; dirtyIndex < dirtyTileIndices.size(); ++dirtyIndex) {
        const int dirtyTileIndex = dirtyTileIndices[dirtyIndex];
        const int tileY = dirtyTileIndex / width_;
        const int tileX = dirtyTileIndex - (tileY * width_);
        resolveDirtyTile(roadStrokeCommand.layer, tileX, tileY);
    }

    bumpDirtyChunkRevisions(roadStrokeCommand.layer, dirtyTileIndices);
    rebuildCostMapAndTrafficOverlayForTiles(dirtyTileIndices);
    if (topologyDirtyTileIndices != 0) {
        *topologyDirtyTileIndices = dirtyTileIndices;
    }
    ++revision_;
    return true;
}

bool TransportNetwork::canPlaceRoadStroke(const RoadStrokeCommand& roadStrokeCommand, const std::vector<int>& lotOccupancy, int invalidLotId) const {
    if (roadStrokeCommand.operation != RoadStrokeOperation::Place || roadStrokeCommand.family == RoadFamily::None) {
        return false;
    }

    RoadTemplate roadTemplate = roadStrokeCommand.roadTemplate;
    if (roadTemplate.elements.empty()) {
        RoadTemplateKind templateKind = roadStrokeCommand.templateKind;
        if (roadStrokeCommand.family == RoadFamily::Highway || roadStrokeCommand.layer == TransportLayerId::Elevated) {
            templateKind = RoadTemplateKind::Highway;
        }
        roadTemplate = Road::makeTemplate(templateKind, RoadTrafficSide::RightHand, RoadDirectionMode::TwoWay);
    } else {
        roadTemplate.family = roadStrokeCommand.family;
        roadTemplate.layer = roadStrokeCommand.layer;
    }

    Road road(roadTemplate);
    std::vector<RoadTilePlacement> placements;
    placements.reserve(4096);
    if (!road.appendStrokePlacements(roadStrokeCommand.startTile, roadStrokeCommand.cornerTile, roadStrokeCommand.endTile, width_, height_, placements)) {
        return false;
    }

    if (placements.empty()) {
        return false;
    }

    std::vector<PendingTileUpdate> pendingTiles;
    pendingTiles.reserve(placements.size());
    std::size_t placementIndex = 0;
    for (; placementIndex < placements.size(); ++placementIndex) {
        const RoadTilePlacement& placement = placements[placementIndex];
        if (roadStrokeCommand.layer == TransportLayerId::Ground && lotOccupancy[placement.tileIndex] != invalidLotId) {
            return false;
        }

        const std::size_t slot = slotIndex(roadStrokeCommand.layer, placement.tileIndex, totalTileCount_);
        std::size_t pendingIndex = 0;
        for (; pendingIndex < pendingTiles.size(); ++pendingIndex) {
            if (pendingTiles[pendingIndex].slot == slot) {
                break;
            }
        }

        if (pendingIndex == pendingTiles.size()) {
            PendingTileUpdate pendingTile;
            pendingTile.slot = slot;
            pendingTile.tile = transportTiles_[slot];
            pendingTiles.push_back(pendingTile);
        }

        if (pendingTiles[pendingIndex].tile.tryAddLane(placement.lanePlacement) == RoadTileLaneAddResult::Rejected) {
            return false;
        }
    }

    return true;
}

bool TransportNetwork::removeRoadAtTile(int tileX, int tileY, std::vector<int>* topologyDirtyTileIndices) {
    if (topologyDirtyTileIndices != 0) {
        topologyDirtyTileIndices->clear();
    }

    if (!isTileInsideMap(tileX, tileY)) {
        return false;
    }

    std::vector<int> tileIndices(1, tileIndex(tileX, tileY));
    return removeRoadsAtTiles(tileIndices, topologyDirtyTileIndices);
}

bool TransportNetwork::removeRoadsAtTiles(const std::vector<int>& tileIndices, std::vector<int>* topologyDirtyTileIndices) {
    if (topologyDirtyTileIndices != 0) {
        topologyDirtyTileIndices->clear();
    }

    if (tileIndices.empty()) {
        return false;
    }

    std::vector<int> candidateTileIndices;
    candidateTileIndices.reserve(tileIndices.size());
    std::size_t tileListIndex = 0;
    for (; tileListIndex < tileIndices.size(); ++tileListIndex) {
        const int candidateTileIndex = tileIndices[tileListIndex];
        if (candidateTileIndex >= 0 && candidateTileIndex < static_cast<int>(totalTileCount_)) {
            candidateTileIndices.push_back(candidateTileIndex);
        }
    }

    if (candidateTileIndices.empty()) {
        return false;
    }

    std::sort(candidateTileIndices.begin(), candidateTileIndices.end());
    candidateTileIndices.erase(std::unique(candidateTileIndices.begin(), candidateTileIndices.end()), candidateTileIndices.end());

    RoadAxis preferredRemovalAxis = RoadAxis::None;
    if (candidateTileIndices.size() > 1u) {
        const int firstTileY = candidateTileIndices.front() / width_;
        const int firstTileX = candidateTileIndices.front() - (firstTileY * width_);
        bool sameX = true;
        bool sameY = true;
        for (std::size_t candidateIndex = 1; candidateIndex < candidateTileIndices.size(); ++candidateIndex) {
            const int candidateTileY = candidateTileIndices[candidateIndex] / width_;
            const int candidateTileX = candidateTileIndices[candidateIndex] - (candidateTileY * width_);
            sameX = sameX && candidateTileX == firstTileX;
            sameY = sameY && candidateTileY == firstTileY;
        }
        if (sameX && !sameY) {
            preferredRemovalAxis = RoadAxis::Vertical;
        } else if (sameY && !sameX) {
            preferredRemovalAxis = RoadAxis::Horizontal;
        }
    }

    bool removedAnyRoad = false;
    std::vector<int> costMapDirtyTileIndices;
    std::size_t layerIndex = 0;
    for (; layerIndex < layerCount(); ++layerIndex) {
        const TransportLayerId layer = static_cast<TransportLayerId>(layerIndex);
        std::vector<RoadLanePlacement> removalLanes;
        for (tileListIndex = 0; tileListIndex < candidateTileIndices.size(); ++tileListIndex) {
            const int candidateTileIndex = candidateTileIndices[tileListIndex];
            const int tileY = candidateTileIndex / width_;
            const int tileX = candidateTileIndex - (tileY * width_);
            collectRoadRemovalFootprint(layer, tileX, tileY, preferredRemovalAxis, removalLanes);
        }
        if (removalLanes.empty()) {
            continue;
        }

        std::vector<int> removalTileIndices;
        removalTileIndices.reserve(removalLanes.size());
        for (std::size_t removalIndex = 0; removalIndex < removalLanes.size(); ++removalIndex) {
            removalTileIndices.push_back(removalLanes[removalIndex].tileIndex);
        }
        std::sort(removalTileIndices.begin(), removalTileIndices.end());
        removalTileIndices.erase(std::unique(removalTileIndices.begin(), removalTileIndices.end()), removalTileIndices.end());

        bool removedLayerRoad = false;
        std::vector<int> dirtyTileIndices = removalTileIndices;
        std::size_t removalIndex = 0;
        for (; removalIndex < removalLanes.size(); ++removalIndex) {
            if (removeMatchingRoadSliceLanes(layer, removalLanes[removalIndex])) {
                removedLayerRoad = true;
            }
        }

        if (!removedLayerRoad) {
            continue;
        }

        std::vector<int> dirtyNeighborhoodSeeds = dirtyTileIndices;
        markDirtyTileNeighborhood(dirtyNeighborhoodSeeds, dirtyTileIndices);
        expandDirtyRoadDependencies(layer, dirtyTileIndices);

        std::size_t dirtyIndex = 0;
        for (; dirtyIndex < dirtyTileIndices.size(); ++dirtyIndex) {
            const int dirtyTileIndex = dirtyTileIndices[dirtyIndex];
            const int dirtyTileY = dirtyTileIndex / width_;
            const int dirtyTileX = dirtyTileIndex - (dirtyTileY * width_);
            resolveDirtyTile(layer, dirtyTileX, dirtyTileY);
        }

        bumpDirtyChunkRevisions(layer, dirtyTileIndices);
        costMapDirtyTileIndices.insert(costMapDirtyTileIndices.end(), dirtyTileIndices.begin(), dirtyTileIndices.end());
        removedAnyRoad = true;
    }

    if (!removedAnyRoad) {
        return false;
    }

    std::sort(costMapDirtyTileIndices.begin(), costMapDirtyTileIndices.end());
    costMapDirtyTileIndices.erase(std::unique(costMapDirtyTileIndices.begin(), costMapDirtyTileIndices.end()), costMapDirtyTileIndices.end());
    rebuildCostMapAndTrafficOverlayForTiles(costMapDirtyTileIndices);
    if (topologyDirtyTileIndices != 0) {
        *topologyDirtyTileIndices = costMapDirtyTileIndices;
    }
    ++revision_;
    return true;
}

const std::vector<ResolvedRoadCell>& TransportNetwork::resolvedCells() const {
    return resolvedCells_;
}

const TransportCostMap& TransportNetwork::costMap() const {
    return costMap_;
}

const std::vector<std::uint8_t>& TransportNetwork::groundRoadRenderState() const {
    return groundRoadRenderState_;
}

const std::vector<std::uint8_t>& TransportNetwork::trafficOverlayState() const {
    return trafficOverlayState_;
}

const std::vector<std::uint64_t>& TransportNetwork::groundChunkRevisions() const {
    return groundChunkRevisions_;
}

const std::vector<std::uint64_t>& TransportNetwork::elevatedChunkRevisions() const {
    return elevatedChunkRevisions_;
}

const std::vector<std::uint64_t>& TransportNetwork::trafficOverlayChunkRevisions() const {
    return trafficOverlayChunkRevisions_;
}

std::uint64_t TransportNetwork::revision() const {
    return revision_;
}

std::uint64_t TransportNetwork::trafficOverlayRevision() const {
    return trafficOverlayRevision_;
}

void TransportNetwork::setCongestionCurve(const TransportCongestionCurve& congestionCurve) {
    costMap_.setCongestionCurve(congestionCurve);
}

void TransportNetwork::beginTrafficAssignmentFromOldLoad(CommuteTimeOfDay commuteTimeOfDay) {
    costMap_.beginNextLoadFromOldLoad(commuteTimeOfDay);
}

void TransportNetwork::beginTrafficAssignmentFromZero(CommuteTimeOfDay commuteTimeOfDay) {
    costMap_.beginNextLoadFromZero(commuteTimeOfDay);
}

void TransportNetwork::applyTrafficPathLoad(CommuteTimeOfDay commuteTimeOfDay, const TransportPathResult& pathResult, std::uint16_t demand, bool addLoad) {
    costMap_.applyPathLoad(commuteTimeOfDay, pathResult, demand, addLoad);
}

void TransportNetwork::commitTrafficAssignment(CommuteTimeOfDay commuteTimeOfDay) {
    std::vector<int> touchedTileIndices;
    costMap_.commitNextLoad(commuteTimeOfDay, &touchedTileIndices);
    if (touchedTileIndices.empty()) {
        return;
    }

    costMap_.buildTrafficOverlayForTiles(touchedTileIndices, trafficOverlayState_);
    bumpTrafficOverlayChunkRevisionsForTiles(touchedTileIndices);
    ++trafficOverlayRevision_;
}

bool TransportNetwork::hasOccupancy(TransportLayerId layer, int tileIndexValue) const {
    if (tileIndexValue < 0 || tileIndexValue >= static_cast<int>(totalTileCount_)) {
        return false;
    }

    const std::size_t slot = slotIndex(layer, tileIndexValue, totalTileCount_);
    return slot < transportTiles_.size() && !transportTiles_[slot].empty();
}

bool TransportNetwork::hasGroundOccupancy(int tileIndexValue) const {
    return hasOccupancy(TransportLayerId::Ground, tileIndexValue);
}

TransportNetworkSaveState TransportNetwork::exportSaveState() const {
    TransportNetworkSaveState saveState;

    std::size_t layerIndex = 0;
    for (; layerIndex < layerCount(); ++layerIndex) {
        const TransportLayerId layer = static_cast<TransportLayerId>(layerIndex);
        std::size_t tileIndexValue = 0;
        for (; tileIndexValue < totalTileCount_; ++tileIndexValue) {
            const TransportTile& tile = transportTiles_[slotIndex(layer, static_cast<int>(tileIndexValue), totalTileCount_)];
            if (tile.lanes().empty()) {
                continue;
            }

            TransportTileSaveState tileSaveState;
            tileSaveState.layer = layer;
            tileSaveState.tileIndex = static_cast<int>(tileIndexValue);
            tileSaveState.lanes = tile.lanes();
            saveState.tiles.push_back(tileSaveState);
        }
    }

    return saveState;
}

void TransportNetwork::importSaveState(const TransportNetworkSaveState& saveState) {
    clear();

    std::size_t savedTileIndex = 0;
    for (; savedTileIndex < saveState.tiles.size(); ++savedTileIndex) {
        const TransportTileSaveState& tileSaveState = saveState.tiles[savedTileIndex];
        if (tileSaveState.tileIndex < 0 || tileSaveState.tileIndex >= static_cast<int>(totalTileCount_)) {
            continue;
        }

        const std::size_t slot = slotIndex(tileSaveState.layer, tileSaveState.tileIndex, totalTileCount_);
        if (slot >= transportTiles_.size()) {
            continue;
        }

        transportTiles_[slot].lanesForMutation() = tileSaveState.lanes;
        std::size_t laneIndex = 0;
        for (; laneIndex < transportTiles_[slot].lanesForMutation().size(); ++laneIndex) {
            RoadLanePlacement& lane = transportTiles_[slot].lanesForMutation()[laneIndex];
            lane.layer = tileSaveState.layer;
            lane.tileIndex = tileSaveState.tileIndex;
            lane.tileY = tileSaveState.tileIndex / width_;
            lane.tileX = tileSaveState.tileIndex - (lane.tileY * width_);
        }
    }

    std::size_t layerIndex = 0;
    for (; layerIndex < layerCount(); ++layerIndex) {
        const TransportLayerId layer = static_cast<TransportLayerId>(layerIndex);
        std::vector<int> dirtyTileIndices;
        dirtyTileIndices.reserve(totalTileCount_);
        int dirtyTileIndex = 0;
        for (; dirtyTileIndex < static_cast<int>(totalTileCount_); ++dirtyTileIndex) {
            dirtyTileIndices.push_back(dirtyTileIndex);
        }

        int tileY = 0;
        for (; tileY < height_; ++tileY) {
            int tileX = 0;
            for (; tileX < width_; ++tileX) {
                resolveDirtyTile(layer, tileX, tileY);
            }
        }
    }

    std::size_t chunkIndex = 0;
    for (; chunkIndex < groundChunkRevisions_.size(); ++chunkIndex) {
        ++groundChunkRevisions_[chunkIndex];
    }
    for (chunkIndex = 0; chunkIndex < elevatedChunkRevisions_.size(); ++chunkIndex) {
        ++elevatedChunkRevisions_[chunkIndex];
    }

    rebuildCostMapAndTrafficOverlay();
    ++revision_;
}

int TransportNetwork::width() const {
    return width_;
}

int TransportNetwork::height() const {
    return height_;
}

std::size_t TransportNetwork::totalTileCount() const {
    return totalTileCount_;
}

std::size_t TransportNetwork::layerCount() {
    return static_cast<std::size_t>(TransportLayerId::Count);
}

std::size_t TransportNetwork::slotIndex(TransportLayerId layer, int tileIndexValue, std::size_t totalTileCountValue) {
    return static_cast<std::size_t>(layer) * totalTileCountValue + static_cast<std::size_t>(tileIndexValue);
}

RoadTemplate TransportNetwork::makeRoadTemplate(RoadFamily family, TransportLayerId layer, int laneCount, RoadTrafficSide trafficSide, RoadDirectionMode directionMode) {
    return Road::makeTemplate(family, layer, laneCount, trafficSide, directionMode);
}

RoadTemplate TransportNetwork::makeRoadTemplate(RoadTemplateKind templateKind, RoadTrafficSide trafficSide, RoadDirectionMode directionMode) {
    return Road::makeTemplate(templateKind, trafficSide, directionMode);
}

bool TransportNetwork::isTileInsideMap(int tileX, int tileY) const {
    return tileX >= 0 && tileX < width_ && tileY >= 0 && tileY < height_;
}

int TransportNetwork::tileIndex(int tileX, int tileY) const {
    return (tileY * width_) + tileX;
}

int TransportNetwork::chunkIndexForTile(int tileX, int tileY) const {
    if (chunkLayout_.empty()) {
        return -1;
    }

    const int chunkX = tileX / chunkWidth_;
    const int chunkY = tileY / chunkHeight_;
    return chunkY * chunksPerRow_ + chunkX;
}

bool TransportNetwork::validateAndApplyPlacements(TransportLayerId layer, const std::vector<RoadTilePlacement>& placements, const std::vector<int>& lotOccupancy, int invalidLotId, bool& madeChange) {
    madeChange = false;
    std::vector<PendingTileUpdate> pendingTiles;
    pendingTiles.reserve(placements.size());

    std::size_t placementIndex = 0;
    for (; placementIndex < placements.size(); ++placementIndex) {
        const RoadTilePlacement& placement = placements[placementIndex];
        if (layer == TransportLayerId::Ground && lotOccupancy[placement.tileIndex] != invalidLotId) {
            return false;
        }

        const std::size_t slot = slotIndex(layer, placement.tileIndex, totalTileCount_);
        std::size_t pendingIndex = 0;
        for (; pendingIndex < pendingTiles.size(); ++pendingIndex) {
            if (pendingTiles[pendingIndex].slot == slot) {
                break;
            }
        }

        if (pendingIndex == pendingTiles.size()) {
            PendingTileUpdate pendingTile;
            pendingTile.slot = slot;
            pendingTile.tile = transportTiles_[slot];
            pendingTiles.push_back(pendingTile);
        }

        RoadTileLaneAddResult addResult = pendingTiles[pendingIndex].tile.tryAddLane(placement.lanePlacement);
        if (addResult == RoadTileLaneAddResult::Rejected) {
            return false;
        }
        if (addResult == RoadTileLaneAddResult::Added) {
            pendingTiles[pendingIndex].changed = true;
            madeChange = true;
        }
    }

    if (!madeChange) {
        return true;
    }

    std::size_t pendingIndex = 0;
    for (; pendingIndex < pendingTiles.size(); ++pendingIndex) {
        if (pendingTiles[pendingIndex].changed) {
            transportTiles_[pendingTiles[pendingIndex].slot] = pendingTiles[pendingIndex].tile;
        }
    }

    return true;
}

void TransportNetwork::resolveDirtyTile(TransportLayerId layer, int tileX, int tileY) {
    const int tileIndexValue = tileIndex(tileX, tileY);
    const std::size_t slot = slotIndex(layer, tileIndexValue, totalTileCount_);
    const TransportTile& tile = transportTiles_[slot];
    const std::size_t groundRenderOffset = static_cast<std::size_t>(tileIndexValue) * kGroundRoadRenderChannelsPerTile;

    ResolvedRoadCell resolvedCell;
    if (tile.empty()) {
        resolvedCells_[slot] = resolvedCell;
        if (layer == TransportLayerId::Ground) {
            groundRoadRenderState_[groundRenderOffset + 0u] = 0;
            groundRoadRenderState_[groundRenderOffset + 1u] = 0;
            groundRoadRenderState_[groundRenderOffset + 2u] = 0;
            groundRoadRenderState_[groundRenderOffset + 3u] = 0;
        }
        return;
    }

    const RoadFamily family = tile.family();
    std::uint8_t carMovementMask = 0;
    std::uint8_t pedestrianMovementMask = 0;
    std::uint8_t junctionMask = 0;
    RoadRenderState renderState;
    const std::vector<RoadLanePlacement>& lanes = tile.lanes();
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes[laneIndex];
        if (!lane.active || !lane.isCar()) {
            continue;
        }

        const std::uint8_t laneMovementMask = pathLaneMovementMask(layer, tileX, tileY, lane);
        const std::uint8_t laneGraphicMask = laneGraphicDirectionMask(layer, tileX, tileY, lane);
        const std::uint8_t laneCenterTravelMask = laneCenterTravelDirectionMask(layer, tileX, tileY, lane);
        RoadLaneCellContext laneCellContext = roadLaneCellContext(layer, tileX, tileY, lane);
        laneCellContext.directionMask = laneGraphicMask;
        laneCellContext.travelDirectionMask = laneCenterTravelMask;
        laneCellContext.pathDirectionMask = laneMovementMask;
        RoadLaneCell graphicCell = Road::makeLaneCell(lane, laneCellContext);
        carMovementMask |= laneMovementMask;
        pedestrianMovementMask |= graphicCell.secondary.mode == CommuterMode::Pedestrian ? graphicCell.secondary.directionMask : 0;
        junctionMask |= laneGraphicMask;
        graphicCell.applyGraphics(renderState);
        const std::uint8_t centerDividerMask = graphicCell.primary.centerMask();
        renderState.dividerMask = PackDividerMask(
            static_cast<std::uint8_t>((renderState.dividerMask & kRoadSurfaceSidewalkEdgeMask) | (lane.sameDirectionDividerMask != 0 ? centerDividerMask : 0)),
            static_cast<std::uint8_t>(((renderState.dividerMask >> kRoadDividerYellowShift) & kRoadSurfaceSidewalkEdgeMask) | (lane.opposingDirectionDividerMask != 0 ? centerDividerMask : 0)));

        resolvedCell.laneTypeMask |= LaneTypeMaskFor(graphicCell.primary.laneType);
        resolvedCell.travelMask |= LaneIntentFromCardinalRoadMask(laneMovementMask);
        resolvedCell.surfaceMask |= SurfaceMaskFor(RoadLaneSurface::Asphalt);
        resolvedCell.laneCount = static_cast<std::uint8_t>(std::min(255, static_cast<int>(resolvedCell.laneCount) + 1));
        resolvedCell.laneTypeCosts[static_cast<std::size_t>(graphicCell.primary.laneType)] = TraversalCostForLaneType(graphicCell.primary.laneType, family, lane.laneIndex);

        if (graphicCell.secondary.mode == CommuterMode::Pedestrian) {
            resolvedCell.laneTypeMask |= LaneTypeMaskFor(RoadLaneTypeId::Pedestrian);
            resolvedCell.travelMask |= LaneIntentFromCardinalRoadMask(graphicCell.secondary.directionMask);
            resolvedCell.surfaceMask |= SurfaceMaskFor(RoadLaneSurface::Sidewalk);
            resolvedCell.laneCount = static_cast<std::uint8_t>(std::min(255, static_cast<int>(resolvedCell.laneCount) + 1));
            resolvedCell.laneTypeCosts[static_cast<std::size_t>(RoadLaneTypeId::Pedestrian)] = TraversalCostForLaneType(RoadLaneTypeId::Pedestrian, family, lane.laneIndex);
        } else if (graphicCell.secondary.parallelGraphic.primitive() == RoadGraphicPrimitive::Median) {
            resolvedCell.surfaceMask |= SurfaceMaskFor(RoadLaneSurface::Median);
        }
    }

    if (((renderState.laneGraphicMask >> kRoadSurfaceCrosswalkShift) & kRoadSurfaceSidewalkEdgeMask) != 0) {
        resolvedCell.surfaceMask |= kRoadSurfaceCrosswalk;
    }

    const std::uint8_t carExitMask = carMovementMask;
    const std::uint8_t exitMask = static_cast<std::uint8_t>(carMovementMask | pedestrianMovementMask);
    RoadRenderVariant renderVariant = ChooseRenderVariant(junctionMask);
    const std::uint8_t baseGlyphJunctionMask = junctionMask;
    const RoadFamily glyphFamily = (resolvedCell.laneTypeMask & static_cast<std::uint8_t>(kRoadLaneTypeMedium | kRoadLaneTypeFast)) != 0
        ? RoadFamily::Highway
        : family;

    resolvedCell.family = static_cast<std::uint8_t>(family);
    resolvedCell.exitMask = exitMask;
    resolvedCell.junctionMask = junctionMask;
    resolvedCell.renderVariant = static_cast<std::uint8_t>(renderVariant);
    resolvedCell.baseGlyph = static_cast<std::uint8_t>(ChooseBaseGlyph(glyphFamily, renderVariant, baseGlyphJunctionMask));
    const RoadArrowGlyph turnArrowGlyph = ChooseTurnArrowGlyph(buildTurnArrowIntentMask(layer, tileX, tileY, tile));
    const RoadArrowGlyph debugArrowGlyph = ChooseArrowGlyph(LaneIntentFromCardinalRoadMask(carExitMask));
    if (turnArrowGlyph != RoadArrowGlyph::None) {
        resolvedCell.arrowGlyph = static_cast<std::uint8_t>(turnArrowGlyph);
    } else if (debugArrowGlyph != RoadArrowGlyph::None) {
        resolvedCell.arrowGlyph = static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(debugArrowGlyph) | kRoadArrowDebugFlag);
    }
    resolvedCell.surfaceEdgeMask = renderState.laneGraphicMask;
    resolvedCell.dividerMask = renderState.dividerMask;

    resolvedCells_[slot] = resolvedCell;
    if (layer == TransportLayerId::Ground) {
        groundRoadRenderState_[groundRenderOffset + 0u] = resolvedCell.baseGlyph;
        groundRoadRenderState_[groundRenderOffset + 1u] = resolvedCell.arrowGlyph;
        groundRoadRenderState_[groundRenderOffset + 2u] = resolvedCell.surfaceEdgeMask;
        groundRoadRenderState_[groundRenderOffset + 3u] = resolvedCell.dividerMask;
    }
}

void TransportNetwork::rebuildCostMapAndTrafficOverlay() {
    costMap_.clear();

    std::size_t layerIndex = 0;
    for (; layerIndex < layerCount(); ++layerIndex) {
        const TransportLayerId layer = static_cast<TransportLayerId>(layerIndex);
        int tileY = 0;
        for (; tileY < height_; ++tileY) {
            int tileX = 0;
            for (; tileX < width_; ++tileX) {
                const TransportTile* tile = tileAt(layer, tileX, tileY);
                if (tile == 0 || tile->empty()) {
                    continue;
                }

                const std::vector<RoadLanePlacement>& lanes = tile->lanes();
                std::size_t laneIndex = 0;
                for (; laneIndex < lanes.size(); ++laneIndex) {
                    if (lanes[laneIndex].active && lanes[laneIndex].isCar()) {
                        addLaneToCostMap(layer, tileX, tileY, lanes[laneIndex]);
                    }
                }
            }
        }
    }

    costMap_.finalizeTransferEdges();
    refreshTrafficOverlayState();
    bumpAllTrafficOverlayChunkRevisions();
    ++trafficOverlayRevision_;
}

void TransportNetwork::rebuildCostMapAndTrafficOverlayForTiles(const std::vector<int>& dirtyTileIndices) {
    if (dirtyTileIndices.empty()) {
        return;
    }

    std::vector<int> validTileIndices;
    validTileIndices.reserve(dirtyTileIndices.size());
    std::size_t dirtyIndex = 0;
    for (; dirtyIndex < dirtyTileIndices.size(); ++dirtyIndex) {
        const int dirtyTileIndex = dirtyTileIndices[dirtyIndex];
        if (dirtyTileIndex >= 0 && dirtyTileIndex < static_cast<int>(totalTileCount_)) {
            validTileIndices.push_back(dirtyTileIndex);
        }
    }

    if (validTileIndices.empty()) {
        return;
    }

    std::sort(validTileIndices.begin(), validTileIndices.end());
    validTileIndices.erase(std::unique(validTileIndices.begin(), validTileIndices.end()), validTileIndices.end());

    for (dirtyIndex = 0; dirtyIndex < validTileIndices.size(); ++dirtyIndex) {
        std::size_t layerIndex = 0;
        for (; layerIndex < layerCount(); ++layerIndex) {
            costMap_.clearCostsForTile(static_cast<TransportLayerId>(layerIndex), validTileIndices[dirtyIndex]);
        }
    }

    for (dirtyIndex = 0; dirtyIndex < validTileIndices.size(); ++dirtyIndex) {
        const int dirtyTileIndex = validTileIndices[dirtyIndex];
        const int tileY = dirtyTileIndex / width_;
        const int tileX = dirtyTileIndex - (tileY * width_);

        std::size_t layerIndex = 0;
        for (; layerIndex < layerCount(); ++layerIndex) {
            const TransportLayerId layer = static_cast<TransportLayerId>(layerIndex);
            const TransportTile* tile = tileAt(layer, tileX, tileY);
            if (tile == 0 || tile->empty()) {
                continue;
            }

            const std::vector<RoadLanePlacement>& lanes = tile->lanes();
            std::size_t laneIndex = 0;
            for (; laneIndex < lanes.size(); ++laneIndex) {
                if (lanes[laneIndex].active && lanes[laneIndex].isCar()) {
                    addLaneToCostMap(layer, tileX, tileY, lanes[laneIndex]);
                }
            }
        }
    }

    costMap_.buildTrafficOverlayForTiles(validTileIndices, trafficOverlayState_);
    bumpTrafficOverlayChunkRevisionsForTiles(validTileIndices);
    ++trafficOverlayRevision_;
}

void TransportNetwork::addLaneToCostMap(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) {
    if (!lanePlacement.active || !lanePlacement.isCar()) {
        return;
    }

    const TransportTile* laneTile = tileAt(layer, tileX, tileY);
    if (laneTile == 0) {
        return;
    }

    const std::uint8_t movementMask = pathLaneMovementMask(layer, tileX, tileY, lanePlacement);
    RoadLaneCellContext laneCellContext = roadLaneCellContext(layer, tileX, tileY, lanePlacement);
    laneCellContext.directionMask = laneGraphicDirectionMask(layer, tileX, tileY, lanePlacement);
    laneCellContext.travelDirectionMask = laneCenterTravelDirectionMask(layer, tileX, tileY, lanePlacement);
    laneCellContext.pathDirectionMask = movementMask;
    const RoadLaneCell cell = Road::makeLaneCell(lanePlacement, laneCellContext);
    const std::uint16_t traversalCost = TraversalCostForLaneType(cell.primary.laneType, lanePlacement.family, lanePlacement.laneIndex);
    std::size_t directionIndex = 0;
    for (; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        if ((movementMask & direction) == 0) {
            continue;
        }

        costMap_.addDirectionalCost(layer, TransportMode::Car, lanePlacement.tileIndex, direction, traversalCost, static_cast<std::uint16_t>(cell.primary.capacity));
    }

    if (cell.secondary.mode != CommuterMode::Pedestrian) {
        return;
    }

    for (directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        if ((cell.secondary.directionMask & direction) != 0) {
            costMap_.addDirectionalCost(layer, TransportMode::Pedestrian, lanePlacement.tileIndex, direction, TraversalCostForLaneType(RoadLaneTypeId::Pedestrian, lanePlacement.family, lanePlacement.laneIndex), static_cast<std::uint16_t>(cell.secondary.capacity));
        }
    }

    const std::uint8_t buildingAccessMask = cell.secondaryEdgeMask();
    if (layer == TransportLayerId::Ground && buildingAccessMask != 0u) {
        costMap_.addBuildingAccess(layer, TransportMode::Pedestrian, lanePlacement.tileIndex, buildingAccessMask);
        costMap_.addBuildingAccess(layer, TransportMode::Car, lanePlacement.tileIndex, buildingAccessMask);
    }
}

void TransportNetwork::refreshTrafficOverlayState() {
    costMap_.buildTrafficOverlay(trafficOverlayState_);
}

void TransportNetwork::bumpAllTrafficOverlayChunkRevisions() {
    std::size_t chunkIndex = 0;
    for (; chunkIndex < trafficOverlayChunkRevisions_.size(); ++chunkIndex) {
        ++trafficOverlayChunkRevisions_[chunkIndex];
    }
}

void TransportNetwork::bumpTrafficOverlayChunkRevisionsForTiles(const std::vector<int>& dirtyTileIndices) {
    std::vector<int> dirtyChunkIndices;
    dirtyChunkIndices.reserve(dirtyTileIndices.size());

    std::size_t dirtyIndex = 0;
    for (; dirtyIndex < dirtyTileIndices.size(); ++dirtyIndex) {
        const int dirtyTileIndex = dirtyTileIndices[dirtyIndex];
        if (dirtyTileIndex < 0 || dirtyTileIndex >= static_cast<int>(totalTileCount_)) {
            continue;
        }

        const int tileY = dirtyTileIndex / width_;
        const int tileX = dirtyTileIndex - (tileY * width_);
        const int dirtyChunkIndex = chunkIndexForTile(tileX, tileY);
        if (dirtyChunkIndex >= 0) {
            dirtyChunkIndices.push_back(dirtyChunkIndex);
        }
    }

    std::sort(dirtyChunkIndices.begin(), dirtyChunkIndices.end());
    dirtyChunkIndices.erase(std::unique(dirtyChunkIndices.begin(), dirtyChunkIndices.end()), dirtyChunkIndices.end());

    std::size_t chunkIndex = 0;
    for (; chunkIndex < dirtyChunkIndices.size(); ++chunkIndex) {
        const int dirtyChunkIndex = dirtyChunkIndices[chunkIndex];
        if (dirtyChunkIndex >= 0 && dirtyChunkIndex < static_cast<int>(trafficOverlayChunkRevisions_.size())) {
            ++trafficOverlayChunkRevisions_[static_cast<std::size_t>(dirtyChunkIndex)];
        }
    }
}

void TransportNetwork::markDirtyNeighborhood(const std::vector<RoadTilePlacement>& placements, std::vector<int>& dirtyTileIndices) const {
    dirtyTileIndices.reserve(placements.size() * 9);
    std::size_t placementIndex = 0;
    for (; placementIndex < placements.size(); ++placementIndex) {
        const RoadTilePlacement& placement = placements[placementIndex];
        int neighborTileY = placement.tileY - kRoadImmediateDirtyRadius;
        for (; neighborTileY <= placement.tileY + kRoadImmediateDirtyRadius; ++neighborTileY) {
            int neighborTileX = placement.tileX - kRoadImmediateDirtyRadius;
            for (; neighborTileX <= placement.tileX + kRoadImmediateDirtyRadius; ++neighborTileX) {
                if (isTileInsideMap(neighborTileX, neighborTileY)) {
                    dirtyTileIndices.push_back(tileIndex(neighborTileX, neighborTileY));
                }
            }
        }
    }

    std::sort(dirtyTileIndices.begin(), dirtyTileIndices.end());
    dirtyTileIndices.erase(std::unique(dirtyTileIndices.begin(), dirtyTileIndices.end()), dirtyTileIndices.end());
}

void TransportNetwork::markDirtyTileNeighborhood(const std::vector<int>& tileIndices, std::vector<int>& dirtyTileIndices) const {
    dirtyTileIndices.reserve(tileIndices.size() * 9u);
    std::size_t tileListIndex = 0;
    for (; tileListIndex < tileIndices.size(); ++tileListIndex) {
        const int centerTileIndex = tileIndices[tileListIndex];
        if (centerTileIndex < 0 || centerTileIndex >= static_cast<int>(totalTileCount_)) {
            continue;
        }

        const int centerTileY = centerTileIndex / width_;
        const int centerTileX = centerTileIndex - (centerTileY * width_);
        int neighborTileY = centerTileY - kRoadImmediateDirtyRadius;
        for (; neighborTileY <= centerTileY + kRoadImmediateDirtyRadius; ++neighborTileY) {
            int neighborTileX = centerTileX - kRoadImmediateDirtyRadius;
            for (; neighborTileX <= centerTileX + kRoadImmediateDirtyRadius; ++neighborTileX) {
                if (isTileInsideMap(neighborTileX, neighborTileY)) {
                    dirtyTileIndices.push_back(tileIndex(neighborTileX, neighborTileY));
                }
            }
        }
    }

    std::sort(dirtyTileIndices.begin(), dirtyTileIndices.end());
    dirtyTileIndices.erase(std::unique(dirtyTileIndices.begin(), dirtyTileIndices.end()), dirtyTileIndices.end());
}

void TransportNetwork::expandDirtyRoadDependencies(TransportLayerId layer, std::vector<int>& dirtyTileIndices) const {
    if (dirtyTileIndices.empty()) {
        return;
    }

    std::vector<bool> queued(totalTileCount_, false);
    std::vector<bool> seed(totalTileCount_, false);
    std::vector<int> expandedTileIndices;
    expandedTileIndices.reserve(dirtyTileIndices.size());

    std::size_t dirtyIndex = 0;
    for (; dirtyIndex < dirtyTileIndices.size(); ++dirtyIndex) {
        const int dirtyTileIndex = dirtyTileIndices[dirtyIndex];
        if (dirtyTileIndex < 0 || dirtyTileIndex >= static_cast<int>(totalTileCount_)) {
            continue;
        }

        if (!queued[static_cast<std::size_t>(dirtyTileIndex)]) {
            queued[static_cast<std::size_t>(dirtyTileIndex)] = true;
            seed[static_cast<std::size_t>(dirtyTileIndex)] = true;
            expandedTileIndices.push_back(dirtyTileIndex);
        }
    }

    std::size_t readIndex = 0;
    for (; readIndex < expandedTileIndices.size(); ++readIndex) {
        const int currentTileIndex = expandedTileIndices[readIndex];
        const int currentTileY = currentTileIndex / width_;
        const int currentTileX = currentTileIndex - (currentTileY * width_);
        if (!seed[static_cast<std::size_t>(currentTileIndex)] &&
            tileIsStableStraightSandwich(layer, currentTileX, currentTileY)) {
            continue;
        }

        std::size_t directionIndex = 0;
        for (; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
            const std::uint8_t direction = kCardinalDirections[directionIndex];
            const int neighborTileX = currentTileX + DeltaXForDirection(direction);
            const int neighborTileY = currentTileY + DeltaYForDirection(direction);
            if (!isTileInsideMap(neighborTileX, neighborTileY)) {
                continue;
            }

            const int neighborTileIndex = tileIndex(neighborTileX, neighborTileY);
            if (queued[static_cast<std::size_t>(neighborTileIndex)] ||
                !tileHasActiveRoadLane(layer, neighborTileX, neighborTileY)) {
                continue;
            }

            queued[static_cast<std::size_t>(neighborTileIndex)] = true;
            expandedTileIndices.push_back(neighborTileIndex);
        }
    }

    dirtyTileIndices.swap(expandedTileIndices);
    std::sort(dirtyTileIndices.begin(), dirtyTileIndices.end());
}

void TransportNetwork::bumpDirtyChunkRevisions(TransportLayerId layer, const std::vector<int>& dirtyTileIndices) {
    std::vector<int> dirtyChunkIndices;
    dirtyChunkIndices.reserve(dirtyTileIndices.size());

    std::size_t dirtyIndex = 0;
    for (; dirtyIndex < dirtyTileIndices.size(); ++dirtyIndex) {
        const int dirtyTileIndex = dirtyTileIndices[dirtyIndex];
        const int tileY = dirtyTileIndex / width_;
        const int tileX = dirtyTileIndex - (tileY * width_);
        const int dirtyChunkIndex = chunkIndexForTile(tileX, tileY);
        if (dirtyChunkIndex >= 0) {
            dirtyChunkIndices.push_back(dirtyChunkIndex);
        }
    }

    std::sort(dirtyChunkIndices.begin(), dirtyChunkIndices.end());
    dirtyChunkIndices.erase(std::unique(dirtyChunkIndices.begin(), dirtyChunkIndices.end()), dirtyChunkIndices.end());

    std::size_t chunkIndex = 0;
    for (; chunkIndex < dirtyChunkIndices.size(); ++chunkIndex) {
        const int dirtyChunkIndex = dirtyChunkIndices[chunkIndex];
        if (dirtyChunkIndex < 0 || dirtyChunkIndex >= static_cast<int>(chunkLayout_.size())) {
            continue;
        }

        if (layer == TransportLayerId::Ground) {
            ++groundChunkRevisions_[static_cast<std::size_t>(dirtyChunkIndex)];
        } else if (layer == TransportLayerId::Elevated) {
            ++elevatedChunkRevisions_[static_cast<std::size_t>(dirtyChunkIndex)];
        }
    }
}

const TransportTile* TransportNetwork::tileAt(TransportLayerId layer, int tileX, int tileY) const {
    if (!isTileInsideMap(tileX, tileY)) {
        return 0;
    }

    return &transportTiles_[slotIndex(layer, tileIndex(tileX, tileY), totalTileCount_)];
}

TransportTile* TransportNetwork::tileAt(TransportLayerId layer, int tileX, int tileY) {
    if (!isTileInsideMap(tileX, tileY)) {
        return 0;
    }

    return &transportTiles_[slotIndex(layer, tileIndex(tileX, tileY), totalTileCount_)];
}

bool TransportNetwork::tileHasActiveRoadLane(TransportLayerId layer, int tileX, int tileY) const {
    const TransportTile* tile = tileAt(layer, tileX, tileY);
    return tile != 0 && !tile->empty();
}

bool TransportNetwork::tileIsStableStraightSandwich(TransportLayerId layer, int tileX, int tileY) const {
    const TransportTile* tile = tileAt(layer, tileX, tileY);
    if (tile == 0 || tile->empty()) {
        return false;
    }

    const bool hasHorizontalCarBody = tile->hasCarAxis(RoadAxis::Horizontal);
    const bool hasVerticalCarBody = tile->hasCarAxis(RoadAxis::Vertical);
    if (hasHorizontalCarBody == hasVerticalCarBody) {
        return false;
    }

    const RoadAxis axis = hasHorizontalCarBody ? RoadAxis::Horizontal : RoadAxis::Vertical;
    const std::uint8_t axisDirectionMask = axis == RoadAxis::Horizontal
        ? static_cast<std::uint8_t>(kRoadDirectionEast | kRoadDirectionWest)
        : static_cast<std::uint8_t>(kRoadDirectionNorth | kRoadDirectionSouth);

    bool hasActiveCarLane = false;
    bool hasActiveSeparatorLane = false;
    const std::vector<RoadLanePlacement>& lanes = tile->lanes();
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes[laneIndex];
        if (!lane.active) {
            continue;
        }

        if (lane.isCar()) {
            hasActiveCarLane = true;
            if ((RoadDirectionMaskForLane(lane) & ~axisDirectionMask) != 0) {
                return false;
            }
        } else if (lane.isSeparator()) {
            hasActiveSeparatorLane = true;
        }
    }

    if (!hasActiveCarLane || !hasActiveSeparatorLane) {
        return false;
    }

    const std::uint8_t firstDirection = axis == RoadAxis::Horizontal ? kRoadDirectionEast : kRoadDirectionNorth;
    const std::uint8_t secondDirection = OppositeCardinal(firstDirection);
    return tileHasCarBodyAxis(layer, tileX + DeltaXForDirection(firstDirection), tileY + DeltaYForDirection(firstDirection), axis, tile->family()) &&
        tileHasCarBodyAxis(layer, tileX + DeltaXForDirection(secondDirection), tileY + DeltaYForDirection(secondDirection), axis, tile->family());
}

void TransportNetwork::collectRoadRemovalFootprint(TransportLayerId layer, int tileX, int tileY, RoadAxis preferredAxis, std::vector<RoadLanePlacement>& removalLanes) const {
    const TransportTile* tile = tileAt(layer, tileX, tileY);
    if (tile == 0 || tile->lanes().empty()) {
        return;
    }

    const std::vector<RoadLanePlacement>& lanes = tile->lanes();
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        if (lanes[laneIndex].active &&
            (preferredAxis == RoadAxis::None || lanes[laneIndex].axis == preferredAxis)) {
            collectRoadSliceFootprint(layer, tileX, tileY, lanes[laneIndex], removalLanes);
        }
    }

    if (removalLanes.empty()) {
        RoadLanePlacement removalLane;
        removalLane.layer = layer;
        removalLane.tileX = tileX;
        removalLane.tileY = tileY;
        removalLane.tileIndex = tileIndex(tileX, tileY);
        removalLanes.push_back(removalLane);
    }
}

void TransportNetwork::collectRoadSliceFootprint(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& referenceLane, std::vector<RoadLanePlacement>& removalLanes) const {
    if (referenceLane.axis == RoadAxis::None) {
        return;
    }

    int scanTileX = tileX;
    int scanTileY = tileY;
    for (; isTileInsideMap(scanTileX, scanTileY) && tileHasMatchingRoadSliceLane(layer, scanTileX, scanTileY, referenceLane);) {
        RoadLanePlacement removalLane = referenceLane;
        removalLane.tileX = scanTileX;
        removalLane.tileY = scanTileY;
        removalLane.tileIndex = tileIndex(scanTileX, scanTileY);
        removalLanes.push_back(removalLane);
        if (referenceLane.axis == RoadAxis::Horizontal) {
            --scanTileY;
        } else {
            --scanTileX;
        }
    }

    scanTileX = referenceLane.axis == RoadAxis::Horizontal ? tileX : tileX + 1;
    scanTileY = referenceLane.axis == RoadAxis::Horizontal ? tileY + 1 : tileY;
    for (; isTileInsideMap(scanTileX, scanTileY) && tileHasMatchingRoadSliceLane(layer, scanTileX, scanTileY, referenceLane);) {
        RoadLanePlacement removalLane = referenceLane;
        removalLane.tileX = scanTileX;
        removalLane.tileY = scanTileY;
        removalLane.tileIndex = tileIndex(scanTileX, scanTileY);
        removalLanes.push_back(removalLane);
        if (referenceLane.axis == RoadAxis::Horizontal) {
            ++scanTileY;
        } else {
            ++scanTileX;
        }
    }
}

bool TransportNetwork::tileHasMatchingRoadSliceLane(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& referenceLane) const {
    const TransportTile* tile = tileAt(layer, tileX, tileY);
    if (tile == 0) {
        return false;
    }

    const std::vector<RoadLanePlacement>& lanes = tile->lanes();
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes[laneIndex];
        if (!lane.active ||
            lane.family != referenceLane.family ||
            lane.templateId != referenceLane.templateId ||
            lane.axis != referenceLane.axis) {
            continue;
        }

        return true;
    }

    return false;
}

bool TransportNetwork::removeMatchingRoadSliceLanes(TransportLayerId layer, const RoadLanePlacement& referenceLane) {
    const std::size_t slot = slotIndex(layer, referenceLane.tileIndex, totalTileCount_);
    if (slot >= transportTiles_.size()) {
        return false;
    }

    std::vector<RoadLanePlacement>& lanes = transportTiles_[slot].lanesForMutation();
    const std::size_t originalLaneCount = lanes.size();
    lanes.erase(std::remove_if(lanes.begin(), lanes.end(),
        [&referenceLane](const RoadLanePlacement& lane) {
            return lane.active &&
                lane.family == referenceLane.family &&
                lane.templateId == referenceLane.templateId &&
                lane.axis == referenceLane.axis;
        }),
        lanes.end());
    if (lanes.empty()) {
        transportTiles_[slot].clear();
    }

    return lanes.size() != originalLaneCount;
}

std::uint8_t TransportNetwork::capReturnDirectionForLane(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection) const {
    if (roadDirection == 0 || lanePlacement.axis == RoadAxis::None || !CarLaneAllowsCapReturn(lanePlacement)) {
        return 0;
    }

    if (tileHasAnyCarBody(layer, tileX + DeltaXForDirection(roadDirection),
        tileY + DeltaYForDirection(roadDirection), lanePlacement.family)) {
        return 0;
    }

    const bool horizontal = lanePlacement.axis == RoadAxis::Horizontal;
    int minCross = horizontal ? tileY : tileX;
    int maxCross = minCross;
    while (tileHasCarBodyAxis(layer, horizontal ? tileX : minCross - 1, horizontal ? minCross - 1 : tileY, lanePlacement.axis, lanePlacement.family)) {
        --minCross;
    }
    while (tileHasCarBodyAxis(layer, horizontal ? tileX : maxCross + 1, horizontal ? maxCross + 1 : tileY, lanePlacement.axis, lanePlacement.family)) {
        ++maxCross;
    }

    const int width = (maxCross - minCross) + 1;
    if (width < 2) {
        return 0;
    }

    const int crossOffset = (horizontal ? tileY : tileX) - minCross;
    const std::uint8_t turnDirection = crossOffset * 2 < width
        ? (horizontal ? kRoadDirectionSouth : kRoadDirectionEast)
        : (horizontal ? kRoadDirectionNorth : kRoadDirectionWest);
    return turnDirection;
}

bool TransportNetwork::laneHasAuthoredContinuation(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection) const {
    const TransportTile* neighborTile = tileAt(layer, tileX + DeltaXForDirection(roadDirection), tileY + DeltaYForDirection(roadDirection));
    if (neighborTile == 0 || neighborTile->family() != lanePlacement.family) {
        return false;
    }

    const std::vector<RoadLanePlacement>& lanes = neighborTile->lanes();
    for (std::size_t laneIndex = 0; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& neighborLane = lanes[laneIndex];
        if (neighborLane.active &&
            neighborLane.isCar() &&
            neighborLane.templateId == lanePlacement.templateId &&
            neighborLane.laneIndex == lanePlacement.laneIndex &&
            neighborLane.axis == lanePlacement.axis &&
            neighborLane.sideOverlaps(lanePlacement) &&
            neighborLane.hasTravelDirection(roadDirection)) {
            return true;
        }
    }

    return false;
}

std::uint8_t TransportNetwork::pathLaneMovementMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const {
    if (!lanePlacement.active) {
        return 0;
    }

    if (!lanePlacement.isCar()) {
        return RoadDirectionMaskForLane(lanePlacement);
    }

    std::uint8_t movementMask = 0;
    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        if (!lanePlacement.hasTravelDirection(direction)) {
            continue;
        }

        if (laneHasAuthoredContinuation(layer, tileX, tileY, lanePlacement, direction)) {
            movementMask |= direction;
            continue;
        }

        const std::uint8_t capMovementMask = capReturnDirectionForLane(layer, tileX, tileY, lanePlacement, direction);
        if (capMovementMask != 0) {
            return capMovementMask;
        }
    }

    return movementMask;
}

std::uint8_t TransportNetwork::laneGraphicDirectionMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const {
    if (!lanePlacement.active || !lanePlacement.isCar()) {
        return RoadDirectionMaskForLane(lanePlacement);
    }

    std::uint8_t movementMask = 0;
    std::uint8_t blockedTravelMask = 0;
    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        if (!lanePlacement.hasTravelDirection(direction)) {
            continue;
        }

        if (laneHasAuthoredContinuation(layer, tileX, tileY, lanePlacement, direction)) {
            movementMask |= direction;
            continue;
        }

        const std::uint8_t capMovementMask = capReturnDirectionForLane(layer, tileX, tileY, lanePlacement, direction);
        if (capMovementMask != 0) {
            blockedTravelMask |= direction;
            movementMask |= static_cast<std::uint8_t>(direction | capMovementMask);
        }
    }

    std::uint8_t incomingMask = 0;
    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t edgeDirection = kCardinalDirections[directionIndex];
        if ((blockedTravelMask & OppositeCardinal(edgeDirection)) != 0) {
            continue;
        }

        const int neighborTileX = tileX + DeltaXForDirection(edgeDirection);
        const int neighborTileY = tileY + DeltaYForDirection(edgeDirection);
        const TransportTile* neighborTile = tileAt(layer, neighborTileX, neighborTileY);
        if (neighborTile == 0 || neighborTile->family() != lanePlacement.family) {
            continue;
        }

        const std::vector<RoadLanePlacement>& lanes = neighborTile->lanes();
        for (std::size_t laneIndex = 0; laneIndex < lanes.size(); ++laneIndex) {
            if (lanes[laneIndex].active &&
                lanes[laneIndex].isCar() &&
                (pathLaneMovementMask(layer, neighborTileX, neighborTileY, lanes[laneIndex]) & OppositeCardinal(edgeDirection)) != 0) {
                incomingMask |= edgeDirection;
                break;
            }
        }
    }

    assert(CountCardinalDirections(movementMask) <= 2);
    assert(CountCardinalDirections(incomingMask) <= 2);
    if (CountCardinalDirections(movementMask) > 2 || CountCardinalDirections(incomingMask) > 2) {
        return 0;
    }

    const std::uint8_t graphicMask = static_cast<std::uint8_t>(movementMask | incomingMask);
    return graphicMask != 0 ? graphicMask : RoadDirectionMaskForLane(lanePlacement);
}

std::uint8_t TransportNetwork::laneCenterTravelDirectionMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const {
    if (!lanePlacement.active || !lanePlacement.isCar()) {
        return RoadDirectionMaskForLane(lanePlacement);
    }

    std::uint8_t travelMask = 0;
    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        if (!lanePlacement.hasTravelDirection(direction)) {
            continue;
        }

        travelMask |= direction;
        if (!laneHasAuthoredContinuation(layer, tileX, tileY, lanePlacement, direction)) {
            travelMask |= capReturnDirectionForLane(layer, tileX, tileY, lanePlacement, direction);
        }
    }

    assert(CountCardinalDirections(travelMask) <= 2);
    if (CountCardinalDirections(travelMask) > 2) {
        return 0;
    }

    const std::uint8_t fallbackTravelMask = RoadDirectionMaskForLane(lanePlacement);
    assert(CountCardinalDirections(fallbackTravelMask) <= 2);
    if (CountCardinalDirections(fallbackTravelMask) > 2) {
        return 0;
    }

    return travelMask != 0 ? travelMask : fallbackTravelMask;
}

RoadLaneCellContext TransportNetwork::roadLaneCellContext(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const {
    RoadLaneCellContext context;
    if (TemplateKindFromTemplateId(lanePlacement.templateId) == RoadTemplateKind::Avenue) {
        context.hasAvenueTileRole = true;
        context.avenueOuterTile = avenueTileIsOuter(layer, tileX, tileY);
    }
    if (DirectionModeFromTemplateId(lanePlacement.templateId) != RoadDirectionMode::TwoWay) {
        context.hasOneWayLeftNeighbor = true;
        context.oneWayLeftNeighbor = oneWayRoadHasLeftNeighbor(layer, tileX, tileY, lanePlacement);
    }
    return context;
}

bool TransportNetwork::avenueTileIsOuter(TransportLayerId layer, int tileX, int tileY) const {
    int avenueNeighborCount = 0;
    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        if (tileHasAvenueCarBody(layer, tileX + DeltaXForDirection(direction), tileY + DeltaYForDirection(direction))) {
            ++avenueNeighborCount;
        }
    }
    return avenueNeighborCount < 4;
}

bool TransportNetwork::tileHasAvenueCarBody(TransportLayerId layer, int tileX, int tileY) const {
    const TransportTile* tile = tileAt(layer, tileX, tileY);
    if (tile == 0) {
        return false;
    }

    const std::vector<RoadLanePlacement>& lanes = tile->lanes();
    for (std::size_t laneIndex = 0; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes[laneIndex];
        if (lane.active && lane.isCar() && TemplateKindFromTemplateId(lane.templateId) == RoadTemplateKind::Avenue) {
            return true;
        }
    }
    return false;
}

bool TransportNetwork::oneWayRoadHasLeftNeighbor(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const {
    const std::uint8_t travelMask = RoadDirectionMaskForLane(lanePlacement);
    if (CountCardinalDirections(travelMask) != 1) {
        return false;
    }

    std::uint8_t travelDirection = 0;
    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        if ((travelMask & kCardinalDirections[directionIndex]) != 0) {
            travelDirection = kCardinalDirections[directionIndex];
            break;
        }
    }

    const std::uint8_t leftDirection = LeftDirectionForTravelDirection(travelDirection);
    const TransportTile* leftTile = tileAt(layer, tileX + DeltaXForDirection(leftDirection), tileY + DeltaYForDirection(leftDirection));
    if (leftTile == 0 || leftTile->family() != lanePlacement.family) {
        return false;
    }

    const std::vector<RoadLanePlacement>& lanes = leftTile->lanes();
    for (std::size_t laneIndex = 0; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& leftLane = lanes[laneIndex];
        if (!leftLane.active || !leftLane.isCar() || DirectionModeFromTemplateId(leftLane.templateId) == RoadDirectionMode::TwoWay) {
            continue;
        }

        if (leftLane.templateId == lanePlacement.templateId &&
            leftLane.hasTravelDirection(travelDirection)) {
            return true;
        }
    }
    return false;
}

bool TransportNetwork::isCarIntersectionCollectionTile(TransportLayerId layer, int tileX, int tileY) const {
    const TransportTile* tile = tileAt(layer, tileX, tileY);
    if (tile == 0 || tile->empty() || !tile->hasLaneType(RoadLaneTypeId::Car)) {
        return false;
    }

    if (!tile->hasCarAxis(RoadAxis::Horizontal) || !tile->hasCarAxis(RoadAxis::Vertical)) {
        return false;
    }

    std::uint8_t graphicMask = 0;
    const std::vector<RoadLanePlacement>& lanes = tile->lanes();
    for (std::size_t laneIndex = 0; laneIndex < lanes.size(); ++laneIndex) {
        if (lanes[laneIndex].active && lanes[laneIndex].isCar()) {
            graphicMask |= laneGraphicDirectionMask(layer, tileX, tileY, lanes[laneIndex]);
        }
    }

    return CountCardinalDirections(graphicMask) >= 3;
}

std::uint8_t TransportNetwork::buildCarExitMask(TransportLayerId layer, int tileX, int tileY, const TransportTile& tile) const {
    std::uint8_t exitMask = 0;
    const std::vector<RoadLanePlacement>& lanes = tile.lanes();
    for (std::size_t laneIndex = 0; laneIndex < lanes.size(); ++laneIndex) {
        if (lanes[laneIndex].active && lanes[laneIndex].isCar()) {
            exitMask |= pathLaneMovementMask(layer, tileX, tileY, lanes[laneIndex]);
        }
    }
    return exitMask;
}

std::uint8_t TransportNetwork::buildTurnExitMaskThroughIntersection(TransportLayerId layer, int entryTileX, int entryTileY, std::uint8_t travelDirection) const {
    std::uint8_t exitMask = 0;
    int currentTileX = entryTileX;
    int currentTileY = entryTileY;
    const int maximumSteps = std::max(width_, height_);
    for (int stepIndex = 0; stepIndex < maximumSteps; ++stepIndex) {
        if (!isCarIntersectionCollectionTile(layer, currentTileX, currentTileY)) {
            break;
        }

        const TransportTile* currentTile = tileAt(layer, currentTileX, currentTileY);
        if (currentTile == 0) {
            break;
        }

        const std::uint8_t currentExitMask = buildCarExitMask(layer, currentTileX, currentTileY, *currentTile);
        exitMask |= currentExitMask;
        if ((currentExitMask & travelDirection) == 0) {
            break;
        }

        const int nextTileX = currentTileX + DeltaXForDirection(travelDirection);
        const int nextTileY = currentTileY + DeltaYForDirection(travelDirection);
        if (!isCarIntersectionCollectionTile(layer, nextTileX, nextTileY)) {
            break;
        }

        currentTileX = nextTileX;
        currentTileY = nextTileY;
    }

    return static_cast<std::uint8_t>(
        exitMask &
        (kRoadDirectionNorth | kRoadDirectionEast | kRoadDirectionSouth | kRoadDirectionWest) &
        ~OppositeCardinal(travelDirection));
}

std::uint8_t TransportNetwork::buildTurnArrowIntentMask(TransportLayerId layer, int tileX, int tileY, const TransportTile& tile) const {
    if (isCarIntersectionCollectionTile(layer, tileX, tileY)) {
        return 0;
    }

    std::uint8_t turnIntentMask = 0;
    const std::vector<RoadLanePlacement>& lanes = tile.lanes();
    for (std::size_t laneIndex = 0; laneIndex < lanes.size(); ++laneIndex) {
        if (!lanes[laneIndex].active || !lanes[laneIndex].isCar()) {
            continue;
        }

        const std::uint8_t movementMask = pathLaneMovementMask(layer, tileX, tileY, lanes[laneIndex]);
        for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
            const std::uint8_t direction = kCardinalDirections[directionIndex];
            if ((movementMask & direction) == 0) {
                continue;
            }

            const int candidateTileX = tileX + DeltaXForDirection(direction);
            const int candidateTileY = tileY + DeltaYForDirection(direction);
            if (isCarIntersectionCollectionTile(layer, candidateTileX, candidateTileY)) {
                turnIntentMask |= LaneIntentFromCardinalRoadMask(buildTurnExitMaskThroughIntersection(layer, candidateTileX, candidateTileY, direction));
            }
        }
    }

    return turnIntentMask;
}

bool TransportNetwork::tileHasCarBodyAxis(TransportLayerId layer, int tileX, int tileY, RoadAxis axis, RoadFamily family) const {
    const TransportTile* tile = tileAt(layer, tileX, tileY);
    if (tile == 0 || tile->family() != family) {
        return false;
    }

    return tile->hasCarAxis(axis);
}

bool TransportNetwork::tileHasAnyCarBody(TransportLayerId layer, int tileX, int tileY, RoadFamily family) const {
    const TransportTile* tile = tileAt(layer, tileX, tileY);
    return tile != 0 && tile->family() == family && tile->hasLaneType(RoadLaneTypeId::Car);
}
