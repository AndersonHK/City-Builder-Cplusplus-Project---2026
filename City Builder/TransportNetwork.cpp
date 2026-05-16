#include "TransportNetwork.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>

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
        return 200u;
    }

    return 0u;
}

std::uint16_t CapacityContributionForLane(const RoadLanePlacement& lanePlacement) {
    const float laneSpan = std::max(0.0f, std::min(1.0f, lanePlacement.sideMax) - std::max(0.0f, lanePlacement.sideMin));
    return static_cast<std::uint16_t>(std::max(1.0f, std::floor(static_cast<float>(FullLaneCapacityForLane(lanePlacement)) * laneSpan + 0.5f)));
}

RoadDirectionMode DirectionModeFromTemplateId(std::uint16_t templateId) {
    return static_cast<RoadDirectionMode>((templateId >> 6) & 0x3u);
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
    rebuildRoadTilesInRegion(roadStrokeCommand.layer, dirtyTileIndices);
    ensurePedestrianCapLanes(roadStrokeCommand.layer, dirtyTileIndices);
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
                addRoadTileErasure(layer, removalTileIndex);
                removedAnyRoad = true;
            }
        }

        std::vector<int> shortSegmentTileIndices;
        collectShortRoadSegmentErasures(layer, shortSegmentTileIndices);
        std::sort(shortSegmentTileIndices.begin(), shortSegmentTileIndices.end());
        shortSegmentTileIndices.erase(std::unique(shortSegmentTileIndices.begin(), shortSegmentTileIndices.end()), shortSegmentTileIndices.end());

        for (removalIndex = 0; removalIndex < shortSegmentTileIndices.size(); ++removalIndex) {
            const int removalTileIndex = shortSegmentTileIndices[removalIndex];
            if (removalTileIndex < 0 || removalTileIndex >= static_cast<int>(totalTileCount_)) {
                continue;
            }

            const std::size_t slot = slotIndex(layer, removalTileIndex, totalTileCount_);
            if (slot < transportTiles_.size()) {
                transportTiles_[slot].clear();
            }
            if (addRoadTileErasure(layer, removalTileIndex)) {
                removalTileIndices.push_back(removalTileIndex);
                removedAnyRoad = true;
            }
        }

        std::vector<int> dirtyTileIndices;
        markDirtyTileNeighborhood(removalTileIndices, dirtyTileIndices);
        expandDirtyRoadDependencies(layer, dirtyTileIndices);
        rebuildRoadTilesInRegion(layer, dirtyTileIndices);
        ensurePedestrianCapLanes(layer, dirtyTileIndices);
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

