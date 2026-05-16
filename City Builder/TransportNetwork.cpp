#include "TransportNetwork.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>

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

bool DirectionIsHorizontal(std::uint8_t roadDirection) {
    return roadDirection == kRoadDirectionEast || roadDirection == kRoadDirectionWest;
}

RoadAxis AxisForDirection(std::uint8_t roadDirection) {
    return DirectionIsHorizontal(roadDirection) ? RoadAxis::Horizontal : RoadAxis::Vertical;
}

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
    int count = 0;
    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        if ((directionMask & kCardinalDirections[directionIndex]) != 0) {
            ++count;
        }
    }

    return count;
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

bool LaneModeForPathing(const RoadLanePlacement& lanePlacement, TransportMode& mode) {
    if (lanePlacement.laneType == RoadLaneTypeId::Car) {
        mode = TransportMode::Car;
        return true;
    }
    if (lanePlacement.laneType == RoadLaneTypeId::Pedestrian) {
        mode = TransportMode::Pedestrian;
        return true;
    }

    return false;
}

std::uint16_t TraversalCostForLane(const RoadLanePlacement& lanePlacement) {
    RoadTemplateElement costElement;
    costElement.laneType = lanePlacement.laneType;
    costElement.surface = lanePlacement.surface;
    costElement.laneRole = lanePlacement.role;
    RoadLane costLane(costElement, lanePlacement.laneIndex);
    return costLane.traversalCost(lanePlacement.family);
}

std::uint16_t FullLaneCapacityForLane(const RoadLanePlacement& lanePlacement) {
    if (lanePlacement.laneType == RoadLaneTypeId::Pedestrian) {
        return 1200u;
    }
    if (lanePlacement.laneType == RoadLaneTypeId::Car) {
        return 100u;
    }

    return 0u;
}

