#include "TransportNetwork.h"

#include <algorithm>

namespace {
// Chooses the road shape implied by cardinal exits.
RoadRenderVariant ChooseRenderVariant(std::uint8_t junctionMask) {
    const bool hasNorth = (junctionMask & kRoadDirectionNorth) != 0;
    const bool hasEast = (junctionMask & kRoadDirectionEast) != 0;
    const bool hasSouth = (junctionMask & kRoadDirectionSouth) != 0;
    const bool hasWest = (junctionMask & kRoadDirectionWest) != 0;
    const int cardinalCount = (hasNorth ? 1 : 0) + (hasEast ? 1 : 0) + (hasSouth ? 1 : 0) + (hasWest ? 1 : 0);

    switch (cardinalCount) {
        case 0:
            return RoadRenderVariant::Isolated;

        case 1:
            return RoadRenderVariant::DeadEnd;

        case 2:
            if ((hasNorth && hasSouth) || (hasEast && hasWest)) {
                return RoadRenderVariant::Straight;
            }

            return RoadRenderVariant::Corner;

        case 3:
            return RoadRenderVariant::Tee;

        default:
            return RoadRenderVariant::Cross;
    }
}

// Maps road family and topology to an atlas base glyph.
RoadBaseGlyph ChooseBaseGlyph(RoadFamily family, RoadRenderVariant renderVariant, std::uint8_t junctionMask) {
    if (family == RoadFamily::None) {
        return RoadBaseGlyph::None;
    }

    const bool hasNorth = (junctionMask & kRoadDirectionNorth) != 0;
    const bool hasEast = (junctionMask & kRoadDirectionEast) != 0;
    const bool hasSouth = (junctionMask & kRoadDirectionSouth) != 0;
    const bool hasWest = (junctionMask & kRoadDirectionWest) != 0;
    const bool isLocalStreet = family == RoadFamily::LocalStreet;

    switch (renderVariant) {
        case RoadRenderVariant::Isolated:
            return isLocalStreet ? RoadBaseGlyph::LocalIsolated : RoadBaseGlyph::HighwayIsolated;

        case RoadRenderVariant::DeadEnd:
            if (hasNorth) {
                return isLocalStreet ? RoadBaseGlyph::LocalDeadEndNorth : RoadBaseGlyph::HighwayDeadEndNorth;
            }
            if (hasEast) {
                return isLocalStreet ? RoadBaseGlyph::LocalDeadEndEast : RoadBaseGlyph::HighwayDeadEndEast;
            }
            if (hasSouth) {
                return isLocalStreet ? RoadBaseGlyph::LocalDeadEndSouth : RoadBaseGlyph::HighwayDeadEndSouth;
            }
            if (hasWest) {
                return isLocalStreet ? RoadBaseGlyph::LocalDeadEndWest : RoadBaseGlyph::HighwayDeadEndWest;
            }
            return isLocalStreet ? RoadBaseGlyph::LocalIsolated : RoadBaseGlyph::HighwayIsolated;

        case RoadRenderVariant::Straight:
            if (hasNorth || hasSouth) {
                return isLocalStreet ? RoadBaseGlyph::LocalStraightVertical : RoadBaseGlyph::HighwayStraightVertical;
            }
            return isLocalStreet ? RoadBaseGlyph::LocalStraightHorizontal : RoadBaseGlyph::HighwayStraightHorizontal;

        case RoadRenderVariant::Corner:
            if (hasNorth && hasEast) {
                return isLocalStreet ? RoadBaseGlyph::LocalCornerNorthEast : RoadBaseGlyph::HighwayCornerNorthEast;
            }
            if (hasEast && hasSouth) {
                return isLocalStreet ? RoadBaseGlyph::LocalCornerSouthEast : RoadBaseGlyph::HighwayCornerSouthEast;
            }
            if (hasSouth && hasWest) {
                return isLocalStreet ? RoadBaseGlyph::LocalCornerSouthWest : RoadBaseGlyph::HighwayCornerSouthWest;
            }
            if (hasWest && hasNorth) {
                return isLocalStreet ? RoadBaseGlyph::LocalCornerNorthWest : RoadBaseGlyph::HighwayCornerNorthWest;
            }
            return isLocalStreet ? RoadBaseGlyph::LocalIsolated : RoadBaseGlyph::HighwayIsolated;

        case RoadRenderVariant::Tee:
            if (!hasNorth) {
                return isLocalStreet ? RoadBaseGlyph::LocalTeeMissingNorth : RoadBaseGlyph::HighwayTeeMissingNorth;
            }
            if (!hasEast) {
                return isLocalStreet ? RoadBaseGlyph::LocalTeeMissingEast : RoadBaseGlyph::HighwayTeeMissingEast;
            }
            if (!hasSouth) {
                return isLocalStreet ? RoadBaseGlyph::LocalTeeMissingSouth : RoadBaseGlyph::HighwayTeeMissingSouth;
            }
            return isLocalStreet ? RoadBaseGlyph::LocalTeeMissingWest : RoadBaseGlyph::HighwayTeeMissingWest;

        case RoadRenderVariant::Cross:
            return isLocalStreet ? RoadBaseGlyph::LocalCross : RoadBaseGlyph::HighwayCross;

        default:
            return RoadBaseGlyph::None;
    }
}

// Maps lane intent to the small directional overlay glyph.
RoadArrowGlyph ChooseArrowGlyph(std::uint8_t laneIntentMask) {
    const bool east = (laneIntentMask & kLaneIntentEast) != 0;
    const bool west = (laneIntentMask & kLaneIntentWest) != 0;
    const bool north = (laneIntentMask & kLaneIntentNorth) != 0;
    const bool south = (laneIntentMask & kLaneIntentSouth) != 0;

    if (north && east && !south && !west) {
        return RoadArrowGlyph::NorthEast;
    }
    if (south && east && !north && !west) {
        return RoadArrowGlyph::SouthEast;
    }
    if (south && west && !north && !east) {
        return RoadArrowGlyph::SouthWest;
    }
    if (north && west && !south && !east) {
        return RoadArrowGlyph::NorthWest;
    }
    if (north && !east && !south && !west) {
        return RoadArrowGlyph::North;
    }
    if (east && !north && !south && !west) {
        return RoadArrowGlyph::East;
    }
    if (south && !north && !east && !west) {
        return RoadArrowGlyph::South;
    }
    if (west && !north && !east && !south) {
        return RoadArrowGlyph::West;
    }

    return RoadArrowGlyph::None;
}
}