void TransportNetwork::beginTrafficAssignmentFromOldLoad() {
    costMap_.beginNextLoadFromOldLoad();
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
        ensurePedestrianCapLanes(layer, dirtyTileIndices);
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

void TransportNetwork::rebuildRoadTilesInRegion(TransportLayerId layer, const std::vector<int>& tileIndices) {
    if (tileIndices.empty()) {
        return;
    }

    std::vector<bool> rebuildTile(totalTileCount_, false);
    std::size_t tileListIndex = 0;
    for (; tileListIndex < tileIndices.size(); ++tileListIndex) {
        const int tileIndexValue = tileIndices[tileListIndex];
        if (tileIndexValue < 0 || tileIndexValue >= static_cast<int>(totalTileCount_)) {
            continue;
        }

        rebuildTile[static_cast<std::size_t>(tileIndexValue)] = true;
        transportTiles_[slotIndex(layer, tileIndexValue, totalTileCount_)].clear();
    }

    std::size_t strokeIndex = 0;
    for (; strokeIndex < strokes_.size(); ++strokeIndex) {
        const TransportStrokeSaveState& stroke = strokes_[strokeIndex];
        if (stroke.layer != layer) {
            continue;
        }

        const RoadTemplate roadTemplate = Road::makeTemplate(stroke.family, stroke.layer, stroke.laneCount, stroke.trafficSide, stroke.directionMode);
        Road road(roadTemplate);
        std::vector<RoadTilePlacement> placements;
        placements.reserve(4096);
        if (!road.appendStrokePlacements(stroke.startTile, stroke.cornerTile, stroke.endTile, width_, height_, placements)) {
            continue;
        }

        std::size_t placementIndex = 0;
        for (; placementIndex < placements.size(); ++placementIndex) {
            const int tileIndexValue = placements[placementIndex].tileIndex;
            if (tileIndexValue < 0 ||
                tileIndexValue >= static_cast<int>(totalTileCount_) ||
                !rebuildTile[static_cast<std::size_t>(tileIndexValue)] ||
                tileIsErased(layer, tileIndexValue)) {
                continue;
            }

            RoadLanePlacement lanePlacement = placements[placementIndex].lanePlacement;
            lanePlacement.strokeId = stroke.strokeId;
            transportTiles_[slotIndex(layer, tileIndexValue, totalTileCount_)].tryAddLane(lanePlacement);
        }
    }
}

bool TransportNetwork::tileIsErased(TransportLayerId layer, int tileIndexValue) const {
    std::size_t erasureIndex = 0;
    for (; erasureIndex < tileErasures_.size(); ++erasureIndex) {
        if (tileErasures_[erasureIndex].layer == layer &&
            tileErasures_[erasureIndex].tileIndex == tileIndexValue) {
            return true;
        }
    }

    return false;
}

bool TransportNetwork::addRoadTileErasure(TransportLayerId layer, int tileIndexValue) {
    if (tileIndexValue < 0 || tileIndexValue >= static_cast<int>(totalTileCount_) || tileIsErased(layer, tileIndexValue)) {
        return false;
    }

    TransportTileEraseSaveState erasure;
    erasure.layer = layer;
    erasure.tileIndex = tileIndexValue;
    tileErasures_.push_back(erasure);
    return true;
}

void TransportNetwork::collectShortRoadSegmentErasures(TransportLayerId layer, std::vector<int>& erasureTileIndices) const {
    std::size_t strokeIndex = 0;
    for (; strokeIndex < strokes_.size(); ++strokeIndex) {
        const TransportStrokeSaveState& stroke = strokes_[strokeIndex];
        if (stroke.layer != layer) {
            continue;
        }

        const RoadTemplate roadTemplate = Road::makeTemplate(stroke.family, stroke.layer, stroke.laneCount, stroke.trafficSide, stroke.directionMode);
        const int footprint = std::max(1, static_cast<int>(roadTemplate.identity.footprint));
        if (footprint <= 1) {
            continue;
        }

        Road road(roadTemplate);
        std::vector<RoadTilePlacement> placements;
        placements.reserve(4096);
        if (!road.appendStrokePlacements(stroke.startTile, stroke.cornerTile, stroke.endTile, width_, height_, placements)) {
            continue;
        }

        const RoadAxis axes[] = {
            RoadAxis::Horizontal,
            RoadAxis::Vertical
        };

        std::size_t axisIndex = 0;
        for (; axisIndex < sizeof(axes) / sizeof(axes[0]); ++axisIndex) {
            const RoadAxis axis = axes[axisIndex];
            const std::uint8_t axisMask = AxisMaskFor(axis);
            std::map<int, std::vector<int> > tileIndicesByLongitudinalCoordinate;

            std::size_t placementIndex = 0;
            for (; placementIndex < placements.size(); ++placementIndex) {
                const RoadTilePlacement& placement = placements[placementIndex];
                if ((AxisMaskFor(placement.lanePlacement.axis) & axisMask) == 0u ||
                    tileIsErased(layer, placement.tileIndex)) {
                    continue;
                }

                const int longitudinalCoordinate = axis == RoadAxis::Horizontal ? placement.tileX : placement.tileY;
                tileIndicesByLongitudinalCoordinate[longitudinalCoordinate].push_back(placement.tileIndex);
            }

            if (tileIndicesByLongitudinalCoordinate.empty()) {
                continue;
            }

            std::map<int, std::vector<int> >::const_iterator runBegin = tileIndicesByLongitudinalCoordinate.begin();
            std::map<int, std::vector<int> >::const_iterator iterator = runBegin;
            int previousCoordinate = iterator->first;
            ++iterator;

            for (;;) {
                const bool runEnded = iterator == tileIndicesByLongitudinalCoordinate.end() || iterator->first != previousCoordinate + 1;
                if (runEnded) {
                    const int runEndCoordinate = previousCoordinate;
                    const int runLength = runEndCoordinate - runBegin->first + 1;
                    if (runLength < footprint) {
                        std::map<int, std::vector<int> >::const_iterator eraseIterator = runBegin;
                        for (; eraseIterator != iterator; ++eraseIterator) {
                            erasureTileIndices.insert(erasureTileIndices.end(), eraseIterator->second.begin(), eraseIterator->second.end());
                        }
                    }

                    if (iterator == tileIndicesByLongitudinalCoordinate.end()) {
                        break;
                    }

                    runBegin = iterator;
                }

                previousCoordinate = iterator->first;
                ++iterator;
            }
        }
    }
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

    if (changed) {
        std::size_t strokeIndex = 0;
        for (; strokeIndex < strokes_.size(); ++strokeIndex) {
            if (strokes_[strokeIndex].strokeId == oldStrokeId) {
                strokes_[strokeIndex].strokeId = lanePlacement.strokeId;
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
        if (lane.isCar() || lane.isPedestrian()) {
            resolvedCell.travelMask |= LaneIntentFromCardinalRoadMask(pathLaneMovementMask(layer, tileX, tileY, lane));
        } else {
            resolvedCell.travelMask |= lane.laneTravelMask;
        }
        resolvedCell.surfaceMask |= SurfaceMaskFor(lane.surface);
        resolvedCell.laneCount = static_cast<std::uint8_t>(std::min(255, static_cast<int>(resolvedCell.laneCount) + 1));
        const std::size_t laneTypeIndex = static_cast<std::size_t>(lane.laneType);
        if (laneTypeIndex < resolvedCell.laneTypeCosts.size()) {
            RoadTemplateElement costElement;
            costElement.laneType = lane.laneType;
            RoadLane costLane(costElement, lane.laneIndex);
            resolvedCell.laneTypeCosts[laneTypeIndex] = costLane.traversalCost(family);
        }

        if (lane.isPedestrian() && lane.surface == RoadLaneSurface::Sidewalk) {
            const std::uint8_t laneGraphicMask = pedestrianLaneGraphicMask(layer, tileX, tileY, lane);
            const std::uint8_t laneCrosswalkMask = pedestrianLaneCrosswalkMask(layer, tileX, tileY, lane, tile);
            if (laneCrosswalkMask != 0) {
                crosswalkEdges |= laneCrosswalkMask;
                resolvedCell.surfaceMask |= kRoadSurfaceCrosswalk;
            }
            sidewalkEdges |= static_cast<std::uint8_t>(laneGraphicMask & ~laneCrosswalkMask);
        }

        sameDirectionDividerEdges |= lane.sameDirectionDividerMask;
        opposingDirectionDividerEdges |= lane.opposingDirectionDividerMask;
    }

    const std::uint8_t carExitMask = buildCarExitMask(layer, tileX, tileY, tile);
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
    const RoadArrowGlyph debugArrowGlyph = ChooseArrowGlyph(LaneIntentFromCardinalRoadMask(carExitMask));
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

    const std::uint8_t movementMask = pathLaneMovementMask(layer, tileX, tileY, lanePlacement);
    std::size_t directionIndex = 0;
    for (; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        if ((movementMask & direction) == 0) {
            continue;
        }

        costMap_.addDirectionalCost(layer, mode, lanePlacement.tileIndex, direction, traversalCost, capacity);
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

void TransportNetwork::ensurePedestrianCapLanes(TransportLayerId layer, const std::vector<int>& dirtyTileIndices) {
    if (layer != TransportLayerId::Ground) {
        return;
    }

    struct PendingCapLane {
        int tileX;
        int tileY;
        RoadFamily family;
        std::uint16_t templateId;
        RoadAxis axis;
        std::uint8_t travelMask;
        std::uint8_t edgeMask;
    };

    std::vector<PendingCapLane> pendingCapLanes;
    pendingCapLanes.reserve(dirtyTileIndices.size());

    const auto queueCapLane = [&](int capTileX, int capTileY, RoadFamily family, std::uint16_t templateId, RoadAxis axis, std::uint8_t travelMask, std::uint8_t edgeMask) {
        if (!isTileInsideMap(capTileX, capTileY)) {
            return;
        }

        std::size_t pendingIndex = 0;
        for (; pendingIndex < pendingCapLanes.size(); ++pendingIndex) {
            const PendingCapLane& pending = pendingCapLanes[pendingIndex];
            if (pending.tileX == capTileX &&
                pending.tileY == capTileY &&
                pending.family == family &&
                pending.axis == axis &&
                pending.edgeMask == edgeMask) {
                return;
            }
        }

        PendingCapLane pending;
        pending.tileX = capTileX;
        pending.tileY = capTileY;
        pending.family = family;
        pending.templateId = templateId;
        pending.axis = axis;
        pending.travelMask = travelMask;
        pending.edgeMask = edgeMask;
        pendingCapLanes.push_back(pending);
    };

    std::size_t dirtyIndex = 0;
    for (; dirtyIndex < dirtyTileIndices.size(); ++dirtyIndex) {
        const int dirtyTileIndex = dirtyTileIndices[dirtyIndex];
        if (dirtyTileIndex < 0 || dirtyTileIndex >= static_cast<int>(totalTileCount_)) {
            continue;
        }

        const int tileY = dirtyTileIndex / width_;
        const int tileX = dirtyTileIndex - (tileY * width_);
        const TransportTile* tile = tileAt(layer, tileX, tileY);
        if (tile == 0 || tile->empty() || tile->family() != RoadFamily::LocalStreet) {
            continue;
        }

        std::uint16_t templateId = 0;
        const std::vector<RoadLanePlacement>& lanes = tile->lanes();
        std::size_t laneIndex = 0;
        for (; laneIndex < lanes.size(); ++laneIndex) {
            if (lanes[laneIndex].active && lanes[laneIndex].isCar()) {
                templateId = lanes[laneIndex].templateId;
                break;
            }
        }

        if (templateId == 0) {
            continue;
        }

        if (tile->hasCarAxis(RoadAxis::Horizontal)) {
            const bool westTerminal = !tileHasCarBodyAxis(layer, tileX - 1, tileY, RoadAxis::Horizontal, tile->family());
            const bool eastTerminal = !tileHasCarBodyAxis(layer, tileX + 1, tileY, RoadAxis::Horizontal, tile->family());
            if (westTerminal || eastTerminal) {
                int minCrossY = tileY;
                int maxCrossY = tileY;
                while (tileHasCarBodyAxis(layer, tileX, minCrossY - 1, RoadAxis::Horizontal, tile->family())) {
                    --minCrossY;
                }
                while (tileHasCarBodyAxis(layer, tileX, maxCrossY + 1, RoadAxis::Horizontal, tile->family())) {
                    ++maxCrossY;
                }

                int capTileY = minCrossY;
                for (; capTileY <= maxCrossY; ++capTileY) {
                    if (westTerminal) {
                        queueCapLane(tileX, capTileY, tile->family(), templateId, RoadAxis::Vertical, static_cast<std::uint8_t>(kRoadDirectionNorth | kRoadDirectionSouth), kRoadDirectionWest);
                    }
                    if (eastTerminal) {
                        queueCapLane(tileX, capTileY, tile->family(), templateId, RoadAxis::Vertical, static_cast<std::uint8_t>(kRoadDirectionNorth | kRoadDirectionSouth), kRoadDirectionEast);
                    }
                }
            }
        }

        if (tile->hasCarAxis(RoadAxis::Vertical)) {
            const bool northTerminal = !tileHasCarBodyAxis(layer, tileX, tileY - 1, RoadAxis::Vertical, tile->family());
            const bool southTerminal = !tileHasCarBodyAxis(layer, tileX, tileY + 1, RoadAxis::Vertical, tile->family());
            if (northTerminal || southTerminal) {
                int minCrossX = tileX;
                int maxCrossX = tileX;
                while (tileHasCarBodyAxis(layer, minCrossX - 1, tileY, RoadAxis::Vertical, tile->family())) {
                    --minCrossX;
                }
                while (tileHasCarBodyAxis(layer, maxCrossX + 1, tileY, RoadAxis::Vertical, tile->family())) {
                    ++maxCrossX;
                }

                int capTileX = minCrossX;
                for (; capTileX <= maxCrossX; ++capTileX) {
                    if (northTerminal) {
                        queueCapLane(capTileX, tileY, tile->family(), templateId, RoadAxis::Horizontal, static_cast<std::uint8_t>(kRoadDirectionEast | kRoadDirectionWest), kRoadDirectionNorth);
                    }
                    if (southTerminal) {
                        queueCapLane(capTileX, tileY, tile->family(), templateId, RoadAxis::Horizontal, static_cast<std::uint8_t>(kRoadDirectionEast | kRoadDirectionWest), kRoadDirectionSouth);
                    }
                }
            }
        }
    }

    std::size_t pendingIndex = 0;
    for (; pendingIndex < pendingCapLanes.size(); ++pendingIndex) {
        const PendingCapLane& pending = pendingCapLanes[pendingIndex];
        TransportTile* tile = tileAt(layer, pending.tileX, pending.tileY);
        if (tile == 0 || tile->family() != pending.family) {
            continue;
        }

        RoadLanePlacement lane;
        lane.tileX = pending.tileX;
        lane.tileY = pending.tileY;
        lane.tileIndex = tileIndex(pending.tileX, pending.tileY);
        lane.family = pending.family;
        lane.layer = layer;
        lane.templateId = pending.templateId;
        lane.laneIndex = 0;
        lane.axis = pending.axis;
        lane.crossSectionMask = 0xFFu;
        lane.laneType = RoadLaneTypeId::Pedestrian;
        lane.surface = RoadLaneSurface::Sidewalk;
        lane.role = RoadLaneRole::Access;
        lane.separatorStyle = RoadSeparatorStyle::None;
        lane.laneTravelMask = LaneIntentFromCardinalRoadMask(pending.travelMask);
        lane.arrowTravelMask = 0;
        lane.sideMin = 0.0f;
        lane.sideMax = 1.0f;
        lane.sidewalkEdgeMask = pending.edgeMask;
        tile->tryAddLane(lane);
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
                if (lanes[laneIndex].isCar() || lanes[laneIndex].isPedestrian()) {
                    const std::uint8_t movementMask = livePathLaneMovementMask(layer, tileX, tileY, lanes[laneIndex]);
                    const std::uint8_t laneTravelMask = LaneIntentFromCardinalRoadMask(movementMask);
                    const std::uint8_t arrowTravelMask = lanes[laneIndex].isCar() ? laneTravelMask : 0;
                    if (lanes[laneIndex].laneTravelMask != laneTravelMask ||
                        lanes[laneIndex].arrowTravelMask != arrowTravelMask) {
                        lanes[laneIndex].laneTravelMask = laneTravelMask;
                        lanes[laneIndex].arrowTravelMask = arrowTravelMask;
                        removedThisPass = true;
                        removedAnyLane = true;
                    }
                }

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

bool TransportNetwork::tileHasPathLaneBody(TransportLayerId layer, int tileX, int tileY, RoadFamily family, RoadLaneTypeId laneType, RoadAxis axis, bool includeInactiveLanes) const {
    const TransportTile* tile = tileAt(layer, tileX, tileY);
    if (tile == 0 || tile->family() != family) {
        return false;
    }

    const std::uint8_t axisMask = AxisMaskFor(axis);
    const std::vector<RoadLanePlacement>& lanes = tile->lanes();
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes[laneIndex];
        if ((includeInactiveLanes || lane.active) &&
            lane.family == family &&
            lane.laneType == laneType &&
            (axisMask == 0 || (AxisMaskFor(lane.axis) & axisMask) != 0)) {
            return true;
        }
    }

    return false;
}

bool TransportNetwork::tileHasPathLaneTravel(TransportLayerId layer, int tileX, int tileY, RoadFamily family, RoadLaneTypeId laneType, RoadAxis axis, std::uint8_t roadDirection) const {
    const TransportTile* tile = tileAt(layer, tileX, tileY);
    if (tile == 0 || tile->family() != family) {
        return false;
    }

    const std::uint8_t axisMask = AxisMaskFor(axis);
    const std::vector<RoadLanePlacement>& lanes = tile->lanes();
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes[laneIndex];
        if (lane.active &&
            lane.family == family &&
            lane.laneType == laneType &&
            lane.hasTravelDirection(roadDirection) &&
            (axisMask == 0 || (AxisMaskFor(lane.axis) & axisMask) != 0)) {
            return true;
        }
    }

    return false;
}

bool TransportNetwork::intersectionGroupBounds(TransportLayerId layer, int tileX, int tileY, RoadFamily family, int& minTileX, int& minTileY, int& maxTileX, int& maxTileY) const {
    if (!tileIsIntersectionGroupBody(layer, tileX, tileY, family)) {
        return false;
    }

    minTileX = tileX;
    maxTileX = tileX;
    minTileY = tileY;
    maxTileY = tileY;

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
        minTileX = std::min(minTileX, currentTileX);
        maxTileX = std::max(maxTileX, currentTileX);
        minTileY = std::min(minTileY, currentTileY);
        maxTileY = std::max(maxTileY, currentTileY);

        std::size_t directionIndex = 0;
        for (; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
            const std::uint8_t direction = kCardinalDirections[directionIndex];
            const int neighborTileX = currentTileX + DeltaXForDirection(direction);
            const int neighborTileY = currentTileY + DeltaYForDirection(direction);
            if (!isTileInsideMap(neighborTileX, neighborTileY) ||
                !tileIsIntersectionGroupBody(layer, neighborTileX, neighborTileY, family)) {
                continue;
            }

            const int neighborTileIndex = tileIndex(neighborTileX, neighborTileY);
            if (!visited[static_cast<std::size_t>(neighborTileIndex)]) {
                visited[static_cast<std::size_t>(neighborTileIndex)] = true;
                pendingTileIndices.push_back(neighborTileIndex);
            }
        }
    }

    return true;
}

bool TransportNetwork::intersectionGroupHasThroughStroke(TransportLayerId layer, int tileX, int tileY, RoadFamily family, RoadAxis axis) const {
    if (axis != RoadAxis::Horizontal && axis != RoadAxis::Vertical) {
        return false;
    }

    int minTileX = tileX;
    int maxTileX = tileX;
    int minTileY = tileY;
    int maxTileY = tileY;
    if (!intersectionGroupBounds(layer, tileX, tileY, family, minTileX, minTileY, maxTileX, maxTileY)) {
        return false;
    }

    const bool horizontal = axis == RoadAxis::Horizontal;
    struct AxisSegment {
        int minX;
        int minY;
        int maxX;
        int maxY;
        bool touchesFirstSide;
        bool touchesSecondSide;
    };

    std::vector<AxisSegment> segments;
    std::size_t strokeIndex = 0;
    for (; strokeIndex < strokes_.size(); ++strokeIndex) {
        const TransportStrokeSaveState& stroke = strokes_[strokeIndex];
        if (stroke.layer != layer || stroke.family != family) {
            continue;
        }

        const RoadTemplate roadTemplate = Road::makeTemplate(stroke.family, stroke.layer, stroke.laneCount, stroke.trafficSide, stroke.directionMode);
        const int footprint = std::max(1, static_cast<int>(roadTemplate.identity.footprint));
        const Int2 segmentEnds[][2] = {
            {stroke.startTile, stroke.cornerTile},
            {stroke.cornerTile, stroke.endTile}
        };

        std::size_t segmentIndex = 0;
        for (; segmentIndex < sizeof(segmentEnds) / sizeof(segmentEnds[0]); ++segmentIndex) {
            const Int2& start = segmentEnds[segmentIndex][0];
            const Int2& end = segmentEnds[segmentIndex][1];
            if (start == end) {
                continue;
            }

            AxisSegment segment;
            segment.touchesFirstSide = false;
            segment.touchesSecondSide = false;
            if (horizontal) {
                if (start.y != end.y) {
                    continue;
                }
                segment.minX = std::min(start.x, end.x);
                segment.maxX = std::max(start.x, end.x);
                segment.minY = start.y;
                segment.maxY = start.y + footprint - 1;
            } else {
                if (start.x != end.x) {
                    continue;
                }
                segment.minX = start.x;
                segment.maxX = start.x + footprint - 1;
                segment.minY = std::min(start.y, end.y);
                segment.maxY = std::max(start.y, end.y);
            }

            const bool crossesGroupBand = horizontal
                ? (segment.minY <= maxTileY && segment.maxY >= minTileY)
                : (segment.minX <= maxTileX && segment.maxX >= minTileX);
            if (!crossesGroupBand) {
                continue;
            }

            const int firstSide = horizontal ? minTileX - 1 : minTileY - 1;
            const int secondSide = horizontal ? maxTileX + 1 : maxTileY + 1;
            const bool reachesFirstSide = horizontal
                ? (segment.minX <= firstSide && segment.maxX >= firstSide)
                : (segment.minY <= firstSide && segment.maxY >= firstSide);
            const bool reachesSecondSide = horizontal
                ? (segment.minX <= secondSide && segment.maxX >= secondSide)
                : (segment.minY <= secondSide && segment.maxY >= secondSide);

            if (reachesFirstSide || reachesSecondSide) {
                const int scanStart = horizontal ? std::max(segment.minY, minTileY) : std::max(segment.minX, minTileX);
                const int scanEnd = horizontal ? std::min(segment.maxY, maxTileY) : std::min(segment.maxX, maxTileX);
                int scan = scanStart;
                for (; scan <= scanEnd; ++scan) {
                    if (reachesFirstSide) {
                        const int sideTileX = horizontal ? firstSide : scan;
                        const int sideTileY = horizontal ? scan : firstSide;
                        if (isTileInsideMap(sideTileX, sideTileY) &&
                            !tileIsErased(layer, tileIndex(sideTileX, sideTileY))) {
                            segment.touchesFirstSide = true;
                        }
                    }
                    if (reachesSecondSide) {
                        const int sideTileX = horizontal ? secondSide : scan;
                        const int sideTileY = horizontal ? scan : secondSide;
                        if (isTileInsideMap(sideTileX, sideTileY) &&
                            !tileIsErased(layer, tileIndex(sideTileX, sideTileY))) {
                            segment.touchesSecondSide = true;
                        }
                    }
                }
            }

            segments.push_back(segment);
        }
    }

    if (segments.empty()) {
        return false;
    }

    std::vector<std::size_t> pendingSegmentIndices;
    std::vector<bool> visited(segments.size(), false);
    std::size_t startSegmentIndex = 0;
    for (; startSegmentIndex < segments.size(); ++startSegmentIndex) {
        if (segments[startSegmentIndex].touchesFirstSide) {
            pendingSegmentIndices.push_back(startSegmentIndex);
            visited[startSegmentIndex] = true;
        }
    }

    std::size_t readIndex = 0;
    for (; readIndex < pendingSegmentIndices.size(); ++readIndex) {
        const AxisSegment& current = segments[pendingSegmentIndices[readIndex]];
        if (current.touchesSecondSide) {
            return true;
        }

        std::size_t candidateIndex = 0;
        for (; candidateIndex < segments.size(); ++candidateIndex) {
            if (visited[candidateIndex]) {
                continue;
            }

            const AxisSegment& candidate = segments[candidateIndex];
            if (current.minX <= candidate.maxX &&
                current.maxX >= candidate.minX &&
                current.minY <= candidate.maxY &&
                current.maxY >= candidate.minY) {
                visited[candidateIndex] = true;
                pendingSegmentIndices.push_back(candidateIndex);
            }
        }
    }

    return false;
}

bool TransportNetwork::authoredStrokeCoversMovementEdge(TransportLayerId layer, int tileX, int tileY, RoadFamily family, std::uint8_t roadDirection) const {
    const RoadAxis axis = AxisForDirection(roadDirection);
    if (axis != RoadAxis::Horizontal && axis != RoadAxis::Vertical) {
        return false;
    }

    const int neighborTileX = tileX + DeltaXForDirection(roadDirection);
    const int neighborTileY = tileY + DeltaYForDirection(roadDirection);
    if (!isTileInsideMap(neighborTileX, neighborTileY) ||
        tileIsErased(layer, tileIndex(tileX, tileY)) ||
        tileIsErased(layer, tileIndex(neighborTileX, neighborTileY))) {
        return false;
    }

    const bool horizontal = axis == RoadAxis::Horizontal;
    const int movementMinX = std::min(tileX, neighborTileX);
    const int movementMaxX = std::max(tileX, neighborTileX);
    const int movementMinY = std::min(tileY, neighborTileY);
    const int movementMaxY = std::max(tileY, neighborTileY);

    std::size_t strokeIndex = 0;
    for (; strokeIndex < strokes_.size(); ++strokeIndex) {
        const TransportStrokeSaveState& stroke = strokes_[strokeIndex];
        if (stroke.layer != layer || stroke.family != family) {
            continue;
        }

        const RoadTemplate roadTemplate = Road::makeTemplate(stroke.family, stroke.layer, stroke.laneCount, stroke.trafficSide, stroke.directionMode);
        const int footprint = std::max(1, static_cast<int>(roadTemplate.identity.footprint));
        const Int2 segmentEnds[][2] = {
            {stroke.startTile, stroke.cornerTile},
            {stroke.cornerTile, stroke.endTile}
        };

        std::size_t segmentIndex = 0;
        for (; segmentIndex < sizeof(segmentEnds) / sizeof(segmentEnds[0]); ++segmentIndex) {
            const Int2& start = segmentEnds[segmentIndex][0];
            const Int2& end = segmentEnds[segmentIndex][1];
            if (start == end) {
                continue;
            }

            if (horizontal) {
                if (start.y != end.y) {
                    continue;
                }

                const int segmentMinX = std::min(start.x, end.x);
                const int segmentMaxX = std::max(start.x, end.x);
                const int segmentMinY = start.y;
                const int segmentMaxY = start.y + footprint - 1;
                if (movementMinX >= segmentMinX && movementMaxX <= segmentMaxX &&
                    tileY >= segmentMinY && tileY <= segmentMaxY) {
                    return true;
                }
            } else {
                if (start.x != end.x) {
                    continue;
                }

                const int segmentMinX = start.x;
                const int segmentMaxX = start.x + footprint - 1;
                const int segmentMinY = std::min(start.y, end.y);
                const int segmentMaxY = std::max(start.y, end.y);
                if (tileX >= segmentMinX && tileX <= segmentMaxX &&
                    movementMinY >= segmentMinY && movementMaxY <= segmentMaxY) {
                    return true;
                }
            }
        }
    }

    return false;
}

std::uint8_t TransportNetwork::liveCapReturnDirectionForLane(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection) const {
    const std::uint8_t axisMask = AxisMaskFor(lanePlacement.axis);
    if (roadDirection == 0 ||
        !CarLaneAllowsCapReturn(lanePlacement) ||
        (axisMask != AxisMaskFor(RoadAxis::Horizontal) && axisMask != AxisMaskFor(RoadAxis::Vertical))) {
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
        int stepIndex = 0;
        for (; stepIndex < maximumCrossSteps; ++stepIndex) {
            scanTileX += DeltaXForDirection(crossDirection);
            scanTileY += DeltaYForDirection(crossDirection);
            const TransportTile* scanTile = tileAt(layer, scanTileX, scanTileY);
            if (scanTile == 0 || scanTile->family() != lanePlacement.family) {
                break;
            }

            const std::vector<RoadLanePlacement>& lanes = scanTile->lanes();
            std::size_t laneIndex = 0;
            for (; laneIndex < lanes.size(); ++laneIndex) {
                const RoadLanePlacement& candidate = lanes[laneIndex];
                if (candidate.active &&
                    candidate.family == lanePlacement.family &&
                    candidate.laneType == lanePlacement.laneType &&
                    candidate.laneIndex != lanePlacement.laneIndex &&
                    (AxisMaskFor(candidate.axis) & axisMask) != 0 &&
                    candidate.hasTravelDirection(returnDirection)) {
                    return crossDirection;
                }
            }
        }
    }

    return 0;
}

std::uint8_t TransportNetwork::carCapLoopMovementMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const {
    if (!lanePlacement.isCar() ||
        !CarLaneAllowsCapReturn(lanePlacement) ||
        (lanePlacement.axis != RoadAxis::Horizontal && lanePlacement.axis != RoadAxis::Vertical)) {
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

    const int capDepth = std::max(1, width / 2);
    const int crossOffset = (horizontal ? tileY : tileX) - minCross;
    const int halfWidth = std::max(1, width / 2);

    int minLongitudinal = horizontal ? tileX : tileY;
    int maxLongitudinal = minLongitudinal;
    while (tileHasCarBodyAxis(layer, horizontal ? minLongitudinal - 1 : tileX, horizontal ? tileY : minLongitudinal - 1, lanePlacement.axis, lanePlacement.family)) {
        --minLongitudinal;
    }
    while (tileHasCarBodyAxis(layer, horizontal ? maxLongitudinal + 1 : tileX, horizontal ? tileY : maxLongitudinal + 1, lanePlacement.axis, lanePlacement.family)) {
        ++maxLongitudinal;
    }

    const int longitudinal = horizontal ? tileX : tileY;
    const int distanceToStart = longitudinal - minLongitudinal;
    const int distanceToEnd = maxLongitudinal - longitudinal;
    const bool useStartCap = distanceToStart < capDepth && distanceToStart <= distanceToEnd;
    const bool useEndCap = distanceToEnd < capDepth && distanceToEnd < distanceToStart;
    if (!useStartCap && !useEndCap) {
        return 0;
    }

    const int capOffset = useStartCap ? distanceToStart : distanceToEnd;
    if (horizontal) {
        if (useStartCap) {
            if (crossOffset < halfWidth) {
                return capOffset <= crossOffset ? kRoadDirectionSouth : kRoadDirectionWest;
            }
            return capOffset < width - 1 - crossOffset ? kRoadDirectionSouth : kRoadDirectionEast;
        }

        if (crossOffset < halfWidth) {
            return capOffset < crossOffset ? kRoadDirectionNorth : kRoadDirectionWest;
        }
        return capOffset <= width - 1 - crossOffset ? kRoadDirectionNorth : kRoadDirectionEast;
    }

    if (useStartCap) {
        if (crossOffset < halfWidth) {
            return capOffset >= crossOffset ? kRoadDirectionSouth : kRoadDirectionWest;
        }
        return capOffset <= width - 1 - crossOffset ? kRoadDirectionWest : kRoadDirectionNorth;
    }

    if (crossOffset < halfWidth) {
        return capOffset > crossOffset ? kRoadDirectionSouth : kRoadDirectionEast;
    }
    return capOffset >= width - 1 - crossOffset ? kRoadDirectionNorth : kRoadDirectionEast;
}

std::uint8_t TransportNetwork::intersectionBodyLaneMovementMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const {
    const std::uint8_t approachMask = buildIntersectionGroupApproachMask(layer, tileX, tileY, lanePlacement.family);
    if (!tileIsIntersectionGroupBody(layer, tileX, tileY, lanePlacement.family) ||
        CountCardinalDirections(approachMask) < 4) {
        return 0;
    }

    if (!intersectionGroupHasThroughStroke(layer, tileX, tileY, lanePlacement.family, RoadAxis::Horizontal) ||
        !intersectionGroupHasThroughStroke(layer, tileX, tileY, lanePlacement.family, RoadAxis::Vertical)) {
        return 0;
    }

    int minTileX = tileX;
    int maxTileX = tileX;
    int minTileY = tileY;
    int maxTileY = tileY;
    if (!intersectionGroupBounds(layer, tileX, tileY, lanePlacement.family, minTileX, minTileY, maxTileX, maxTileY)) {
        return 0;
    }

    const int groupWidth = (maxTileX - minTileX) + 1;
    const int groupHeight = (maxTileY - minTileY) + 1;
    std::uint8_t movementMask = 0;
    if (groupHeight > 1) {
        movementMask |= ((tileY - minTileY) * 2 < groupHeight) ? kRoadDirectionWest : kRoadDirectionEast;
    }
    if (groupWidth > 1) {
        movementMask |= ((tileX - minTileX) * 2 < groupWidth) ? kRoadDirectionSouth : kRoadDirectionNorth;
    }

    const std::uint8_t authoredMask = RoadDirectionMaskForLane(lanePlacement);
    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        const bool preservesOneTileAxis =
            (groupWidth == 1 && (direction == kRoadDirectionNorth || direction == kRoadDirectionSouth)) ||
            (groupHeight == 1 && (direction == kRoadDirectionEast || direction == kRoadDirectionWest));
        if ((authoredMask & direction) == 0 ||
            (approachMask & direction) == 0 ||
            !preservesOneTileAxis) {
            continue;
        }

        if (tileHasAnyCarBody(layer, tileX + DeltaXForDirection(direction), tileY + DeltaYForDirection(direction), lanePlacement.family)) {
            movementMask |= direction;
        }
    }

    return movementMask;
}

std::uint8_t TransportNetwork::livePathLaneMovementMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const {
    if (!lanePlacement.isCar() && !lanePlacement.isPedestrian()) {
        return RoadDirectionMaskForLane(lanePlacement);
    }

    if (lanePlacement.isCar()) {
        const std::uint8_t intersectionMovementMask = intersectionBodyLaneMovementMask(layer, tileX, tileY, lanePlacement);
        if (intersectionMovementMask != 0) {
            return intersectionMovementMask;
        }

    }

    if (lanePlacement.isPedestrian() &&
        tileIsIntersectionGroupBody(layer, tileX, tileY, lanePlacement.family) &&
        CountCardinalDirections(buildIntersectionGroupApproachMask(layer, tileX, tileY, lanePlacement.family)) >= 4) {
        std::uint8_t movementMask = 0;
        for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
            const std::uint8_t direction = kCardinalDirections[directionIndex];
            const int neighborTileX = tileX + DeltaXForDirection(direction);
            const int neighborTileY = tileY + DeltaYForDirection(direction);
            if (!pedestrianConnectionCrossesSeparator(layer, tileX, tileY, lanePlacement, direction) &&
                tileHasPathLaneBody(layer, neighborTileX, neighborTileY, lanePlacement.family, RoadLaneTypeId::Pedestrian, RoadAxis::None, true)) {
                movementMask |= direction;
            }
        }

        return movementMask;
    }

    const std::uint8_t axisMask = AxisMaskFor(lanePlacement.axis);
    if (axisMask != AxisMaskFor(RoadAxis::Horizontal) && axisMask != AxisMaskFor(RoadAxis::Vertical)) {
        return RoadDirectionMaskForLane(lanePlacement);
    }

    const std::uint8_t capLoopMovementMask = carCapLoopMovementMask(layer, tileX, tileY, lanePlacement);
    if (capLoopMovementMask != 0) {
        return capLoopMovementMask;
    }

    const std::uint8_t axisDirections[] = {
        lanePlacement.axis == RoadAxis::Horizontal ? kRoadDirectionEast : kRoadDirectionNorth,
        lanePlacement.axis == RoadAxis::Horizontal ? kRoadDirectionWest : kRoadDirectionSouth
    };

    std::uint8_t movementMask = 0;
    for (std::size_t directionIndex = 0; directionIndex < sizeof(axisDirections) / sizeof(axisDirections[0]); ++directionIndex) {
        const std::uint8_t direction = axisDirections[directionIndex];
        const bool laneIntendsDirection = lanePlacement.hasTravelDirection(direction) ||
            (CarLaneAllowsCapReturn(lanePlacement) &&
                tileHasPathLaneTravel(
                    layer,
                    tileX - DeltaXForDirection(direction),
                    tileY - DeltaYForDirection(direction),
                    lanePlacement.family,
                    lanePlacement.laneType,
                    lanePlacement.axis,
                    direction));
        const bool hasForwardBody = tileHasPathLaneBody(
                layer,
                tileX + DeltaXForDirection(direction),
                tileY + DeltaYForDirection(direction),
                lanePlacement.family,
                lanePlacement.laneType,
                lanePlacement.axis);
        if (laneIntendsDirection && hasForwardBody) {
            movementMask |= direction;
        } else if (!hasForwardBody) {
            movementMask |= liveCapReturnDirectionForLane(layer, tileX, tileY, lanePlacement, direction);
        }
    }

    return movementMask;
}

bool TransportNetwork::pathLaneCanMove(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection) const {
    return (pathLaneMovementMask(layer, tileX, tileY, lanePlacement) & roadDirection) != 0;
}

std::uint8_t TransportNetwork::pathLaneMovementMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const {
    if (!lanePlacement.active) {
        return 0;
    }

    const std::uint8_t liveMovementMask = livePathLaneMovementMask(layer, tileX, tileY, lanePlacement);
    std::uint8_t movementMask = 0;
    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        if ((liveMovementMask & direction) == 0) {
            continue;
        }

        const int neighborTileX = tileX + DeltaXForDirection(direction);
        const int neighborTileY = tileY + DeltaYForDirection(direction);
        if ((lanePlacement.isCar() || lanePlacement.isPedestrian()) &&
            tileIsIntersectionGroupBody(layer, tileX, tileY, lanePlacement.family) &&
            tileIsIntersectionGroupBody(layer, neighborTileX, neighborTileY, lanePlacement.family) &&
            !intersectionGroupHasThroughStroke(layer, tileX, tileY, lanePlacement.family, AxisForDirection(direction)) &&
            !authoredStrokeCoversMovementEdge(layer, tileX, tileY, lanePlacement.family, direction)) {
            continue;
        }

        if (lanePlacement.isCar() &&
            tileHasAnyCarBody(layer, neighborTileX, neighborTileY, lanePlacement.family)) {
            movementMask |= direction;
        } else if (lanePlacement.isPedestrian() &&
            !pedestrianConnectionCrossesSeparator(layer, tileX, tileY, lanePlacement, direction) &&
            tileHasPathLaneBody(layer, neighborTileX, neighborTileY, lanePlacement.family, lanePlacement.laneType, RoadAxis::None)) {
            movementMask |= direction;
        }
    }

    return movementMask;
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

bool TransportNetwork::separatorLaneIsCrossedByCarLane(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const {
    const TransportTile* tile = tileAt(layer, tileX, tileY);
    if (tile == 0 || !lanePlacement.isSeparator()) {
        return false;
    }

    const std::uint8_t dividerMask = separatorDividerMaskForLane(lanePlacement);
    if (dividerMask == 0) {
        return false;
    }

    const std::vector<RoadLanePlacement>& lanes = tile->lanes();
    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        if ((dividerMask & direction) == 0) {
            continue;
        }

        std::size_t laneIndex = 0;
        for (; laneIndex < lanes.size(); ++laneIndex) {
            const RoadLanePlacement& carLane = lanes[laneIndex];
            if (carLane.active &&
                carLane.isCar() &&
                carLane.family == lanePlacement.family &&
                carLane.hasTravelDirection(direction) &&
                tileHasAnyCarBody(layer, tileX + DeltaXForDirection(direction), tileY + DeltaYForDirection(direction), lanePlacement.family)) {
                return true;
            }
        }
    }

    return false;
}

bool TransportNetwork::separatorLaneShouldBeActive(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const {
    if (!lanePlacement.isSeparator() || lanePlacement.separatorStyle == RoadSeparatorStyle::None) {
        return false;
    }

    return !separatorLaneIsCrossedByCarLane(layer, tileX, tileY, lanePlacement) &&
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

    if (lanePlacement.opposingDirectionDividerMask != 0) {
        return lanePlacement.opposingDirectionDividerMask;
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

bool TransportNetwork::tileHasActiveSeparatorEdge(TransportLayerId layer, int tileX, int tileY, RoadAxis separatorAxis, std::uint8_t edgeDirection, RoadFamily family) const {
    const TransportTile* tile = tileAt(layer, tileX, tileY);
    if (tile == 0 || separatorAxis == RoadAxis::None || edgeDirection == 0) {
        return false;
    }

    const std::vector<RoadLanePlacement>& lanes = tile->lanes();
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes[laneIndex];
        if (lane.active &&
            lane.isSeparator() &&
            lane.family == family &&
            (separatorDividerMaskForLane(lane) & edgeDirection) != 0) {
            return true;
        }
    }

    return false;
}

bool TransportNetwork::pedestrianConnectionCrossesSeparator(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection) const {
    if (!lanePlacement.isPedestrian() ||
        roadDirection == 0) {
        return false;
    }

    const RoadAxis movementAxis = AxisForDirection(roadDirection);
    const RoadAxis separatorAxis = movementAxis == RoadAxis::Horizontal ? RoadAxis::Vertical : RoadAxis::Horizontal;
    const int neighborTileX = tileX + DeltaXForDirection(roadDirection);
    const int neighborTileY = tileY + DeltaYForDirection(roadDirection);
    return tileHasActiveSeparatorEdge(layer, tileX, tileY, separatorAxis, roadDirection, lanePlacement.family) ||
        tileHasActiveSeparatorEdge(layer, neighborTileX, neighborTileY, separatorAxis, OppositeCardinal(roadDirection), lanePlacement.family);
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
    if (pedestrianLaneBordersEmptyTile(layer, tileX, tileY, pedestrianLane)) {
        return true;
    }

    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        if (!pedestrianLane.hasTravelDirection(direction) ||
            pedestrianConnectionCrossesSeparator(layer, tileX, tileY, pedestrianLane, direction)) {
            continue;
        }

        const int neighborTileX = tileX + DeltaXForDirection(direction);
        const int neighborTileY = tileY + DeltaYForDirection(direction);
        if (tileHasPathLaneBody(layer, neighborTileX, neighborTileY, pedestrianLane.family, pedestrianLane.laneType, RoadAxis::None, true)) {
            return true;
        }
    }

    return false;
}

std::uint8_t TransportNetwork::pedestrianLaneGraphicMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane) const {
    std::uint8_t graphicMask = 0;

    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        const int neighborTileX = tileX + DeltaXForDirection(direction);
        const int neighborTileY = tileY + DeltaYForDirection(direction);
        const TransportTile* neighborTile = tileAt(layer, neighborTileX, neighborTileY);
        if (neighborTile == 0 || neighborTile->empty()) {
            graphicMask |= direction;
        }
    }

    return static_cast<std::uint8_t>(graphicMask & kRoadSurfaceSidewalkEdgeMask);
}

std::uint8_t TransportNetwork::pedestrianLaneCrosswalkMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane, const TransportTile& tile) const {
    if (!pedestrianLane.isPedestrian()) {
        return 0;
    }

    if (!tileIsIntersectionGroupBody(layer, tileX, tileY, tile.family())) {
        return 0;
    }

    const std::uint8_t approachMask = buildJunctionMask(layer, tileX, tileY, tile, 0);
    const int approachCount = CountCardinalDirections(approachMask);
    if (approachCount < 3) {
        return 0;
    }

    const std::uint8_t crosswalkCandidateMask = pathLaneMovementMask(layer, tileX, tileY, pedestrianLane);
    std::uint8_t crosswalkMask = 0;
    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        if ((crosswalkCandidateMask & direction) == 0 ||
            (approachMask & direction) == 0) {
            continue;
        }

        const RoadAxis pedestrianAxis = AxisForDirection(direction);
        const RoadAxis crossedCarAxis = pedestrianAxis == RoadAxis::Horizontal ? RoadAxis::Vertical : RoadAxis::Horizontal;
        if (intersectionGroupHasThroughStroke(layer, tileX, tileY, tile.family(), crossedCarAxis)) {
            crosswalkMask |= direction;
        }
    }

    return static_cast<std::uint8_t>(crosswalkMask & kRoadSurfaceSidewalkEdgeMask);
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
            if (!pathLaneCanMove(layer, tileX, tileY, lane, direction)) {
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
    const std::vector<RoadLanePlacement>& lanes = tile.lanes();
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes[laneIndex];
        if (!lane.active || !lane.isCar()) {
            continue;
        }

        exitMask |= pathLaneMovementMask(layer, tileX, tileY, lane);
    }

    return exitMask;
}