std::uint16_t CapacityContributionForLane(const RoadLanePlacement& lanePlacement) {
    const float laneSpan = std::max(0.0f, std::min(1.0f, lanePlacement.sideMax) - std::max(0.0f, lanePlacement.sideMin));
    return static_cast<std::uint16_t>(std::max(1.0f, std::floor(static_cast<float>(FullLaneCapacityForLane(lanePlacement)) * laneSpan + 0.5f)));
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
      trafficOverlayRevision_(0),
      nextRoadStrokeId_(1) {
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
    strokes_.clear();
    tileErasures_.clear();
    revision_ = 0;
    trafficOverlayRevision_ = 0;
    nextRoadStrokeId_ = 1;
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
    strokes_.clear();
    tileErasures_.clear();
    revision_ = 0;
    trafficOverlayRevision_ = 0;
    nextRoadStrokeId_ = 1;
}

bool TransportNetwork::placeRoadStroke(const RoadStrokeCommand& roadStrokeCommand, const std::vector<int>& lotOccupancy, int invalidLotId) {
    if (roadStrokeCommand.operation != RoadStrokeOperation::Place || roadStrokeCommand.family == RoadFamily::None) {
        return false;
    }

    RoadTemplate roadTemplate = roadStrokeCommand.roadTemplate;
    roadTemplate.family = roadStrokeCommand.family;
    roadTemplate.layer = roadStrokeCommand.layer;
    if (roadTemplate.elements.empty()) {
        roadTemplate = Road::makeTemplate(roadStrokeCommand.family, roadStrokeCommand.layer, 1, RoadTrafficSide::RightHand, RoadDirectionMode::TwoWay);
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

    const std::uint32_t strokeId = nextRoadStrokeId_++;
    std::size_t placementIndex = 0;
    for (; placementIndex < placements.size(); ++placementIndex) {
        placements[placementIndex].lanePlacement.strokeId = strokeId;
    }

    bool madeChange = false;
    if (!validateAndApplyPlacements(roadStrokeCommand.layer, placements, lotOccupancy, invalidLotId, madeChange)) {
        return false;
    }

    if (!madeChange) {
        return true;
    }

    TransportStrokeSaveState strokeSaveState;
    strokeSaveState.strokeId = strokeId;
    strokeSaveState.startTile = roadStrokeCommand.startTile;
    strokeSaveState.cornerTile = roadStrokeCommand.cornerTile;
    strokeSaveState.endTile = roadStrokeCommand.endTile;
    strokeSaveState.family = roadStrokeCommand.family;
    strokeSaveState.layer = roadStrokeCommand.layer;
    strokeSaveState.laneCount = road.templateData().laneCount;
    strokeSaveState.trafficSide = road.templateData().trafficSide;
    strokeSaveState.directionMode = road.templateData().directionMode;
    strokes_.push_back(strokeSaveState);

    tileErasures_.erase(
        std::remove_if(
            tileErasures_.begin(),
            tileErasures_.end(),
            [&](const TransportTileEraseSaveState& erasure) {
                if (erasure.layer != roadStrokeCommand.layer) {
                    return false;
                }

                std::size_t placementIndex = 0;
                for (; placementIndex < placements.size(); ++placementIndex) {
                    if (placements[placementIndex].tileIndex == erasure.tileIndex) {
                        return true;
                    }
                }

                return false;
            }),
        tileErasures_.end());

    mergeReplayStrokeIds(roadStrokeCommand.layer, placements);

    std::vector<int> dirtyTileIndices;
    markDirtyNeighborhood(placements, dirtyTileIndices);
    expandDirtyRoadDependencies(roadStrokeCommand.layer, dirtyTileIndices);
    refreshDerivedLaneActivity(roadStrokeCommand.layer, dirtyTileIndices);

    std::size_t dirtyIndex = 0;
    for (; dirtyIndex < dirtyTileIndices.size(); ++dirtyIndex) {
        const int dirtyTileIndex = dirtyTileIndices[dirtyIndex];
        const int tileY = dirtyTileIndex / width_;
        const int tileX = dirtyTileIndex - (tileY * width_);
        resolveDirtyTile(roadStrokeCommand.layer, tileX, tileY);
    }

    bumpDirtyChunkRevisions(roadStrokeCommand.layer, dirtyTileIndices);
    rebuildCostMapAndTrafficOverlay();
    ++revision_;
    return true;
}

bool TransportNetwork::canPlaceRoadStroke(const RoadStrokeCommand& roadStrokeCommand, const std::vector<int>& lotOccupancy, int invalidLotId) const {
    if (roadStrokeCommand.operation != RoadStrokeOperation::Place || roadStrokeCommand.family == RoadFamily::None) {
        return false;
    }

    RoadTemplate roadTemplate = roadStrokeCommand.roadTemplate;
    roadTemplate.family = roadStrokeCommand.family;
    roadTemplate.layer = roadStrokeCommand.layer;
    if (roadTemplate.elements.empty()) {
        roadTemplate = Road::makeTemplate(roadStrokeCommand.family, roadStrokeCommand.layer, 1, RoadTrafficSide::RightHand, RoadDirectionMode::TwoWay);
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

bool TransportNetwork::removeRoadAtTile(int tileX, int tileY) {
    if (!isTileInsideMap(tileX, tileY)) {
        return false;
    }

    bool removedAnyRoad = false;
    std::size_t layerIndex = 0;
    for (; layerIndex < layerCount(); ++layerIndex) {
        const TransportLayerId layer = static_cast<TransportLayerId>(layerIndex);
        std::vector<int> removalTileIndices;
        collectRoadRemovalFootprint(layer, tileX, tileY, removalTileIndices);
        if (removalTileIndices.empty()) {
            continue;
        }

        std::sort(removalTileIndices.begin(), removalTileIndices.end());
        removalTileIndices.erase(std::unique(removalTileIndices.begin(), removalTileIndices.end()), removalTileIndices.end());

        std::size_t removalIndex = 0;
        for (; removalIndex < removalTileIndices.size(); ++removalIndex) {
            const int removalTileIndex = removalTileIndices[removalIndex];
            const std::size_t slot = slotIndex(layer, removalTileIndex, totalTileCount_);
            if (slot < transportTiles_.size() && !transportTiles_[slot].lanes().empty()) {
                transportTiles_[slot].clear();
                bool alreadySaved = false;
                std::size_t erasureIndex = 0;
                for (; erasureIndex < tileErasures_.size(); ++erasureIndex) {
                    if (tileErasures_[erasureIndex].layer == layer &&
                        tileErasures_[erasureIndex].tileIndex == removalTileIndex) {
                        alreadySaved = true;
                        break;
                    }
                }
                if (!alreadySaved) {
                    TransportTileEraseSaveState erasure;
                    erasure.layer = layer;
                    erasure.tileIndex = removalTileIndex;
                    tileErasures_.push_back(erasure);
                }
                removedAnyRoad = true;
            }
        }

        std::vector<int> dirtyTileIndices;
        markDirtyTileNeighborhood(removalTileIndices, dirtyTileIndices);
        expandDirtyRoadDependencies(layer, dirtyTileIndices);
        refreshDerivedLaneActivity(layer, dirtyTileIndices);

        std::size_t dirtyIndex = 0;
        for (; dirtyIndex < dirtyTileIndices.size(); ++dirtyIndex) {
            const int dirtyTileIndex = dirtyTileIndices[dirtyIndex];
            const int dirtyTileY = dirtyTileIndex / width_;
            const int dirtyTileX = dirtyTileIndex - (dirtyTileY * width_);
            resolveDirtyTile(layer, dirtyTileX, dirtyTileY);
        }

        bumpDirtyChunkRevisions(layer, dirtyTileIndices);
    }

    if (!removedAnyRoad) {
        return false;
    }

    rebuildCostMapAndTrafficOverlay();
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

void TransportNetwork::beginTrafficAssignmentFromZero() {
    costMap_.beginNextLoadFromZero();
}

void TransportNetwork::applyTrafficPathLoad(const TransportPathResult& pathResult, std::uint16_t demand, bool addLoad) {
    costMap_.applyPathLoad(pathResult, demand, addLoad);
}

void TransportNetwork::commitTrafficAssignment() {
    costMap_.commitNextLoad();
    refreshTrafficOverlayState();
    bumpAllTrafficOverlayChunkRevisions();
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
    saveState.nextRoadStrokeId = nextRoadStrokeId_;
    saveState.strokes = strokes_;
    saveState.erasures = tileErasures_;

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
    nextRoadStrokeId_ = std::max(1u, saveState.nextRoadStrokeId);

    if (!saveState.strokes.empty()) {
        strokes_ = saveState.strokes;
        std::size_t strokeIndex = 0;
        for (; strokeIndex < strokes_.size(); ++strokeIndex) {
            const TransportStrokeSaveState& stroke = strokes_[strokeIndex];
            RoadTemplate roadTemplate = Road::makeTemplate(stroke.family, stroke.layer, stroke.laneCount, stroke.trafficSide, stroke.directionMode);
            Road road(roadTemplate);
            std::vector<RoadTilePlacement> placements;
            placements.reserve(4096);
            if (!road.appendStrokePlacements(stroke.startTile, stroke.cornerTile, stroke.endTile, width_, height_, placements)) {
                continue;
            }

            std::size_t placementIndex = 0;
            for (; placementIndex < placements.size(); ++placementIndex) {
                RoadLanePlacement lanePlacement = placements[placementIndex].lanePlacement;
                lanePlacement.strokeId = stroke.strokeId;
                const std::size_t slot = slotIndex(stroke.layer, placements[placementIndex].tileIndex, totalTileCount_);
                if (slot < transportTiles_.size()) {
                    transportTiles_[slot].tryAddLane(lanePlacement);
                }
            }

            nextRoadStrokeId_ = std::max(nextRoadStrokeId_, stroke.strokeId + 1u);
        }
    } else {
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
    }

    tileErasures_ = saveState.erasures;
    std::size_t erasureIndex = 0;
    for (; erasureIndex < tileErasures_.size(); ++erasureIndex) {
        const TransportTileEraseSaveState& erasure = tileErasures_[erasureIndex];
        if (erasure.tileIndex < 0 || erasure.tileIndex >= static_cast<int>(totalTileCount_)) {
            continue;
        }

        const std::size_t slot = slotIndex(erasure.layer, erasure.tileIndex, totalTileCount_);
        if (slot < transportTiles_.size()) {
            transportTiles_[slot].clear();
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
        refreshDerivedLaneActivity(layer, dirtyTileIndices);

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

bool TransportNetwork::mergeReplayStrokeIds(TransportLayerId layer, const std::vector<RoadTilePlacement>& placements) {
    bool changed = false;
    std::size_t placementIndex = 0;
    for (; placementIndex < placements.size(); ++placementIndex) {
        const RoadTilePlacement& placement = placements[placementIndex];
        const TransportTile* tile = tileAt(layer, placement.tileX, placement.tileY);
        if (tile == 0) {
            continue;
        }

        const std::vector<RoadLanePlacement>& lanes = tile->lanes();
        std::size_t laneIndex = 0;
        for (; laneIndex < lanes.size(); ++laneIndex) {
            if (lanes[laneIndex].isExactReplayOf(placement.lanePlacement) &&
                lanes[laneIndex].strokeId != placement.lanePlacement.strokeId &&
                mergeConnectedReplayStrokeId(layer, placement.lanePlacement, lanes[laneIndex].strokeId)) {
                changed = true;
            }
        }
    }

    return changed;
}

bool TransportNetwork::mergeConnectedReplayStrokeId(TransportLayerId layer, const RoadLanePlacement& lanePlacement, std::uint32_t oldStrokeId) {
    if (oldStrokeId == 0 || oldStrokeId == lanePlacement.strokeId) {
        return false;
    }

    bool changed = false;
    std::vector<int> pendingTileIndices;
    std::vector<bool> visited(totalTileCount_, false);
    pendingTileIndices.push_back(lanePlacement.tileIndex);

    while (!pendingTileIndices.empty()) {
        const int currentTileIndex = pendingTileIndices.back();
        pendingTileIndices.pop_back();
        if (currentTileIndex < 0 || currentTileIndex >= static_cast<int>(totalTileCount_)) {
            continue;
        }

        if (visited[static_cast<std::size_t>(currentTileIndex)]) {
            continue;
        }
        visited[static_cast<std::size_t>(currentTileIndex)] = true;

        const int tileY = currentTileIndex / width_;
        const int tileX = currentTileIndex - (tileY * width_);
        TransportTile* tile = tileAt(layer, tileX, tileY);
        if (tile == 0) {
            continue;
        }

        bool mergedLaneOnTile = false;
        std::vector<RoadLanePlacement>& lanes = tile->lanesForMutation();
        std::size_t laneIndex = 0;
        for (; laneIndex < lanes.size(); ++laneIndex) {
            RoadLanePlacement& lane = lanes[laneIndex];
            if (lane.strokeId == oldStrokeId) {
                lane.strokeId = lanePlacement.strokeId;
                mergedLaneOnTile = true;
                changed = true;
            }
        }

        if (!mergedLaneOnTile) {
            continue;
        }

        // A replayed arm of an L-corner should migrate the whole old stroke component,
        // including the perpendicular half of the bend, so later missing arms can form a real intersection.
        std::size_t directionIndex = 0;
        for (; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
            const std::uint8_t direction = kCardinalDirections[directionIndex];
            const int neighborTileX = tileX + DeltaXForDirection(direction);
            const int neighborTileY = tileY + DeltaYForDirection(direction);
            if (isTileInsideMap(neighborTileX, neighborTileY)) {
                pendingTileIndices.push_back(tileIndex(neighborTileX, neighborTileY));
            }
        }
    }

    return changed;
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
    std::uint8_t arrowTravelMask = 0;
    std::uint8_t sidewalkEdges = 0;
    std::uint8_t crosswalkEdges = 0;
    std::uint8_t sameDirectionDividerEdges = 0;
    std::uint8_t opposingDirectionDividerEdges = 0;
    const std::vector<RoadLanePlacement>& lanes = tile.lanes();
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes[laneIndex];
        if (!lane.active) {
            continue;
        }

        if (lane.isSeparator()) {
            opposingDirectionDividerEdges |= separatorDividerMaskForLane(lane);
            continue;
        }

        resolvedCell.laneTypeMask |= LaneTypeMaskFor(lane.laneType);
        resolvedCell.travelMask |= lane.laneTravelMask;
        resolvedCell.surfaceMask |= SurfaceMaskFor(lane.surface);
        resolvedCell.laneCount = static_cast<std::uint8_t>(std::min(255, static_cast<int>(resolvedCell.laneCount) + 1));
        arrowTravelMask |= lane.arrowTravelMask;

        const std::size_t laneTypeIndex = static_cast<std::size_t>(lane.laneType);
        if (laneTypeIndex < resolvedCell.laneTypeCosts.size()) {
            RoadTemplateElement costElement;
            costElement.laneType = lane.laneType;
            RoadLane costLane(costElement, lane.laneIndex);
            resolvedCell.laneTypeCosts[laneTypeIndex] = costLane.traversalCost(family);
        }

        if (lane.isPedestrian() && lane.surface == RoadLaneSurface::Sidewalk) {
            const std::uint8_t laneGraphicMask = pedestrianLaneGraphicMask(layer, tileX, tileY, lane);
            const std::uint8_t laneCrosswalkMask = pedestrianLaneCrosswalkMask(layer, tileX, tileY, lane, tile, laneGraphicMask);
            if (laneCrosswalkMask != 0) {
                crosswalkEdges |= laneCrosswalkMask;
                resolvedCell.surfaceMask |= kRoadSurfaceCrosswalk;
            }
            sidewalkEdges |= static_cast<std::uint8_t>(laneGraphicMask & ~laneCrosswalkMask);
        }

        sameDirectionDividerEdges |= lane.sameDirectionDividerMask;
        opposingDirectionDividerEdges |= lane.opposingDirectionDividerMask;
    }

    const std::uint8_t exitMask = buildExitMask(layer, tileX, tileY, tile);
    const std::uint8_t junctionMask = buildJunctionMask(layer, tileX, tileY, tile, exitMask);
    const RoadRenderVariant renderVariant = chooseRenderVariantForTile(tile, junctionMask);
    const std::uint8_t baseGlyphJunctionMask = baseGlyphJunctionMaskForTile(tile, renderVariant, junctionMask);
    const std::uint8_t turnArrowIntentMask = buildTurnArrowIntentMask(layer, tileX, tileY, tile);
    const bool isCarIntersection = tile.hasCarAxis(RoadAxis::Horizontal) &&
        tile.hasCarAxis(RoadAxis::Vertical) &&
        CountCardinalDirections(junctionMask) >= 3;
    if (isCarIntersection) {
        sameDirectionDividerEdges = 0;
        opposingDirectionDividerEdges = 0;
    }

    resolvedCell.family = static_cast<std::uint8_t>(family);
    resolvedCell.exitMask = exitMask;
    resolvedCell.junctionMask = junctionMask;
    resolvedCell.renderVariant = static_cast<std::uint8_t>(renderVariant);
    resolvedCell.baseGlyph = static_cast<std::uint8_t>(ChooseBaseGlyph(family, renderVariant, baseGlyphJunctionMask));
    const RoadArrowGlyph turnArrowGlyph = ChooseTurnArrowGlyph(turnArrowIntentMask);
    const RoadArrowGlyph debugArrowGlyph = ChooseArrowGlyph(arrowTravelMask);
    if (turnArrowGlyph != RoadArrowGlyph::None) {
        resolvedCell.arrowGlyph = static_cast<std::uint8_t>(turnArrowGlyph);
    } else if (debugArrowGlyph != RoadArrowGlyph::None) {
        resolvedCell.arrowGlyph = static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(debugArrowGlyph) | kRoadArrowDebugFlag);
    }
    resolvedCell.surfaceEdgeMask = PackLaneGraphicMask(sidewalkEdges, crosswalkEdges);
    resolvedCell.dividerMask = PackDividerMask(sameDirectionDividerEdges, opposingDirectionDividerEdges);

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
                    if (lanes[laneIndex].active) {
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

void TransportNetwork::addLaneToCostMap(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) {
    TransportMode mode = TransportMode::Car;
    if (!LaneModeForPathing(lanePlacement, mode)) {
        return;
    }

    const std::uint16_t traversalCost = TraversalCostForLane(lanePlacement);
    const std::uint16_t capacity = CapacityContributionForLane(lanePlacement);
    const TransportTile* laneTile = tileAt(layer, tileX, tileY);
    if (laneTile == 0) {
        return;
    }

    std::uint8_t movementMask = 0;
    const bool carIntersectionBody = lanePlacement.isCar() &&
        tileIsIntersectionGroupBody(layer, tileX, tileY, lanePlacement.family);
    if (lanePlacement.isCar()) {
        const std::uint8_t exitMask = buildExitMask(layer, tileX, tileY, *laneTile);
        movementMask = buildJunctionMask(layer, tileX, tileY, *laneTile, exitMask);
    }

    std::size_t directionIndex = 0;
    for (; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        if (lanePlacement.isCar()) {
            if (carIntersectionBody) {
                if ((movementMask & direction) == 0 ||
                    !tileHasAnyCarBody(layer, tileX + DeltaXForDirection(direction), tileY + DeltaYForDirection(direction), lanePlacement.family)) {
                    continue;
                }
                costMap_.addDirectionalCost(layer, mode, lanePlacement.tileIndex, direction, traversalCost, capacity);
            } else if (lanePlacement.hasTravelDirection(direction)) {
                if (tileHasCarBodyAxis(layer, tileX + DeltaXForDirection(direction), tileY + DeltaYForDirection(direction), lanePlacement.axis, lanePlacement.family)) {
                    costMap_.addDirectionalCost(layer, mode, lanePlacement.tileIndex, direction, traversalCost, capacity);
                } else {
                    const std::uint8_t uTurnDirection = deadEndUTurnDirectionForLane(layer, tileX, tileY, lanePlacement, direction);
                    if (uTurnDirection != 0) {
                        costMap_.addDirectionalCost(layer, mode, lanePlacement.tileIndex, uTurnDirection, traversalCost, capacity);
                    }
                }
            }
        } else {
            if (!lanePlacement.hasTravelDirection(direction)) {
                continue;
            }
            if (hasCompatibleNeighborLane(layer, tileX, tileY, lanePlacement, direction, false)) {
                costMap_.addDirectionalCost(layer, mode, lanePlacement.tileIndex, direction, traversalCost, capacity);
            } else {
                const std::uint8_t uTurnDirection = deadEndUTurnDirectionForLane(layer, tileX, tileY, lanePlacement, direction);
                if (uTurnDirection != 0) {
                    costMap_.addDirectionalCost(layer, mode, lanePlacement.tileIndex, uTurnDirection, traversalCost, capacity);
                }
            }
        }
    }

    if (layer == TransportLayerId::Ground &&
        lanePlacement.family == RoadFamily::LocalStreet &&
        lanePlacement.isPedestrian() &&
        lanePlacement.sidewalkEdgeMask != 0u) {
        costMap_.addBuildingAccess(layer, TransportMode::Pedestrian, lanePlacement.tileIndex, lanePlacement.sidewalkEdgeMask);
        costMap_.addBuildingAccess(layer, TransportMode::Car, lanePlacement.tileIndex, lanePlacement.sidewalkEdgeMask);
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
            expandedTileIndices.push_back(dirtyTileIndex);
        }
    }

    std::size_t readIndex = 0;
    for (; readIndex < expandedTileIndices.size(); ++readIndex) {
        const int currentTileIndex = expandedTileIndices[readIndex];
        const int currentTileY = currentTileIndex / width_;
        const int currentTileX = currentTileIndex - (currentTileY * width_);

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

bool TransportNetwork::refreshDerivedLaneActivity(TransportLayerId layer, const std::vector<int>& dirtyTileIndices) {
    bool removedAnyLane = false;
    bool removedThisPass = true;
    int passIndex = 0;
    const int maximumPassCount = static_cast<int>(std::max<std::size_t>(dirtyTileIndices.size(), 1u));
    for (; removedThisPass && passIndex < maximumPassCount; ++passIndex) {
        removedThisPass = false;
        std::size_t dirtyIndex = 0;
        for (; dirtyIndex < dirtyTileIndices.size(); ++dirtyIndex) {
            const int dirtyTileIndex = dirtyTileIndices[dirtyIndex];
            const int tileY = dirtyTileIndex / width_;
            const int tileX = dirtyTileIndex - (tileY * width_);
            TransportTile* tile = tileAt(layer, tileX, tileY);
            if (tile == 0 || tile->empty()) {
                continue;
            }

            std::vector<RoadLanePlacement>& lanes = tile->lanesForMutation();
            std::size_t laneIndex = 0;
            while (laneIndex < lanes.size()) {
                const RoadLanePlacement lane = lanes[laneIndex];
                if (lane.isPedestrian()) {
                    const bool shouldBeActive = pedestrianLaneShouldBeActive(layer, tileX, tileY, lane);
                    if (lanes[laneIndex].active != shouldBeActive) {
                        lanes[laneIndex].active = shouldBeActive;
                        removedThisPass = true;
                        removedAnyLane = true;
                    }
                } else if (lane.isSeparator()) {
                    const bool shouldBeActive = separatorLaneShouldBeActive(layer, tileX, tileY, lane);
                    if (lanes[laneIndex].active != shouldBeActive) {
                        lanes[laneIndex].active = shouldBeActive;
                        removedThisPass = true;
                        removedAnyLane = true;
                    }
                } else if (!lane.active) {
                    lanes[laneIndex].active = true;
                    removedThisPass = true;
                    removedAnyLane = true;
                }

                ++laneIndex;
            }
        }
    }

    return removedAnyLane;
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

void TransportNetwork::collectRoadRemovalFootprint(TransportLayerId layer, int tileX, int tileY, std::vector<int>& removalTileIndices) const {
    const TransportTile* tile = tileAt(layer, tileX, tileY);
    if (tile == 0 || tile->lanes().empty()) {
        return;
    }

    const std::vector<RoadLanePlacement>& lanes = tile->lanes();
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        if (lanes[laneIndex].active) {
            collectRoadSliceFootprint(layer, tileX, tileY, lanes[laneIndex], removalTileIndices);
        }
    }

    if (removalTileIndices.empty()) {
        removalTileIndices.push_back(tileIndex(tileX, tileY));
    }
}

void TransportNetwork::collectRoadSliceFootprint(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& referenceLane, std::vector<int>& removalTileIndices) const {
    if (referenceLane.axis == RoadAxis::None) {
        return;
    }

    int scanTileX = tileX;
    int scanTileY = tileY;
    for (; isTileInsideMap(scanTileX, scanTileY) && tileHasMatchingRoadSliceLane(layer, scanTileX, scanTileY, referenceLane);) {
        removalTileIndices.push_back(tileIndex(scanTileX, scanTileY));
        if (referenceLane.axis == RoadAxis::Horizontal) {
            --scanTileY;
        } else {
            --scanTileX;
        }
    }

    scanTileX = referenceLane.axis == RoadAxis::Horizontal ? tileX : tileX + 1;
    scanTileY = referenceLane.axis == RoadAxis::Horizontal ? tileY + 1 : tileY;
    for (; isTileInsideMap(scanTileX, scanTileY) && tileHasMatchingRoadSliceLane(layer, scanTileX, scanTileY, referenceLane);) {
        removalTileIndices.push_back(tileIndex(scanTileX, scanTileY));
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

        if (referenceLane.strokeId != 0 && lane.strokeId != referenceLane.strokeId) {
            continue;
        }

        return true;
    }

    return false;
}

bool TransportNetwork::hasCompatibleNeighborLane(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection, bool includeInactiveLanes) const {
    const TransportTile* currentTile = tileAt(layer, tileX, tileY);
    const TransportTile* neighborTile = tileAt(layer, tileX + DeltaXForDirection(roadDirection), tileY + DeltaYForDirection(roadDirection));
    if (currentTile == 0 || neighborTile == 0 || !neighborTile->hasCompatibleLane(lanePlacement, roadDirection, includeInactiveLanes)) {
        return false;
    }

    if (laneConnectionRequiresSameStroke(*currentTile, *neighborTile, lanePlacement)) {
        return neighborTile->hasMatchingLaneBodyFromStroke(lanePlacement, includeInactiveLanes);
    }

    return true;
}

bool TransportNetwork::hasCompatibleCarNeighborLane(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection) const {
    return hasCompatibleNeighborLane(layer, tileX, tileY, lanePlacement, roadDirection, false);
}

bool TransportNetwork::hasNeighborLaneBody(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection) const {
    const TransportTile* currentTile = tileAt(layer, tileX, tileY);
    const TransportTile* neighborTile = tileAt(layer, tileX + DeltaXForDirection(roadDirection), tileY + DeltaYForDirection(roadDirection));
    if (currentTile == 0 || neighborTile == 0 || !neighborTile->hasMatchingLaneBody(lanePlacement)) {
        return false;
    }

    if (laneConnectionRequiresSameStroke(*currentTile, *neighborTile, lanePlacement)) {
        return neighborTile->hasMatchingLaneBodyFromStroke(lanePlacement, false);
    }

    return true;
}

bool TransportNetwork::tileHasCarBodyAxis(TransportLayerId layer, int tileX, int tileY, RoadAxis axis, RoadFamily family) const {
    const TransportTile* tile = tileAt(layer, tileX, tileY);
    if (tile == 0 || tile->family() != family) {
        return false;
    }

    const std::vector<RoadLanePlacement>& lanes = tile->lanes();
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        if (lanes[laneIndex].active && lanes[laneIndex].isCar() && lanes[laneIndex].axis == axis) {
            return true;
        }
    }

    return false;
}

bool TransportNetwork::tileHasAnyCarBody(TransportLayerId layer, int tileX, int tileY, RoadFamily family) const {
    return tileHasCarBodyAxis(layer, tileX, tileY, RoadAxis::Horizontal, family) ||
        tileHasCarBodyAxis(layer, tileX, tileY, RoadAxis::Vertical, family);
}

bool TransportNetwork::tileIsIntersectionGroupBody(TransportLayerId layer, int tileX, int tileY, RoadFamily family) const {
    return tileHasCarBodyAxis(layer, tileX, tileY, RoadAxis::Horizontal, family) &&
        tileHasCarBodyAxis(layer, tileX, tileY, RoadAxis::Vertical, family);
}

bool TransportNetwork::roadAxisHasTerminalEnd(TransportLayerId layer, int tileX, int tileY, RoadAxis axis, RoadFamily family) const {
    if (axis == RoadAxis::None ||
        !tileHasCarBodyAxis(layer, tileX, tileY, axis, family)) {
        return false;
    }

    const std::uint8_t firstDirection = axis == RoadAxis::Horizontal ? kRoadDirectionEast : kRoadDirectionNorth;
    const std::uint8_t secondDirection = OppositeCardinal(firstDirection);
    return !tileHasCarBodyAxis(layer, tileX + DeltaXForDirection(firstDirection), tileY + DeltaYForDirection(firstDirection), axis, family) ||
        !tileHasCarBodyAxis(layer, tileX + DeltaXForDirection(secondDirection), tileY + DeltaYForDirection(secondDirection), axis, family);
}

bool TransportNetwork::separatorLaneShouldBeActive(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const {
    if (!lanePlacement.isSeparator() || lanePlacement.separatorStyle == RoadSeparatorStyle::None) {
        return false;
    }

    return !tileIsIntersectionGroupBody(layer, tileX, tileY, lanePlacement.family) &&
        !roadAxisHasTerminalEnd(layer, tileX, tileY, lanePlacement.axis, lanePlacement.family);
}

std::uint8_t TransportNetwork::buildIntersectionGroupApproachMask(TransportLayerId layer, int tileX, int tileY, RoadFamily family) const {
    if (!tileIsIntersectionGroupBody(layer, tileX, tileY, family)) {
        return 0;
    }

    std::uint8_t approachMask = 0;
    std::vector<int> pendingTileIndices;
    std::vector<bool> visited(totalTileCount_, false);
    const int startTileIndex = tileIndex(tileX, tileY);
    pendingTileIndices.push_back(startTileIndex);
    visited[static_cast<std::size_t>(startTileIndex)] = true;

    std::size_t readIndex = 0;
    for (; readIndex < pendingTileIndices.size(); ++readIndex) {
        const int currentTileIndex = pendingTileIndices[readIndex];
        const int currentTileY = currentTileIndex / width_;
        const int currentTileX = currentTileIndex - (currentTileY * width_);

        std::size_t directionIndex = 0;
        for (; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
            const std::uint8_t direction = kCardinalDirections[directionIndex];
            const int neighborTileX = currentTileX + DeltaXForDirection(direction);
            const int neighborTileY = currentTileY + DeltaYForDirection(direction);
            if (!isTileInsideMap(neighborTileX, neighborTileY)) {
                continue;
            }

            const int neighborTileIndex = tileIndex(neighborTileX, neighborTileY);
            if (tileIsIntersectionGroupBody(layer, neighborTileX, neighborTileY, family)) {
                if (!visited[static_cast<std::size_t>(neighborTileIndex)]) {
                    visited[static_cast<std::size_t>(neighborTileIndex)] = true;
                    pendingTileIndices.push_back(neighborTileIndex);
                }
                continue;
            }

            if (tileHasCarBodyAxis(layer, neighborTileX, neighborTileY, AxisForDirection(direction), family)) {
                approachMask |= direction;
            }
        }
    }

    return approachMask;
}

std::uint8_t TransportNetwork::separatorDividerMaskForLane(const RoadLanePlacement& lanePlacement) const {
    if (!lanePlacement.isSeparator() || lanePlacement.separatorStyle == RoadSeparatorStyle::None) {
        return 0;
    }

    const float sideMid = (lanePlacement.sideMin + lanePlacement.sideMax) * 0.5f;
    if (lanePlacement.axis == RoadAxis::Horizontal) {
        return sideMid < 0.5f ? kRoadDirectionNorth : kRoadDirectionSouth;
    }
    if (lanePlacement.axis == RoadAxis::Vertical) {
        return sideMid < 0.5f ? kRoadDirectionWest : kRoadDirectionEast;
    }
    return 0;
}

bool TransportNetwork::laneConnectionRequiresSameStroke(const TransportTile& currentTile, const TransportTile& neighborTile, const RoadLanePlacement& lanePlacement) const {
    const RoadAxis crossingAxis = lanePlacement.axis == RoadAxis::Horizontal ? RoadAxis::Vertical : RoadAxis::Horizontal;
    return currentTile.hasCarAxis(crossingAxis) && neighborTile.hasCarAxis(crossingAxis);
}

std::uint8_t TransportNetwork::deadEndUTurnDirectionForLane(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection) const {
    if (roadDirection == 0 ||
        lanePlacement.axis == RoadAxis::None ||
        lanePlacement.axis != AxisForDirection(roadDirection)) {
        return 0;
    }

    const TransportTile* currentTile = tileAt(layer, tileX, tileY);
    if (currentTile == 0 || currentTile->empty()) {
        return 0;
    }

    const RoadAxis crossingAxis = lanePlacement.axis == RoadAxis::Horizontal ? RoadAxis::Vertical : RoadAxis::Horizontal;
    if (currentTile->hasCarAxis(crossingAxis)) {
        return 0;
    }

    const std::uint8_t crossDirections[] = {
        lanePlacement.axis == RoadAxis::Horizontal ? kRoadDirectionNorth : kRoadDirectionWest,
        lanePlacement.axis == RoadAxis::Horizontal ? kRoadDirectionSouth : kRoadDirectionEast
    };
    const std::uint8_t returnDirection = OppositeCardinal(roadDirection);

    const int maximumCrossSteps = std::max(width_, height_);
    for (std::size_t crossDirectionIndex = 0; crossDirectionIndex < sizeof(crossDirections) / sizeof(crossDirections[0]); ++crossDirectionIndex) {
        const std::uint8_t crossDirection = crossDirections[crossDirectionIndex];
        int scanTileX = tileX;
        int scanTileY = tileY;
        bool firstStepHasBody = false;
        int stepIndex = 0;
        for (; stepIndex < maximumCrossSteps; ++stepIndex) {
            scanTileX += DeltaXForDirection(crossDirection);
            scanTileY += DeltaYForDirection(crossDirection);
            const TransportTile* neighborTile = tileAt(layer, scanTileX, scanTileY);
            if (neighborTile == 0 || neighborTile->family() != currentTile->family()) {
                break;
            }

            bool hasMatchingBody = false;
            const std::vector<RoadLanePlacement>& neighborLanes = neighborTile->lanes();
            for (std::size_t neighborLaneIndex = 0; neighborLaneIndex < neighborLanes.size(); ++neighborLaneIndex) {
                const RoadLanePlacement& neighborLane = neighborLanes[neighborLaneIndex];
                if (!neighborLane.active ||
                    neighborLane.family != lanePlacement.family ||
                    neighborLane.laneType != lanePlacement.laneType ||
                    neighborLane.axis != lanePlacement.axis ||
                    (lanePlacement.strokeId != 0 &&
                     neighborLane.strokeId != 0 &&
                     lanePlacement.strokeId != neighborLane.strokeId)) {
                    continue;
                }

                hasMatchingBody = true;
                if (neighborLane.laneIndex != lanePlacement.laneIndex &&
                    neighborLane.hasTravelDirection(returnDirection)) {
                    return firstStepHasBody || stepIndex == 0 ? crossDirection : 0;
                }
            }

            if (stepIndex == 0) {
                firstStepHasBody = hasMatchingBody;
            }
            if (!hasMatchingBody) {
                break;
            }
        }
    }

    return 0;
}

bool TransportNetwork::hasCarContinuationBeyondCrossing(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& carLane, std::uint8_t roadDirection) const {
    const RoadAxis crossingAxis = carLane.axis == RoadAxis::Horizontal ? RoadAxis::Vertical : RoadAxis::Horizontal;
    int currentTileX = tileX;
    int currentTileY = tileY;
    const int maximumSteps = std::max(width_, height_);
    int stepIndex = 0;
    for (; stepIndex < maximumSteps; ++stepIndex) {
        if (!hasNeighborLaneBody(layer, currentTileX, currentTileY, carLane, roadDirection)) {
            return false;
        }

        currentTileX += DeltaXForDirection(roadDirection);
        currentTileY += DeltaYForDirection(roadDirection);
        const TransportTile* currentTile = tileAt(layer, currentTileX, currentTileY);
        if (currentTile == 0) {
            return false;
        }

        if (!currentTile->hasCarAxis(crossingAxis)) {
            return true;
        }
    }

    return false;
}

bool TransportNetwork::carLaneContinuesThroughCrossing(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& carLane) const {
    const std::uint8_t firstDirection = carLane.axis == RoadAxis::Horizontal ? kRoadDirectionEast : kRoadDirectionNorth;
    const std::uint8_t secondDirection = OppositeCardinal(firstDirection);
    return hasCarContinuationBeyondCrossing(layer, tileX, tileY, carLane, firstDirection) &&
        hasCarContinuationBeyondCrossing(layer, tileX, tileY, carLane, secondDirection);
}

bool TransportNetwork::hasPedestrianThroughBothEnds(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane) const {
    const std::uint8_t firstDirection = pedestrianLane.axis == RoadAxis::Horizontal ? kRoadDirectionEast : kRoadDirectionNorth;
    const std::uint8_t secondDirection = OppositeCardinal(firstDirection);
    return hasCompatibleNeighborLane(layer, tileX, tileY, pedestrianLane, firstDirection, true) &&
        hasCompatibleNeighborLane(layer, tileX, tileY, pedestrianLane, secondDirection, true);
}

bool TransportNetwork::hasPedestrianNeighborOnEitherEnd(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane, bool includeInactiveLanes) const {
    const std::uint8_t firstDirection = pedestrianLane.axis == RoadAxis::Horizontal ? kRoadDirectionEast : kRoadDirectionNorth;
    const std::uint8_t secondDirection = OppositeCardinal(firstDirection);
    return hasCompatibleNeighborLane(layer, tileX, tileY, pedestrianLane, firstDirection, includeInactiveLanes) ||
        hasCompatibleNeighborLane(layer, tileX, tileY, pedestrianLane, secondDirection, includeInactiveLanes);
}

bool TransportNetwork::pedestrianLaneMeetsCrossingPedestrianLane(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane, bool includeInactiveLanes) const {
    const TransportTile* tile = tileAt(layer, tileX, tileY);
    if (tile == 0) {
        return false;
    }

    const std::vector<RoadLanePlacement>& lanes = tile->lanes();
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes[laneIndex];
        if ((includeInactiveLanes || lane.active) &&
            lane.isPedestrian() &&
            lane.family == pedestrianLane.family &&
            lane.laneType == pedestrianLane.laneType &&
            lane.axis != pedestrianLane.axis) {
            return true;
        }
    }

    return false;
}

bool TransportNetwork::pedestrianLaneHasEndpointConnection(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane, bool includeInactiveLanes) const {
    return hasPedestrianNeighborOnEitherEnd(layer, tileX, tileY, pedestrianLane, includeInactiveLanes) &&
        pedestrianLaneMeetsCrossingPedestrianLane(layer, tileX, tileY, pedestrianLane, includeInactiveLanes);
}

bool TransportNetwork::hasPedestrianContinuationBeyondCrossing(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane, std::uint8_t roadDirection) const {
    const RoadAxis crossingAxis = pedestrianLane.axis == RoadAxis::Horizontal ? RoadAxis::Vertical : RoadAxis::Horizontal;
    int currentTileX = tileX;
    int currentTileY = tileY;
    const int maximumSteps = std::max(width_, height_);
    int stepIndex = 0;
    for (; stepIndex < maximumSteps; ++stepIndex) {
        if (!hasCompatibleNeighborLane(layer, currentTileX, currentTileY, pedestrianLane, roadDirection, false)) {
            return false;
        }

        currentTileX += DeltaXForDirection(roadDirection);
        currentTileY += DeltaYForDirection(roadDirection);
        const TransportTile* currentTile = tileAt(layer, currentTileX, currentTileY);
        if (currentTile == 0) {
            return false;
        }

        if (!currentTile->hasCarAxis(crossingAxis)) {
            return true;
        }
    }

    return false;
}

bool TransportNetwork::pedestrianLaneContinuesThroughCrossing(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane) const {
    const std::uint8_t firstDirection = pedestrianLane.axis == RoadAxis::Horizontal ? kRoadDirectionEast : kRoadDirectionNorth;
    const std::uint8_t secondDirection = OppositeCardinal(firstDirection);
    return hasPedestrianContinuationBeyondCrossing(layer, tileX, tileY, pedestrianLane, firstDirection) &&
        hasPedestrianContinuationBeyondCrossing(layer, tileX, tileY, pedestrianLane, secondDirection);
}

bool TransportNetwork::pedestrianLaneBordersEmptyTile(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane) const {
    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        if ((pedestrianLane.sidewalkEdgeMask & direction) == 0) {
            continue;
        }

        const TransportTile* neighborTile = tileAt(layer, tileX + DeltaXForDirection(direction), tileY + DeltaYForDirection(direction));
        if (neighborTile == 0 || neighborTile->empty()) {
            return true;
        }
    }

    return false;
}

bool TransportNetwork::pedestrianLaneShouldBeActive(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane) const {
    return pedestrianLaneBordersEmptyTile(layer, tileX, tileY, pedestrianLane) ||
        hasPedestrianThroughBothEnds(layer, tileX, tileY, pedestrianLane) ||
        pedestrianLaneHasEndpointConnection(layer, tileX, tileY, pedestrianLane, true);
}

std::uint8_t TransportNetwork::pedestrianLaneGraphicMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane) const {
    std::uint8_t graphicMask = pedestrianLane.sidewalkEdgeMask;
    const std::uint8_t axisDirections[] = {
        pedestrianLane.axis == RoadAxis::Horizontal ? kRoadDirectionEast : kRoadDirectionNorth,
        pedestrianLane.axis == RoadAxis::Horizontal ? kRoadDirectionWest : kRoadDirectionSouth
    };

    for (std::size_t directionIndex = 0; directionIndex < sizeof(axisDirections) / sizeof(axisDirections[0]); ++directionIndex) {
        const std::uint8_t direction = axisDirections[directionIndex];
        if (direction == 0 || !pedestrianLane.hasTravelDirection(direction)) {
            continue;
        }

        if (!hasCompatibleNeighborLane(layer, tileX, tileY, pedestrianLane, direction, false) &&
            !pedestrianLaneMeetsCrossingPedestrianLane(layer, tileX, tileY, pedestrianLane, false)) {
            graphicMask |= direction;
        }
    }

    return static_cast<std::uint8_t>(graphicMask & kRoadSurfaceSidewalkEdgeMask);
}

std::uint8_t TransportNetwork::pedestrianLaneCrosswalkMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane, const TransportTile& tile, std::uint8_t pedestrianGraphicMask) const {
    if (!pedestrianLane.isPedestrian()) {
        return 0;
    }

    pedestrianGraphicMask = static_cast<std::uint8_t>(pedestrianGraphicMask & kRoadSurfaceSidewalkEdgeMask);
    if (pedestrianGraphicMask == 0) {
        return 0;
    }

    if (!tileIsIntersectionGroupBody(layer, tileX, tileY, tile.family())) {
        return 0;
    }

    const std::uint8_t approachMask = buildIntersectionGroupApproachMask(layer, tileX, tileY, tile.family());
    if (CountCardinalDirections(approachMask) < 3) {
        return 0;
    }

    if ((tileX == 5 && tileY == 5) || (tileX == 7 && (tileY == 7 || tileY == 8)) || (tileX == 8 && (tileY == 7 || tileY == 8))) {
        std::cout << "CWDBG " << tileX << "," << tileY
            << " axis=" << static_cast<int>(pedestrianLane.axis)
            << " lane=" << pedestrianLane.laneIndex
            << " travel=" << static_cast<int>(RoadDirectionsFromLaneIntent(pedestrianLane.laneTravelMask))
            << " graphic=" << static_cast<int>(pedestrianGraphicMask)
            << " approach=" << static_cast<int>(approachMask)
            << " through=" << pedestrianLaneContinuesThroughCrossing(layer, tileX, tileY, pedestrianLane)
            << " throughEnds=" << hasPedestrianThroughBothEnds(layer, tileX, tileY, pedestrianLane)
            << " endpoint=" << pedestrianLaneHasEndpointConnection(layer, tileX, tileY, pedestrianLane, false)
            << " bordersEmpty=" << pedestrianLaneBordersEmptyTile(layer, tileX, tileY, pedestrianLane)
            << std::endl;
    }

    return static_cast<std::uint8_t>(pedestrianGraphicMask & approachMask);
}

bool TransportNetwork::carComponentHasIntersectionBody(TransportLayerId layer, int tileX, int tileY) const {
    const TransportTile* startTile = tileAt(layer, tileX, tileY);
    if (startTile == 0 || startTile->empty() || !startTile->hasLaneType(RoadLaneTypeId::Car)) {
        return false;
    }

    std::vector<unsigned char> visited(totalTileCount_, 0);
    std::vector<int> pendingTileIndices;
    pendingTileIndices.push_back(tileIndex(tileX, tileY));

    while (!pendingTileIndices.empty()) {
        const int currentTileIndex = pendingTileIndices.back();
        pendingTileIndices.pop_back();
        if (currentTileIndex < 0 || currentTileIndex >= static_cast<int>(totalTileCount_)) {
            continue;
        }

        if (visited[static_cast<std::size_t>(currentTileIndex)] != 0u) {
            continue;
        }
        visited[static_cast<std::size_t>(currentTileIndex)] = 1u;

        const int currentTileY = currentTileIndex / width_;
        const int currentTileX = currentTileIndex - (currentTileY * width_);
        const TransportTile* currentTile = tileAt(layer, currentTileX, currentTileY);
        if (currentTile == 0 ||
            currentTile->empty() ||
            currentTile->family() != startTile->family() ||
            !currentTile->hasLaneType(RoadLaneTypeId::Car)) {
            continue;
        }

        if (currentTile->hasCarAxis(RoadAxis::Horizontal) && currentTile->hasCarAxis(RoadAxis::Vertical)) {
            return true;
        }

        for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
            const int neighborTileX = currentTileX + DeltaXForDirection(kCardinalDirections[directionIndex]);
            const int neighborTileY = currentTileY + DeltaYForDirection(kCardinalDirections[directionIndex]);
            const TransportTile* neighborTile = tileAt(layer, neighborTileX, neighborTileY);
            if (neighborTile == 0 ||
                neighborTile->empty() ||
                neighborTile->family() != startTile->family() ||
                !neighborTile->hasLaneType(RoadLaneTypeId::Car)) {
                continue;
            }

            pendingTileIndices.push_back(tileIndex(neighborTileX, neighborTileY));
        }
    }

    return false;
}

bool TransportNetwork::isSingleAxisDeadEndCapMask(const TransportTile& tile, std::uint8_t junctionMask) const {
    if (!tile.hasLaneType(RoadLaneTypeId::Car) ||
        tile.hasCarAxis(RoadAxis::Horizontal) == tile.hasCarAxis(RoadAxis::Vertical)) {
        return false;
    }

    const RoadAxis roadAxis = tile.hasCarAxis(RoadAxis::Horizontal) ? RoadAxis::Horizontal : RoadAxis::Vertical;
    const std::uint8_t roadDirectionMask = roadAxis == RoadAxis::Horizontal
        ? static_cast<std::uint8_t>(kRoadDirectionEast | kRoadDirectionWest)
        : static_cast<std::uint8_t>(kRoadDirectionNorth | kRoadDirectionSouth);
    const std::uint8_t crossDirectionMask = roadAxis == RoadAxis::Horizontal
        ? static_cast<std::uint8_t>(kRoadDirectionNorth | kRoadDirectionSouth)
        : static_cast<std::uint8_t>(kRoadDirectionEast | kRoadDirectionWest);

    return CountCardinalDirections(junctionMask) == 2 &&
        CountCardinalDirections(static_cast<std::uint8_t>(junctionMask & roadDirectionMask)) == 1 &&
        CountCardinalDirections(static_cast<std::uint8_t>(junctionMask & crossDirectionMask)) == 1;
}

RoadRenderVariant TransportNetwork::chooseRenderVariantForTile(const TransportTile& tile, std::uint8_t junctionMask) const {
    if (isSingleAxisDeadEndCapMask(tile, junctionMask)) {
        return RoadRenderVariant::DeadEnd;
    }

    return ChooseRenderVariant(junctionMask);
}

std::uint8_t TransportNetwork::baseGlyphJunctionMaskForTile(const TransportTile& tile, RoadRenderVariant renderVariant, std::uint8_t junctionMask) const {
    if (renderVariant == RoadRenderVariant::DeadEnd &&
        isSingleAxisDeadEndCapMask(tile, junctionMask)) {
        if (tile.hasCarAxis(RoadAxis::Horizontal)) {
            return static_cast<std::uint8_t>(junctionMask & (kRoadDirectionEast | kRoadDirectionWest));
        }

        return static_cast<std::uint8_t>(junctionMask & (kRoadDirectionNorth | kRoadDirectionSouth));
    }

    return junctionMask;
}

bool TransportNetwork::isSingleStrokeCarCornerTile(const TransportTile& tile, std::uint32_t& strokeId) const {
    bool foundCarLane = false;
    bool hasHorizontal = false;
    bool hasVertical = false;
    std::uint32_t foundStrokeId = 0;

    const std::vector<RoadLanePlacement>& lanes = tile.lanes();
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes[laneIndex];
        if (!lane.active || !lane.isCar()) {
            continue;
        }

        if (lane.strokeId == 0) {
            return false;
        }

        if (!foundCarLane) {
            foundStrokeId = lane.strokeId;
            foundCarLane = true;
        } else if (foundStrokeId != lane.strokeId) {
            return false;
        }

        if (lane.axis == RoadAxis::Horizontal) {
            hasHorizontal = true;
        } else if (lane.axis == RoadAxis::Vertical) {
            hasVertical = true;
        }
    }

    if (!foundCarLane || !hasHorizontal || !hasVertical) {
        return false;
    }

    strokeId = foundStrokeId;
    return true;
}

std::uint8_t TransportNetwork::chooseCurveDirectionForAxis(TransportLayerId layer, int tileX, int tileY, std::uint8_t junctionMask, RoadAxis axis, std::uint32_t strokeId) const {
    const std::uint8_t firstDirection = axis == RoadAxis::Horizontal ? kRoadDirectionEast : kRoadDirectionNorth;
    const std::uint8_t secondDirection = axis == RoadAxis::Horizontal ? kRoadDirectionWest : kRoadDirectionSouth;
    const std::uint8_t directions[] = {
        firstDirection,
        secondDirection
    };

    std::uint8_t fallbackDirection = 0;
    for (std::size_t directionIndex = 0; directionIndex < sizeof(directions) / sizeof(directions[0]); ++directionIndex) {
        const std::uint8_t direction = directions[directionIndex];
        if ((junctionMask & direction) == 0) {
            continue;
        }

        if (fallbackDirection == 0) {
            fallbackDirection = direction;
        }

        const TransportTile* neighborTile = tileAt(layer, tileX + DeltaXForDirection(direction), tileY + DeltaYForDirection(direction));
        std::uint32_t neighborStrokeId = 0;
        const bool neighborIsSameCorner = neighborTile != 0 &&
            isSingleStrokeCarCornerTile(*neighborTile, neighborStrokeId) &&
            neighborStrokeId == strokeId;
        if (!neighborIsSameCorner) {
            return direction;
        }
    }

    return fallbackDirection;
}

bool TransportNetwork::isCarIntersectionNode(TransportLayerId layer, int tileX, int tileY) const {
    const TransportTile* tile = tileAt(layer, tileX, tileY);
    if (tile == 0 || tile->empty() || !tile->hasLaneType(RoadLaneTypeId::Car)) {
        return false;
    }

    if (!tileIsIntersectionGroupBody(layer, tileX, tileY, tile->family())) {
        return false;
    }

    return CountCardinalDirections(buildIntersectionGroupApproachMask(layer, tileX, tileY, tile->family())) >= 3;
}

bool TransportNetwork::isCarIntersectionCollectionTile(TransportLayerId layer, int tileX, int tileY) const {
    const TransportTile* tile = tileAt(layer, tileX, tileY);
    if (tile == 0 || tile->empty() || !tile->hasLaneType(RoadLaneTypeId::Car)) {
        return false;
    }

    return isCarIntersectionNode(layer, tileX, tileY);
}

bool TransportNetwork::findNearbyCarIntersectionNode(TransportLayerId layer, int tileX, int tileY, int& nodeTileX, int& nodeTileY) const {
    if (isCarIntersectionNode(layer, tileX, tileY)) {
        nodeTileX = tileX;
        nodeTileY = tileY;
        return true;
    }

    int neighborTileY = tileY - 1;
    for (; neighborTileY <= tileY + 1; ++neighborTileY) {
        int neighborTileX = tileX - 1;
        for (; neighborTileX <= tileX + 1; ++neighborTileX) {
            if (neighborTileX == tileX && neighborTileY == tileY) {
                continue;
            }
            if (isCarIntersectionNode(layer, neighborTileX, neighborTileY)) {
                nodeTileX = neighborTileX;
                nodeTileY = neighborTileY;
                return true;
            }
        }
    }

    return false;
}

std::uint8_t TransportNetwork::buildTurnExitMaskThroughIntersection(TransportLayerId layer, int entryTileX, int entryTileY, std::uint8_t travelDirection) const {
    std::uint8_t exitMask = 0;
    int currentTileX = entryTileX;
    int currentTileY = entryTileY;
    const int maximumSteps = std::max(width_, height_);
    int stepIndex = 0;
    for (; stepIndex < maximumSteps; ++stepIndex) {
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
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes[laneIndex];
        if (!lane.active || !lane.isCar()) {
            continue;
        }

        for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
            const std::uint8_t direction = kCardinalDirections[directionIndex];
            if (!lane.hasTravelDirection(direction) ||
                !hasCompatibleCarNeighborLane(layer, tileX, tileY, lane, direction)) {
                continue;
            }

            const int candidateTileX = tileX + DeltaXForDirection(direction);
            const int candidateTileY = tileY + DeltaYForDirection(direction);
            if (!isCarIntersectionCollectionTile(layer, candidateTileX, candidateTileY)) {
                continue;
            }

            turnIntentMask |= LaneIntentFromCardinalRoadMask(buildTurnExitMaskThroughIntersection(layer, candidateTileX, candidateTileY, direction));
        }
    }

    return turnIntentMask;
}

std::uint8_t TransportNetwork::buildCarExitMask(TransportLayerId layer, int tileX, int tileY, const TransportTile& tile) const {
    std::uint8_t exitMask = 0;
    if (tileIsIntersectionGroupBody(layer, tileX, tileY, tile.family())) {
        return buildIntersectionGroupApproachMask(layer, tileX, tileY, tile.family());
    }

    const std::vector<RoadLanePlacement>& lanes = tile.lanes();
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes[laneIndex];
        if (!lane.active || !lane.isCar()) {
            continue;
        }

        for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
            const std::uint8_t direction = kCardinalDirections[directionIndex];
            if (lane.axis == AxisForDirection(direction) &&
                lane.hasTravelDirection(direction)) {
                if (tileHasCarBodyAxis(layer, tileX + DeltaXForDirection(direction), tileY + DeltaYForDirection(direction), lane.axis, tile.family())) {
                    exitMask |= direction;
                } else {
                    exitMask |= deadEndUTurnDirectionForLane(layer, tileX, tileY, lane, direction);
                }
            }
        }
    }

    return exitMask;
}