// Creates an empty transport network until map dimensions are known.
TransportNetwork::TransportNetwork()
    : width_(0),
      height_(0),
      totalTileCount_(0),
      chunkWidth_(1),
      chunkHeight_(1),
      chunksPerRow_(1),
      revision_(0) {
}

// Allocates transport layers and revision arrays for the active map.
void TransportNetwork::initialize(int width, int height, const std::vector<ChunkRect>& chunkLayout) {
    width_ = width;
    height_ = height;
    totalTileCount_ = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_);
    chunkLayout_ = chunkLayout;
    chunkWidth_ = chunkLayout_.empty() ? 1 : std::max(1, chunkLayout_[0].width);
    chunkHeight_ = chunkLayout_.empty() ? 1 : std::max(1, chunkLayout_[0].height);
    chunksPerRow_ = std::max(1, width_ / chunkWidth_);
    buildCells_.assign(totalTileCount_ * layerCount(), BuildRoadCell());
    resolvedCells_.assign(totalTileCount_ * layerCount(), ResolvedRoadCell());
    groundRoadRenderState_.assign(totalTileCount_ * kGroundRoadRenderChannelsPerTile, 0);
    groundChunkRevisions_.assign(chunkLayout_.size(), 1);
    elevatedChunkRevisions_.assign(chunkLayout_.size(), 1);
    revision_ = 0;
}

// Resets all transport cells and render states.
void TransportNetwork::clear() {
    buildCells_.assign(totalTileCount_ * layerCount(), BuildRoadCell());
    resolvedCells_.assign(totalTileCount_ * layerCount(), ResolvedRoadCell());
    groundRoadRenderState_.assign(totalTileCount_ * kGroundRoadRenderChannelsPerTile, 0);
    groundChunkRevisions_.assign(chunkLayout_.size(), 1);
    elevatedChunkRevisions_.assign(chunkLayout_.size(), 1);
    revision_ = 0;
}