std::uint8_t TransportNetwork::buildExitMask(TransportLayerId layer, int tileX, int tileY, const TransportTile& tile) const {
    std::uint8_t exitMask = 0;
    const std::vector<RoadLanePlacement>& lanes = tile.lanes();
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes[laneIndex];
        if (!lane.active || lane.isSeparator()) {
            continue;
        }
        exitMask |= pathLaneMovementMask(layer, tileX, tileY, lane);
    }

    return exitMask;
}

std::uint8_t TransportNetwork::buildJunctionMask(TransportLayerId layer, int tileX, int tileY, const TransportTile& tile, std::uint8_t exitMask) const {
    const bool hasCarLanes = tile.hasLaneType(RoadLaneTypeId::Car);
    if (!hasCarLanes) {
        return static_cast<std::uint8_t>(exitMask & (kRoadDirectionNorth | kRoadDirectionEast | kRoadDirectionSouth | kRoadDirectionWest));
    }

    if (isCarIntersectionNode(layer, tileX, tileY)) {
        return buildIntersectionGroupApproachMask(layer, tileX, tileY, tile.family());
    }

    std::uint8_t junctionMask = buildCarExitMask(layer, tileX, tileY, tile);
    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        const TransportTile* neighborTile = tileAt(layer, tileX + DeltaXForDirection(direction), tileY + DeltaYForDirection(direction));
        if (neighborTile == 0 || neighborTile->family() != tile.family()) {
            continue;
        }

        const std::uint8_t returnDirection = OppositeCardinal(direction);
        if ((buildCarExitMask(layer, tileX + DeltaXForDirection(direction), tileY + DeltaYForDirection(direction), *neighborTile) & returnDirection) != 0) {
            junctionMask |= direction;
        }
    }

    return junctionMask;
}
