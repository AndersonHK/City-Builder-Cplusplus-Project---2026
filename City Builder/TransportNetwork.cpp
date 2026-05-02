#include "TransportNetwork.h"

#include <algorithm>

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
}

TransportNetwork::TransportNetwork()
    : width_(0),
      height_(0),
      totalTileCount_(0),
      chunkWidth_(1),
      chunkHeight_(1),
      chunksPerRow_(1),
      revision_(0) {
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
    groundRoadRenderState_.assign(totalTileCount_ * kGroundRoadRenderChannelsPerTile, 0);
    groundChunkRevisions_.assign(chunkLayout_.size(), 1);
    elevatedChunkRevisions_.assign(chunkLayout_.size(), 1);
    revision_ = 0;
}

void TransportNetwork::clear() {
    transportTiles_.assign(totalTileCount_ * layerCount(), TransportTile());
    resolvedCells_.assign(totalTileCount_ * layerCount(), ResolvedRoadCell());
    groundRoadRenderState_.assign(totalTileCount_ * kGroundRoadRenderChannelsPerTile, 0);
    groundChunkRevisions_.assign(chunkLayout_.size(), 1);
    elevatedChunkRevisions_.assign(chunkLayout_.size(), 1);
    revision_ = 0;
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

    bool madeChange = false;
    if (!validateAndApplyPlacements(roadStrokeCommand.layer, placements, lotOccupancy, invalidLotId, madeChange)) {
        return false;
    }

    if (!madeChange) {
        return true;
    }

    std::vector<int> dirtyTileIndices;
    markDirtyNeighborhood(placements, dirtyTileIndices);

    std::size_t dirtyIndex = 0;
    for (; dirtyIndex < dirtyTileIndices.size(); ++dirtyIndex) {
        const int dirtyTileIndex = dirtyTileIndices[dirtyIndex];
        const int tileY = dirtyTileIndex / width_;
        const int tileX = dirtyTileIndex - (tileY * width_);
        resolveDirtyTile(roadStrokeCommand.layer, tileX, tileY);
    }

    bumpDirtyChunkRevisions(roadStrokeCommand.layer, dirtyTileIndices);
    ++revision_;
    return true;
}

const std::vector<ResolvedRoadCell>& TransportNetwork::resolvedCells() const {
    return resolvedCells_;
}

const std::vector<std::uint8_t>& TransportNetwork::groundRoadRenderState() const {
    return groundRoadRenderState_;
}

const std::vector<std::uint64_t>& TransportNetwork::groundChunkRevisions() const {
    return groundChunkRevisions_;
}

const std::vector<std::uint64_t>& TransportNetwork::elevatedChunkRevisions() const {
    return elevatedChunkRevisions_;
}

std::uint64_t TransportNetwork::revision() const {
    return revision_;
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
            if (pedestrianLaneShouldRenderCrosswalk(layer, tileX, tileY, lane, tile)) {
                crosswalkEdges |= lane.sidewalkEdgeMask;
                resolvedCell.surfaceMask |= kRoadSurfaceCrosswalk;
            } else {
                sidewalkEdges |= lane.sidewalkEdgeMask;
            }
        }

        sameDirectionDividerEdges |= lane.sameDirectionDividerMask;
        opposingDirectionDividerEdges |= lane.opposingDirectionDividerMask;
    }

    const bool isCarIntersection = tile.hasCarAxis(RoadAxis::Horizontal) && tile.hasCarAxis(RoadAxis::Vertical);
    if (isCarIntersection) {
        sameDirectionDividerEdges = 0;
        opposingDirectionDividerEdges = 0;
    }

    const std::uint8_t exitMask = buildExitMask(layer, tileX, tileY, tile);
    const std::uint8_t junctionMask = buildJunctionMask(layer, tileX, tileY, tile, exitMask);
    const RoadRenderVariant renderVariant = ChooseRenderVariant(junctionMask);

    resolvedCell.family = static_cast<std::uint8_t>(family);
    resolvedCell.exitMask = exitMask;
    resolvedCell.junctionMask = junctionMask;
    resolvedCell.renderVariant = static_cast<std::uint8_t>(renderVariant);
    resolvedCell.baseGlyph = static_cast<std::uint8_t>(ChooseBaseGlyph(family, renderVariant, junctionMask));
    resolvedCell.arrowGlyph = static_cast<std::uint8_t>(ChooseArrowGlyph(arrowTravelMask));
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