// Applies a two-leg road stroke and resolves only affected topology.
bool TransportNetwork::placeRoadStroke(const RoadStrokeCommand& roadStrokeCommand, const std::vector<int>& lotOccupancy, int invalidLotId) {
    if (roadStrokeCommand.operation != RoadStrokeOperation::Place || roadStrokeCommand.family == RoadFamily::None) {
        return false;
    }

    std::vector<PendingPlacement> placements;
    placements.reserve(4096);

    if (!appendLegPlacements(roadStrokeCommand.startTile, roadStrokeCommand.cornerTile, placements)) {
        return false;
    }

    if (!appendLegPlacements(roadStrokeCommand.cornerTile, roadStrokeCommand.endTile, placements)) {
        return false;
    }

    if (placements.empty()) {
        return false;
    }

    const std::size_t layerOffset = static_cast<std::size_t>(roadStrokeCommand.layer) * totalTileCount_;
    bool madeChange = false;
    std::size_t placementIndex = 0;
    for (; placementIndex < placements.size(); ++placementIndex) {
        const PendingPlacement& placement = placements[placementIndex];
        if (roadStrokeCommand.layer == TransportLayerId::Ground && lotOccupancy[placement.tileIndex] != invalidLotId) {
            return false;
        }

        const std::size_t slot = layerOffset + static_cast<std::size_t>(placement.tileIndex);
        const BuildRoadCell& existingCell = buildCells_[slot];
        const RoadFamily existingFamily = static_cast<RoadFamily>(existingCell.family);
        if (existingFamily != RoadFamily::None && existingFamily != roadStrokeCommand.family) {
            return false;
        }

        if (existingCell.family != static_cast<std::uint8_t>(roadStrokeCommand.family) || (existingCell.laneIntentMask & placement.laneIntentMask) != placement.laneIntentMask) {
            madeChange = true;
        }
    }

    if (!madeChange) {
        return true;
    }

    std::vector<int> dirtyTileIndices;
    dirtyTileIndices.reserve(placements.size() * 9);
    std::vector<int> dirtyChunkIndices;

    for (placementIndex = 0; placementIndex < placements.size(); ++placementIndex) {
        const PendingPlacement& placement = placements[placementIndex];
        const std::size_t slot = layerOffset + static_cast<std::size_t>(placement.tileIndex);
        BuildRoadCell& buildCell = buildCells_[slot];
        buildCell.family = static_cast<std::uint8_t>(roadStrokeCommand.family);
        buildCell.laneIntentMask |= placement.laneIntentMask;

        int neighborTileY = placement.tileY - 1;
        for (; neighborTileY <= placement.tileY + 1; ++neighborTileY) {
            int neighborTileX = placement.tileX - 1;
            for (; neighborTileX <= placement.tileX + 1; ++neighborTileX) {
                if (!isTileInsideMap(neighborTileX, neighborTileY)) {
                    continue;
                }

                dirtyTileIndices.push_back(tileIndex(neighborTileX, neighborTileY));
            }
        }
    }

    std::sort(dirtyTileIndices.begin(), dirtyTileIndices.end());
    dirtyTileIndices.erase(std::unique(dirtyTileIndices.begin(), dirtyTileIndices.end()), dirtyTileIndices.end());
    dirtyChunkIndices.reserve(dirtyTileIndices.size());

    std::size_t dirtyIndex = 0;
    for (; dirtyIndex < dirtyTileIndices.size(); ++dirtyIndex) {
        const int dirtyTileIndex = dirtyTileIndices[dirtyIndex];
        const int tileY = dirtyTileIndex / width_;
        const int tileX = dirtyTileIndex - (tileY * width_);
        resolveDirtyTile(roadStrokeCommand.layer, tileX, tileY);

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

        if (roadStrokeCommand.layer == TransportLayerId::Ground) {
            ++groundChunkRevisions_[static_cast<std::size_t>(dirtyChunkIndex)];
        } else if (roadStrokeCommand.layer == TransportLayerId::Elevated) {
            ++elevatedChunkRevisions_[static_cast<std::size_t>(dirtyChunkIndex)];
        }
    }

    ++revision_;
    return true;
}