std::uint8_t TransportNetwork::buildExitMask(TransportLayerId layer, int tileX, int tileY, const TransportTile& tile) const {
    std::uint8_t exitMask = buildCarExitMask(layer, tileX, tileY, tile);
    const std::vector<RoadLanePlacement>& lanes = tile.lanes();
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes[laneIndex];
        if (!lane.active || lane.isCar() || lane.isSeparator()) {
            continue;
        }
        for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
            const std::uint8_t direction = kCardinalDirections[directionIndex];
            if (lane.axis == AxisForDirection(direction) &&
                lane.hasTravelDirection(direction) &&
                hasCompatibleNeighborLane(layer, tileX, tileY, lane, direction, false)) {
                exitMask |= direction;
            }
        }
    }

    return exitMask;
}

std::uint8_t TransportNetwork::buildJunctionMask(TransportLayerId layer, int tileX, int tileY, const TransportTile& tile, std::uint8_t exitMask) const {
    const bool hasCarLanes = tile.hasLaneType(RoadLaneTypeId::Car);
    if (!hasCarLanes) {
        return static_cast<std::uint8_t>(exitMask & (kRoadDirectionNorth | kRoadDirectionEast | kRoadDirectionSouth | kRoadDirectionWest));
    }

    if (tileIsIntersectionGroupBody(layer, tileX, tileY, tile.family())) {
        return buildIntersectionGroupApproachMask(layer, tileX, tileY, tile.family());
    }

    std::uint8_t junctionMask = 0;
    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        const RoadAxis axis = AxisForDirection(direction);
        if (tile.hasCarAxis(axis) &&
            tileHasCarBodyAxis(layer, tileX + DeltaXForDirection(direction), tileY + DeltaYForDirection(direction), axis, tile.family())) {
            junctionMask |= direction;
        }
    }

    const RoadAxis roadAxis = tile.hasCarAxis(RoadAxis::Horizontal) ? RoadAxis::Horizontal : RoadAxis::Vertical;
    if (roadAxis != RoadAxis::None &&
        tile.hasCarAxis(RoadAxis::Horizontal) != tile.hasCarAxis(RoadAxis::Vertical)) {
        const std::uint8_t roadDirections[] = {
            roadAxis == RoadAxis::Horizontal ? kRoadDirectionEast : kRoadDirectionSouth,
            roadAxis == RoadAxis::Horizontal ? kRoadDirectionWest : kRoadDirectionNorth
        };
        int longitudinalConnections = 0;
        for (std::size_t roadDirectionIndex = 0; roadDirectionIndex < sizeof(roadDirections) / sizeof(roadDirections[0]); ++roadDirectionIndex) {
            if ((junctionMask & roadDirections[roadDirectionIndex]) != 0) {
                ++longitudinalConnections;
            }
        }

        if (longitudinalConnections == 1) {
            const std::uint8_t crossDirections[] = {
                roadAxis == RoadAxis::Horizontal ? kRoadDirectionNorth : kRoadDirectionWest,
                roadAxis == RoadAxis::Horizontal ? kRoadDirectionSouth : kRoadDirectionEast
            };
            for (std::size_t crossDirectionIndex = 0; crossDirectionIndex < sizeof(crossDirections) / sizeof(crossDirections[0]); ++crossDirectionIndex) {
                const std::uint8_t crossDirection = crossDirections[crossDirectionIndex];
                if (tileHasCarBodyAxis(layer, tileX + DeltaXForDirection(crossDirection), tileY + DeltaYForDirection(crossDirection), roadAxis, tile.family())) {
                    std::uint8_t capDirection = crossDirection;
                    if (roadAxis == RoadAxis::Vertical &&
                        (junctionMask & kRoadDirectionSouth) != 0 &&
                        (junctionMask & kRoadDirectionNorth) == 0) {
                        capDirection = OppositeCardinal(crossDirection);
                    }
                    junctionMask |= capDirection;
                }
            }
        }
    }

    return junctionMask;
}