void TransportNetwork::markDirtyNeighborhood(const std::vector<RoadTilePlacement>& placements, std::vector<int>& dirtyTileIndices) const {
    dirtyTileIndices.reserve(placements.size() * 9);
    std::size_t placementIndex = 0;
    for (; placementIndex < placements.size(); ++placementIndex) {
        const RoadTilePlacement& placement = placements[placementIndex];
        int neighborTileY = placement.tileY - 1;
        for (; neighborTileY <= placement.tileY + 1; ++neighborTileY) {
            int neighborTileX = placement.tileX - 1;
            for (; neighborTileX <= placement.tileX + 1; ++neighborTileX) {
                if (isTileInsideMap(neighborTileX, neighborTileY)) {
                    dirtyTileIndices.push_back(tileIndex(neighborTileX, neighborTileY));
                }
            }
        }
    }

    std::sort(dirtyTileIndices.begin(), dirtyTileIndices.end());
    dirtyTileIndices.erase(std::unique(dirtyTileIndices.begin(), dirtyTileIndices.end()), dirtyTileIndices.end());
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

bool TransportNetwork::hasCompatibleNeighborLane(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection) const {
    const TransportTile* neighborTile = tileAt(layer, tileX + DeltaXForDirection(roadDirection), tileY + DeltaYForDirection(roadDirection));
    if (neighborTile == 0) {
        return false;
    }

    return neighborTile->hasCompatibleLane(lanePlacement.family, lanePlacement.laneType, lanePlacement.axis, roadDirection);
}

bool TransportNetwork::hasCarThroughBothEnds(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& carLane) const {
    const std::uint8_t firstDirection = carLane.axis == RoadAxis::Horizontal ? kRoadDirectionEast : kRoadDirectionNorth;
    const std::uint8_t secondDirection = OppositeCardinal(firstDirection);
    const TransportTile* firstNeighbor = tileAt(layer, tileX + DeltaXForDirection(firstDirection), tileY + DeltaYForDirection(firstDirection));
    const TransportTile* secondNeighbor = tileAt(layer, tileX + DeltaXForDirection(secondDirection), tileY + DeltaYForDirection(secondDirection));
    return firstNeighbor != 0 &&
        secondNeighbor != 0 &&
        firstNeighbor->family() == carLane.family &&
        secondNeighbor->family() == carLane.family &&
        firstNeighbor->hasCarAxis(carLane.axis) &&
        secondNeighbor->hasCarAxis(carLane.axis);
}

bool TransportNetwork::hasPedestrianThroughBothEnds(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane) const {
    const std::uint8_t firstDirection = pedestrianLane.axis == RoadAxis::Horizontal ? kRoadDirectionEast : kRoadDirectionNorth;
    const std::uint8_t secondDirection = OppositeCardinal(firstDirection);
    return hasCompatibleNeighborLane(layer, tileX, tileY, pedestrianLane, firstDirection) &&
        hasCompatibleNeighborLane(layer, tileX, tileY, pedestrianLane, secondDirection);
}

bool TransportNetwork::pedestrianLaneShouldRenderCrosswalk(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane, const TransportTile& tile) const {
    if (!pedestrianLane.isPedestrian() || pedestrianLane.sidewalkEdgeMask == 0) {
        return false;
    }

    if (!hasPedestrianThroughBothEnds(layer, tileX, tileY, pedestrianLane)) {
        return false;
    }

    const std::vector<RoadLanePlacement>& lanes = tile.lanes();
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes[laneIndex];
        if (lane.isCar() &&
            lane.axis != pedestrianLane.axis &&
            hasCarThroughBothEnds(layer, tileX, tileY, lane)) {
            return true;
        }
    }

    return false;
}

std::uint8_t TransportNetwork::buildExitMask(TransportLayerId layer, int tileX, int tileY, const TransportTile& tile) const {
    std::uint8_t exitMask = 0;
    const std::vector<RoadLanePlacement>& lanes = tile.lanes();
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes[laneIndex];
        for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
            const std::uint8_t direction = kCardinalDirections[directionIndex];
            if (lane.axis == AxisForDirection(direction) &&
                lane.hasTravelDirection(direction) &&
                hasCompatibleNeighborLane(layer, tileX, tileY, lane, direction)) {
                exitMask |= direction;
            }
        }
    }

    return exitMask;
}

std::uint8_t TransportNetwork::buildJunctionMask(TransportLayerId layer, int tileX, int tileY, const TransportTile& tile, std::uint8_t exitMask) const {
    std::uint8_t junctionMask = exitMask & (kRoadDirectionNorth | kRoadDirectionEast | kRoadDirectionSouth | kRoadDirectionWest);
    const bool hasCarLanes = tile.hasLaneType(RoadLaneTypeId::Car);

    for (std::size_t directionIndex = 0; directionIndex < sizeof(kCardinalDirections) / sizeof(kCardinalDirections[0]); ++directionIndex) {
        const std::uint8_t direction = kCardinalDirections[directionIndex];
        const TransportTile* neighborTile = tileAt(layer, tileX + DeltaXForDirection(direction), tileY + DeltaYForDirection(direction));
        if (neighborTile == 0 || neighborTile->family() != tile.family()) {
            continue;
        }

        const RoadAxis axis = AxisForDirection(direction);
        if (hasCarLanes) {
            if (tile.hasCarAxis(axis) && neighborTile->hasCarAxis(axis)) {
                junctionMask |= direction;
            }
        } else if (tile.hasAxis(axis) && neighborTile->hasAxis(axis)) {
            junctionMask |= direction;
        }
    }

    return junctionMask;
}