// Returns resolved per-layer road cells for publication.
const std::vector<ResolvedRoadCell>& TransportNetwork::resolvedCells() const {
    return resolvedCells_;
}

// Returns packed ground-road atlas indices for tile-pass rendering.
const std::vector<std::uint8_t>& TransportNetwork::groundRoadRenderState() const {
    return groundRoadRenderState_;
}

// Returns per-chunk revisions for ground-road render texture updates.
const std::vector<std::uint64_t>& TransportNetwork::groundChunkRevisions() const {
    return groundChunkRevisions_;
}

// Returns per-chunk revisions for elevated-road buffer updates.
const std::vector<std::uint64_t>& TransportNetwork::elevatedChunkRevisions() const {
    return elevatedChunkRevisions_;
}

// Returns the whole-network revision for publication snapshots.
std::uint64_t TransportNetwork::revision() const {
    return revision_;
}

// Checks whether a layer has a road family on a tile.
bool TransportNetwork::hasOccupancy(TransportLayerId layer, int tileIndexValue) const {
    const std::size_t slot = slotIndex(layer, tileIndexValue, totalTileCount_);
    if (slot >= buildCells_.size()) {
        return false;
    }

    return buildCells_[slot].family != static_cast<std::uint8_t>(RoadFamily::None) && buildCells_[slot].laneIntentMask != 0;
}

// Checks whether ground roads block lot placement on a tile.
bool TransportNetwork::hasGroundOccupancy(int tileIndexValue) const {
    return hasOccupancy(TransportLayerId::Ground, tileIndexValue);
}

// Returns the map width owned by the transport network.
int TransportNetwork::width() const {
    return width_;
}

// Returns the map height owned by the transport network.
int TransportNetwork::height() const {
    return height_;
}

// Returns the map tile count used for layer offsets.
std::size_t TransportNetwork::totalTileCount() const {
    return totalTileCount_;
}

// Returns the number of transport layers in packed storage.
std::size_t TransportNetwork::layerCount() {
    return static_cast<std::size_t>(TransportLayerId::Count);
}

// Converts a layer and tile index to packed transport storage.
std::size_t TransportNetwork::slotIndex(TransportLayerId layer, int tileIndexValue, std::size_t totalTileCountValue) {
    return static_cast<std::size_t>(layer) * totalTileCountValue + static_cast<std::size_t>(tileIndexValue);
}

// Validates a tile coordinate against transport map bounds.
bool TransportNetwork::isTileInsideMap(int tileX, int tileY) const {
    return tileX >= 0 && tileX < width_ && tileY >= 0 && tileY < height_;
}

// Converts a tile coordinate to row-major transport index.
int TransportNetwork::tileIndex(int tileX, int tileY) const {
    return (tileY * width_) + tileX;
}

// Maps a tile coordinate to the shared chunk layout.
int TransportNetwork::chunkIndexForTile(int tileX, int tileY) const {
    if (chunkLayout_.empty()) {
        return -1;
    }

    const int chunkX = tileX / chunkWidth_;
    const int chunkY = tileY / chunkHeight_;
    return chunkY * chunksPerRow_ + chunkX;
}

// Expands an axis-aligned drag leg into per-tile lane-intent placements.
bool TransportNetwork::appendLegPlacements(const Int2& startTile, const Int2& endTile, std::vector<PendingPlacement>& placements) const {
    if (startTile == endTile) {
        if (!appendPlacement(startTile.x, startTile.y, kLaneIntentEast, placements)) {
            return false;
        }

        return appendPlacement(startTile.x, startTile.y + 1, kLaneIntentWest, placements);
    }

    if (startTile.y == endTile.y) {
        const int minimumX = std::min(startTile.x, endTile.x);
        const int maximumX = std::max(startTile.x, endTile.x);
        int tileX = minimumX;
        for (; tileX <= maximumX; ++tileX) {
            if (!appendPlacement(tileX, startTile.y, kLaneIntentEast, placements)) {
                return false;
            }

            if (!appendPlacement(tileX, startTile.y + 1, kLaneIntentWest, placements)) {
                return false;
            }
        }

        return true;
    }

    if (startTile.x == endTile.x) {
        const int minimumY = std::min(startTile.y, endTile.y);
        const int maximumY = std::max(startTile.y, endTile.y);
        int tileY = minimumY;
        for (; tileY <= maximumY; ++tileY) {
            if (!appendPlacement(startTile.x, tileY, kLaneIntentSouth, placements)) {
                return false;
            }

            if (!appendPlacement(startTile.x + 1, tileY, kLaneIntentNorth, placements)) {
                return false;
            }
        }

        return true;
    }

    return false;
}

// Adds or merges one tile placement inside a pending stroke.
bool TransportNetwork::appendPlacement(int tileX, int tileY, std::uint8_t laneIntentMask, std::vector<PendingPlacement>& placements) const {
    if (!isTileInsideMap(tileX, tileY)) {
        return false;
    }

    std::size_t placementIndex = 0;
    for (; placementIndex < placements.size(); ++placementIndex) {
        if (placements[placementIndex].tileX != tileX || placements[placementIndex].tileY != tileY) {
            continue;
        }

        placements[placementIndex].laneIntentMask |= laneIntentMask;
        return true;
    }

    PendingPlacement placement;
    placement.tileX = tileX;
    placement.tileY = tileY;
    placement.tileIndex = tileIndex(tileX, tileY);
    placement.laneIntentMask = laneIntentMask;
    placements.push_back(placement);
    return true;
}

// Rebuilds one resolved road cell and its render-state bytes.
void TransportNetwork::resolveDirtyTile(TransportLayerId layer, int tileX, int tileY) {
    const int tileIndexValue = tileIndex(tileX, tileY);
    const std::size_t slot = slotIndex(layer, tileIndexValue, totalTileCount_);
    const BuildRoadCell& buildCell = buildCells_[slot];
    const std::size_t groundRenderOffset = static_cast<std::size_t>(tileIndexValue) * kGroundRoadRenderChannelsPerTile;

    ResolvedRoadCell resolvedCell;
    if (buildCell.family == static_cast<std::uint8_t>(RoadFamily::None) || buildCell.laneIntentMask == 0) {
        resolvedCells_[slot] = resolvedCell;
        if (layer == TransportLayerId::Ground) {
            groundRoadRenderState_[groundRenderOffset + 0u] = 0;
            groundRoadRenderState_[groundRenderOffset + 1u] = 0;
        }
        return;
    }

    const RoadFamily family = static_cast<RoadFamily>(buildCell.family);
    std::uint8_t junctionMask = 0;
    if (hasSameFamilyNeighbor(layer, tileX, tileY - 1, family)) {
        junctionMask |= kRoadDirectionNorth;
    }

    if (hasSameFamilyNeighbor(layer, tileX + 1, tileY, family)) {
        junctionMask |= kRoadDirectionEast;
    }

    if (hasSameFamilyNeighbor(layer, tileX, tileY + 1, family)) {
        junctionMask |= kRoadDirectionSouth;
    }

    if (hasSameFamilyNeighbor(layer, tileX - 1, tileY, family)) {
        junctionMask |= kRoadDirectionWest;
    }

    resolvedCell.family = buildCell.family;
    resolvedCell.laneIntentMask = buildCell.laneIntentMask;
    resolvedCell.junctionMask = junctionMask;
    resolvedCell.renderVariant = static_cast<std::uint8_t>(ChooseRenderVariant(junctionMask));
    resolvedCell.sidewalkMask = family == RoadFamily::LocalStreet ? static_cast<std::uint8_t>((~junctionMask) & 0x0F) : 0;
    resolvedCell.exitMask = buildExitMask(layer, tileX, tileY, family, buildCell.laneIntentMask);
    resolvedCell.baseGlyph = static_cast<std::uint8_t>(ChooseBaseGlyph(family, static_cast<RoadRenderVariant>(resolvedCell.renderVariant), junctionMask));
    resolvedCell.arrowGlyph = static_cast<std::uint8_t>(ChooseArrowGlyph(buildCell.laneIntentMask));

    if (family == RoadFamily::LocalStreet) {
        resolvedCell.carCost = 10;
        resolvedCell.pedestrianCost = 12;
    } else if (family == RoadFamily::Highway) {
        resolvedCell.carCost = 4;
        resolvedCell.pedestrianCost = 0;
    }

    resolvedCells_[slot] = resolvedCell;
    if (layer == TransportLayerId::Ground) {
        groundRoadRenderState_[groundRenderOffset + 0u] = resolvedCell.baseGlyph;
        groundRoadRenderState_[groundRenderOffset + 1u] = resolvedCell.arrowGlyph;
    }
}

// Checks adjacent connectivity against the same road family.
bool TransportNetwork::hasSameFamilyNeighbor(TransportLayerId layer, int tileX, int tileY, RoadFamily family) const {
    if (!isTileInsideMap(tileX, tileY)) {
        return false;
    }

    const std::size_t slot = slotIndex(layer, tileIndex(tileX, tileY), totalTileCount_);
    const BuildRoadCell& buildCell = buildCells_[slot];
    return buildCell.family == static_cast<std::uint8_t>(family) && buildCell.laneIntentMask != 0;
}

// Combines lane intent and neighboring roads into topology exit bits.
std::uint8_t TransportNetwork::buildExitMask(TransportLayerId layer, int tileX, int tileY, RoadFamily family, std::uint8_t laneIntentMask) const {
    std::uint8_t exitMask = 0;

    const bool northNeighbor = hasSameFamilyNeighbor(layer, tileX, tileY - 1, family);
    const bool eastNeighbor = hasSameFamilyNeighbor(layer, tileX + 1, tileY, family);
    const bool southNeighbor = hasSameFamilyNeighbor(layer, tileX, tileY + 1, family);
    const bool westNeighbor = hasSameFamilyNeighbor(layer, tileX - 1, tileY, family);

    if (northNeighbor && (laneIntentMask & (kLaneIntentNorth | kLaneIntentEast | kLaneIntentWest)) != 0) {
        exitMask |= kRoadDirectionNorth;
    }

    if (eastNeighbor && (laneIntentMask & (kLaneIntentEast | kLaneIntentNorth | kLaneIntentSouth)) != 0) {
        exitMask |= kRoadDirectionEast;
    }

    if (southNeighbor && (laneIntentMask & (kLaneIntentSouth | kLaneIntentEast | kLaneIntentWest)) != 0) {
        exitMask |= kRoadDirectionSouth;
    }

    if (westNeighbor && (laneIntentMask & (kLaneIntentWest | kLaneIntentNorth | kLaneIntentSouth)) != 0) {
        exitMask |= kRoadDirectionWest;
    }

    if ((exitMask & (kRoadDirectionNorth | kRoadDirectionEast)) == (kRoadDirectionNorth | kRoadDirectionEast) && hasSameFamilyNeighbor(layer, tileX + 1, tileY - 1, family)) {
        exitMask |= kRoadDirectionNorthEast;
    }

    if ((exitMask & (kRoadDirectionSouth | kRoadDirectionEast)) == (kRoadDirectionSouth | kRoadDirectionEast) && hasSameFamilyNeighbor(layer, tileX + 1, tileY + 1, family)) {
        exitMask |= kRoadDirectionSouthEast;
    }

    if ((exitMask & (kRoadDirectionSouth | kRoadDirectionWest)) == (kRoadDirectionSouth | kRoadDirectionWest) && hasSameFamilyNeighbor(layer, tileX - 1, tileY + 1, family)) {
        exitMask |= kRoadDirectionSouthWest;
    }

    if ((exitMask & (kRoadDirectionNorth | kRoadDirectionWest)) == (kRoadDirectionNorth | kRoadDirectionWest) && hasSameFamilyNeighbor(layer, tileX - 1, tileY - 1, family)) {
        exitMask |= kRoadDirectionNorthWest;
    }

    return exitMask;
}
