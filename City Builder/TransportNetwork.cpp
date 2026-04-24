#include "TransportNetwork.h"

#include <algorithm>
#include <cmath>

namespace {
const float kRoadLayoutEpsilon = 0.0001f;

struct LayoutWidth {
    RoadElementKind kind;
    RoadLaneRole laneRole;
    float width;
    float minimumWidth;
    float maximumWidth;
    std::uint8_t laneTravelMask;

    LayoutWidth()
        : kind(RoadElementKind::Lane),
          laneRole(RoadLaneRole::Through),
          width(1.0f),
          minimumWidth(1.0f),
          maximumWidth(1.0f),
          laneTravelMask(0) {
    }
};

enum class RoadTemplateSeamKind : std::uint8_t {
    None = 0,
    SameDirectionLaneDivider,
    OpposingDirectionLaneDivider
};

struct CrossSectionTile {
    std::uint8_t elementMask;
    std::uint8_t laneTravelMask;
    std::uint8_t laneCount;
    bool lowSidewalkEdge;
    bool highSidewalkEdge;
    RoadTemplateSeamKind lowSeamKind;
    RoadTemplateSeamKind highSeamKind;

    CrossSectionTile()
        : elementMask(0),
          laneTravelMask(0),
          laneCount(0),
          lowSidewalkEdge(false),
          highSidewalkEdge(false),
          lowSeamKind(RoadTemplateSeamKind::None),
          highSeamKind(RoadTemplateSeamKind::None) {
    }
};

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

// Converts a road direction bit into the existing lane-travel bitset.
std::uint8_t LaneIntentFromRoadDirection(std::uint8_t roadDirection) {
    switch (roadDirection) {
        case kRoadDirectionNorth:
            return kLaneIntentNorth;
        case kRoadDirectionEast:
            return kLaneIntentEast;
        case kRoadDirectionSouth:
            return kLaneIntentSouth;
        case kRoadDirectionWest:
            return kLaneIntentWest;
        default:
            return 0;
    }
}

// Converts lane-travel bits into cardinal road-direction bits.
std::uint8_t RoadDirectionsFromLaneIntent(std::uint8_t laneIntentMask) {
    std::uint8_t roadDirectionMask = 0;
    if ((laneIntentMask & kLaneIntentNorth) != 0) {
        roadDirectionMask |= kRoadDirectionNorth;
    }
    if ((laneIntentMask & kLaneIntentEast) != 0) {
        roadDirectionMask |= kRoadDirectionEast;
    }
    if ((laneIntentMask & kLaneIntentSouth) != 0) {
        roadDirectionMask |= kRoadDirectionSouth;
    }
    if ((laneIntentMask & kLaneIntentWest) != 0) {
        roadDirectionMask |= kRoadDirectionWest;
    }

    return roadDirectionMask;
}

// Returns the opposite cardinal road direction bit.
std::uint8_t OppositeRoadDirection(std::uint8_t roadDirection) {
    switch (roadDirection) {
        case kRoadDirectionNorth:
            return kRoadDirectionSouth;
        case kRoadDirectionEast:
            return kRoadDirectionWest;
        case kRoadDirectionSouth:
            return kRoadDirectionNorth;
        case kRoadDirectionWest:
            return kRoadDirectionEast;
        default:
            return 0;
    }
}

// Converts an element kind into the authored element mask stored per tile.
std::uint8_t ElementMaskForKind(RoadElementKind kind) {
    switch (kind) {
        case RoadElementKind::Sidewalk:
            return kRoadElementSidewalk;
        case RoadElementKind::Lane:
            return kRoadElementLane;
        case RoadElementKind::Divider:
            return kRoadElementDivider;
        case RoadElementKind::Shoulder:
            return kRoadElementShoulder;
        default:
            return 0;
    }
}

// Converts an authored template seam into the compact render edge mask.
void PackRoadTemplateSeamEdge(std::uint8_t& dividerMask, std::uint8_t sideBit, RoadTemplateSeamKind seamKind) {
    if (seamKind == RoadTemplateSeamKind::SameDirectionLaneDivider) {
        dividerMask |= static_cast<std::uint8_t>(sideBit << kRoadDividerWhiteShift);
    } else if (seamKind == RoadTemplateSeamKind::OpposingDirectionLaneDivider) {
        dividerMask |= static_cast<std::uint8_t>(sideBit << kRoadDividerYellowShift);
    }
}

// Defines the one visual relationship between any two adjacent template members.
RoadTemplateSeamKind RoadTemplateSeamBetween(const LayoutWidth& first, const LayoutWidth& second) {
    if (first.kind != RoadElementKind::Lane || second.kind != RoadElementKind::Lane) {
        return RoadTemplateSeamKind::None;
    }

    if (first.laneRole != RoadLaneRole::Through || second.laneRole != RoadLaneRole::Through) {
        return RoadTemplateSeamKind::None;
    }

    if (first.laneTravelMask == 0 || second.laneTravelMask == 0) {
        return RoadTemplateSeamKind::None;
    }

    return first.laneTravelMask == second.laneTravelMask
        ? RoadTemplateSeamKind::SameDirectionLaneDivider
        : RoadTemplateSeamKind::OpposingDirectionLaneDivider;
}

// True when a travel mask has east/west lane continuity.
bool HasHorizontalLane(std::uint8_t laneTravelMask) {
    return (laneTravelMask & (kLaneIntentEast | kLaneIntentWest)) != 0;
}

// True when a travel mask has north/south lane continuity.
bool HasVerticalLane(std::uint8_t laneTravelMask) {
    return (laneTravelMask & (kLaneIntentNorth | kLaneIntentSouth)) != 0;
}

// Builds a tile footprint from template element widths, using min/max constraints.
int ChooseTemplateFootprint(const RoadTemplate& roadTemplate) {
    float preferredWidth = 0.0f;
    float minimumWidth = 0.0f;
    std::size_t elementIndex = 0;
    for (; elementIndex < roadTemplate.elements.size(); ++elementIndex) {
        preferredWidth += roadTemplate.elements[elementIndex].behavior.preferredWidth;
        minimumWidth += roadTemplate.elements[elementIndex].behavior.minimumWidth;
    }

    int footprint = static_cast<int>(std::floor(preferredWidth + 0.5f));
    footprint = std::max(1, footprint);
    footprint = std::max(footprint, static_cast<int>(std::ceil(minimumWidth - kRoadLayoutEpsilon)));
    return footprint;
}

// Distributes a whole-tile footprint across template elements with DOM-like flexing.
std::vector<LayoutWidth> BuildLayoutWidths(const RoadTemplate& roadTemplate, std::uint8_t forwardDirection, std::uint8_t reverseDirection, int footprint) {
    std::vector<LayoutWidth> widths;
    widths.reserve(roadTemplate.elements.size());

    int laneOrdinal = 0;
    int laneTotal = 0;
    std::size_t countIndex = 0;
    for (; countIndex < roadTemplate.elements.size(); ++countIndex) {
        if (roadTemplate.elements[countIndex].kind == RoadElementKind::Lane) {
            ++laneTotal;
        }
    }

    float totalWidth = 0.0f;
    std::size_t elementIndex = 0;
    for (; elementIndex < roadTemplate.elements.size(); ++elementIndex) {
        const RoadTemplateElement& element = roadTemplate.elements[elementIndex];
        LayoutWidth layoutWidth;
        layoutWidth.kind = element.kind;
        layoutWidth.laneRole = element.laneRole;
        layoutWidth.width = element.behavior.preferredWidth;
        layoutWidth.minimumWidth = element.behavior.minimumWidth;
        layoutWidth.maximumWidth = element.behavior.maximumWidth;
        if (element.kind == RoadElementKind::Lane) {
            std::uint8_t laneDirection = forwardDirection;
            if (roadTemplate.directionMode == RoadDirectionMode::TwoWay && laneOrdinal < laneTotal / 2) {
                laneDirection = reverseDirection;
            } else if (roadTemplate.directionMode == RoadDirectionMode::OneWayReverse) {
                laneDirection = reverseDirection;
            }

            layoutWidth.laneTravelMask = LaneIntentFromRoadDirection(laneDirection);
            ++laneOrdinal;
        }

        totalWidth += layoutWidth.width;
        widths.push_back(layoutWidth);
    }

    float remainingDelta = static_cast<float>(footprint) - totalWidth;
    for (int passIndex = 0; passIndex < 16 && std::fabs(remainingDelta) > kRoadLayoutEpsilon; ++passIndex) {
        float capacity = 0.0f;
        std::size_t widthIndex = 0;
        for (; widthIndex < widths.size(); ++widthIndex) {
            capacity += remainingDelta > 0.0f
                ? std::max(0.0f, widths[widthIndex].maximumWidth - widths[widthIndex].width)
                : std::max(0.0f, widths[widthIndex].width - widths[widthIndex].minimumWidth);
        }

        if (capacity <= kRoadLayoutEpsilon) {
            break;
        }

        float applied = 0.0f;
        for (widthIndex = 0; widthIndex < widths.size(); ++widthIndex) {
            const float elementCapacity = remainingDelta > 0.0f
                ? std::max(0.0f, widths[widthIndex].maximumWidth - widths[widthIndex].width)
                : std::max(0.0f, widths[widthIndex].width - widths[widthIndex].minimumWidth);
            if (elementCapacity <= kRoadLayoutEpsilon) {
                continue;
            }

            const float share = remainingDelta * (elementCapacity / capacity);
            const float clampedShare = remainingDelta > 0.0f ? std::min(share, elementCapacity) : std::max(share, -elementCapacity);
            widths[widthIndex].width += clampedShare;
            applied += clampedShare;
        }

        remainingDelta -= applied;
    }

    return widths;
}

// Assigns cross-section element spans to tile slots.
std::vector<CrossSectionTile> BuildCrossSectionTiles(const std::vector<LayoutWidth>& widths, int footprint) {
    std::vector<CrossSectionTile> tiles(static_cast<std::size_t>(footprint));
    float cursor = 0.0f;
    std::size_t widthIndex = 0;
    for (; widthIndex < widths.size(); ++widthIndex) {
        const LayoutWidth& width = widths[widthIndex];
        const float start = cursor;
        const float end = cursor + width.width;
        const int firstTile = std::max(0, static_cast<int>(std::floor(start + kRoadLayoutEpsilon)));
        const int lastTile = std::min(footprint - 1, static_cast<int>(std::ceil(end - kRoadLayoutEpsilon)) - 1);
        int tileOffset = firstTile;
        for (; tileOffset <= lastTile; ++tileOffset) {
            tiles[static_cast<std::size_t>(tileOffset)].elementMask |= ElementMaskForKind(width.kind);
            if (width.kind == RoadElementKind::Lane) {
                tiles[static_cast<std::size_t>(tileOffset)].laneTravelMask |= width.laneTravelMask;
                ++tiles[static_cast<std::size_t>(tileOffset)].laneCount;
            }
        }

        cursor = end;
    }

    if (!widths.empty()) {
        if (widths.front().kind == RoadElementKind::Sidewalk) {
            tiles.front().lowSidewalkEdge = true;
        }
        if (widths.back().kind == RoadElementKind::Sidewalk) {
            tiles.back().highSidewalkEdge = true;
        }
    }

    float boundary = 0.0f;
    for (widthIndex = 0; widthIndex + 1u < widths.size(); ++widthIndex) {
        boundary += widths[widthIndex].width;
        const RoadTemplateSeamKind seamKind = RoadTemplateSeamBetween(widths[widthIndex], widths[widthIndex + 1u]);
        if (seamKind == RoadTemplateSeamKind::None) {
            continue;
        }

        const float roundedBoundary = std::floor(boundary + 0.5f);
        if (std::fabs(boundary - roundedBoundary) <= 0.03f) {
            const int highTile = std::max(0, std::min(footprint - 1, static_cast<int>(roundedBoundary) - 1));
            const int lowTile = std::max(0, std::min(footprint - 1, static_cast<int>(roundedBoundary)));
            tiles[static_cast<std::size_t>(highTile)].highSeamKind = seamKind;
            tiles[static_cast<std::size_t>(lowTile)].lowSeamKind = seamKind;
        } else {
            const int containingTile = std::max(0, std::min(footprint - 1, static_cast<int>(std::floor(boundary))));
            if ((boundary - std::floor(boundary)) < 0.5f) {
                tiles[static_cast<std::size_t>(containingTile)].lowSeamKind = seamKind;
            } else {
                tiles[static_cast<std::size_t>(containingTile)].highSeamKind = seamKind;
            }
        }
    }

    return tiles;
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

    RoadTemplate roadTemplate = roadStrokeCommand.roadTemplate;
    roadTemplate.family = roadStrokeCommand.family;
    roadTemplate.layer = roadStrokeCommand.layer;
    if (roadTemplate.elements.empty()) {
        roadTemplate = makeRoadTemplate(roadStrokeCommand.family, roadStrokeCommand.layer, 1, RoadTrafficSide::RightHand, RoadDirectionMode::TwoWay);
    }

    std::vector<PendingPlacement> placements;
    placements.reserve(4096);

    if (!appendLegPlacements(roadStrokeCommand.startTile, roadStrokeCommand.cornerTile, roadTemplate, placements)) {
        return false;
    }

    if (!appendLegPlacements(roadStrokeCommand.cornerTile, roadStrokeCommand.endTile, roadTemplate, placements)) {
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

        if (existingCell.family != static_cast<std::uint8_t>(roadStrokeCommand.family) ||
            (existingCell.elementMask & placement.elementMask) != placement.elementMask ||
            (existingCell.laneTravelMask & placement.laneTravelMask) != placement.laneTravelMask ||
            (existingCell.sidewalkMask & placement.sidewalkMask) != placement.sidewalkMask ||
            (existingCell.dividerMask & placement.dividerMask) != placement.dividerMask ||
            existingCell.laneCount < placement.laneCount) {
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
        const bool addsNewLaneDirection = (placement.laneTravelMask & ~buildCell.laneTravelMask) != 0;
        buildCell.family = static_cast<std::uint8_t>(roadStrokeCommand.family);
        buildCell.elementMask |= placement.elementMask;
        buildCell.laneTravelMask |= placement.laneTravelMask;
        buildCell.sidewalkMask |= placement.sidewalkMask;
        buildCell.dividerMask |= placement.dividerMask;
        buildCell.laneCount = addsNewLaneDirection
            ? static_cast<std::uint8_t>(std::min(255, static_cast<int>(buildCell.laneCount) + static_cast<int>(placement.laneCount)))
            : std::max(buildCell.laneCount, placement.laneCount);

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

    return buildCells_[slot].family != static_cast<std::uint8_t>(RoadFamily::None) && buildCells_[slot].elementMask != 0;
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

// Builds a modular road sandwich template from current tool state.
RoadTemplate TransportNetwork::makeRoadTemplate(RoadFamily family, TransportLayerId layer, int laneCount, RoadTrafficSide trafficSide, RoadDirectionMode directionMode) {
    RoadTemplate roadTemplate;
    roadTemplate.family = family;
    roadTemplate.layer = layer;
    roadTemplate.trafficSide = trafficSide;
    roadTemplate.directionMode = directionMode;
    roadTemplate.laneCount = std::max(1, laneCount);

    RoadTemplateElement sidewalkElement;
    sidewalkElement.kind = RoadElementKind::Sidewalk;
    sidewalkElement.laneRole = RoadLaneRole::None;
    sidewalkElement.behavior.kind = RoadElementKind::Sidewalk;
    sidewalkElement.behavior.minimumWidth = family == RoadFamily::LocalStreet ? 0.18f : 0.0f;
    sidewalkElement.behavior.preferredWidth = family == RoadFamily::LocalStreet ? 0.25f : 0.0f;
    sidewalkElement.behavior.maximumWidth = family == RoadFamily::LocalStreet ? 0.45f : 0.0f;
    sidewalkElement.behavior.connectorMask = kRoadDirectionNorth | kRoadDirectionEast | kRoadDirectionSouth | kRoadDirectionWest;

    RoadTemplateElement laneElement;
    laneElement.kind = RoadElementKind::Lane;
    laneElement.laneRole = RoadLaneRole::Through;
    laneElement.behavior.kind = RoadElementKind::Lane;
    laneElement.behavior.minimumWidth = 0.60f;
    laneElement.behavior.preferredWidth = family == RoadFamily::Highway ? 0.90f : 0.75f;
    laneElement.behavior.maximumWidth = 1.0f;
    laneElement.behavior.connectorMask = kRoadDirectionNorth | kRoadDirectionEast | kRoadDirectionSouth | kRoadDirectionWest;

    if (sidewalkElement.behavior.preferredWidth > 0.0f) {
        roadTemplate.elements.push_back(sidewalkElement);
    }

    const int totalLaneElements = directionMode == RoadDirectionMode::TwoWay ? roadTemplate.laneCount * 2 : roadTemplate.laneCount;
    int laneIndex = 0;
    for (; laneIndex < totalLaneElements; ++laneIndex) {
        roadTemplate.elements.push_back(laneElement);
    }

    if (sidewalkElement.behavior.preferredWidth > 0.0f) {
        roadTemplate.elements.push_back(sidewalkElement);
    }

    return roadTemplate;
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

// Expands an axis-aligned drag leg into modular road-template placements.
bool TransportNetwork::appendLegPlacements(const Int2& startTile, const Int2& endTile, const RoadTemplate& roadTemplate, std::vector<PendingPlacement>& placements) const {
    if (startTile == endTile) {
        return true;
    }

    std::uint8_t forwardDirection = 0;
    std::uint8_t reverseDirection = 0;
    bool horizontal = false;
    if (startTile.y == endTile.y) {
        horizontal = true;
        forwardDirection = startTile.x <= endTile.x ? kRoadDirectionEast : kRoadDirectionWest;
        if (roadTemplate.directionMode == RoadDirectionMode::TwoWay) {
            forwardDirection = kRoadDirectionEast;
        }
        reverseDirection = OppositeRoadDirection(forwardDirection);
    } else if (startTile.x == endTile.x) {
        forwardDirection = startTile.y <= endTile.y ? kRoadDirectionSouth : kRoadDirectionNorth;
        if (roadTemplate.directionMode == RoadDirectionMode::TwoWay) {
            forwardDirection = kRoadDirectionNorth;
        }
        reverseDirection = OppositeRoadDirection(forwardDirection);
    } else {
        return false;
    }

    const int footprint = ChooseTemplateFootprint(roadTemplate);
    const std::vector<LayoutWidth> widths = BuildLayoutWidths(roadTemplate, forwardDirection, reverseDirection, footprint);
    std::vector<CrossSectionTile> crossSectionTiles = BuildCrossSectionTiles(widths, footprint);
    if (roadTemplate.trafficSide == RoadTrafficSide::LeftHand && roadTemplate.directionMode == RoadDirectionMode::TwoWay) {
        std::reverse(crossSectionTiles.begin(), crossSectionTiles.end());
    }

    const int minimumX = std::min(startTile.x, endTile.x);
    const int maximumX = std::max(startTile.x, endTile.x);
    const int minimumY = std::min(startTile.y, endTile.y);
    const int maximumY = std::max(startTile.y, endTile.y);

    int longitudinal = horizontal ? minimumX : minimumY;
    const int longitudinalEnd = horizontal ? maximumX : maximumY;
    for (; longitudinal <= longitudinalEnd; ++longitudinal) {
        int crossOffset = 0;
        for (; crossOffset < footprint; ++crossOffset) {
            const CrossSectionTile& crossSectionTile = crossSectionTiles[static_cast<std::size_t>(crossOffset)];
            if (crossSectionTile.elementMask == 0) {
                continue;
            }

            const int tileX = horizontal ? longitudinal : startTile.x + crossOffset;
            const int tileY = horizontal ? startTile.y + crossOffset : longitudinal;
            std::uint8_t sidewalkMask = 0;
            std::uint8_t dividerMask = 0;
            const std::uint8_t lowSideBit = horizontal ? kRoadDirectionNorth : kRoadDirectionWest;
            const std::uint8_t highSideBit = horizontal ? kRoadDirectionSouth : kRoadDirectionEast;
            if (crossSectionTile.lowSidewalkEdge) {
                sidewalkMask |= lowSideBit;
            }
            if (crossSectionTile.highSidewalkEdge) {
                sidewalkMask |= highSideBit;
            }
            PackRoadTemplateSeamEdge(dividerMask, lowSideBit, crossSectionTile.lowSeamKind);
            PackRoadTemplateSeamEdge(dividerMask, highSideBit, crossSectionTile.highSeamKind);

            if (!appendPlacement(tileX, tileY, crossSectionTile.elementMask, crossSectionTile.laneTravelMask, crossSectionTile.laneCount, sidewalkMask, dividerMask, placements)) {
                return false;
            }
        }
    }

    return true;
}

// Adds or merges one tile placement inside a pending stroke.
bool TransportNetwork::appendPlacement(int tileX, int tileY, std::uint8_t elementMask, std::uint8_t laneTravelMask, std::uint8_t laneCount, std::uint8_t sidewalkMask, std::uint8_t dividerMask, std::vector<PendingPlacement>& placements) const {
    if (!isTileInsideMap(tileX, tileY)) {
        return false;
    }

    std::size_t placementIndex = 0;
    for (; placementIndex < placements.size(); ++placementIndex) {
        if (placements[placementIndex].tileX != tileX || placements[placementIndex].tileY != tileY) {
            continue;
        }

        const bool addsNewLaneDirection = (laneTravelMask & ~placements[placementIndex].laneTravelMask) != 0;
        placements[placementIndex].elementMask |= elementMask;
        placements[placementIndex].laneTravelMask |= laneTravelMask;
        placements[placementIndex].sidewalkMask |= sidewalkMask;
        placements[placementIndex].dividerMask |= dividerMask;
        placements[placementIndex].laneCount = addsNewLaneDirection
            ? static_cast<std::uint8_t>(std::min(255, static_cast<int>(placements[placementIndex].laneCount) + static_cast<int>(laneCount)))
            : std::max(placements[placementIndex].laneCount, laneCount);
        return true;
    }

    PendingPlacement placement;
    placement.tileX = tileX;
    placement.tileY = tileY;
    placement.tileIndex = tileIndex(tileX, tileY);
    placement.elementMask = elementMask;
    placement.laneTravelMask = laneTravelMask;
    placement.laneCount = laneCount;
    placement.sidewalkMask = sidewalkMask;
    placement.dividerMask = dividerMask;
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
    if (buildCell.family == static_cast<std::uint8_t>(RoadFamily::None) || buildCell.elementMask == 0) {
        resolvedCells_[slot] = resolvedCell;
        if (layer == TransportLayerId::Ground) {
            groundRoadRenderState_[groundRenderOffset + 0u] = 0;
            groundRoadRenderState_[groundRenderOffset + 1u] = 0;
            groundRoadRenderState_[groundRenderOffset + 2u] = 0;
            groundRoadRenderState_[groundRenderOffset + 3u] = 0;
        }
        return;
    }

    const RoadFamily family = static_cast<RoadFamily>(buildCell.family);
    std::uint8_t junctionMask = 0;
    if (HasVerticalLane(buildCell.laneTravelMask) && hasSameFamilyNeighbor(layer, tileX, tileY - 1, family)) {
        const std::size_t neighborSlot = slotIndex(layer, tileIndex(tileX, tileY - 1), totalTileCount_);
        if (HasVerticalLane(buildCells_[neighborSlot].laneTravelMask)) {
            junctionMask |= kRoadDirectionNorth;
        }
    }

    if (HasHorizontalLane(buildCell.laneTravelMask) && hasSameFamilyNeighbor(layer, tileX + 1, tileY, family)) {
        const std::size_t neighborSlot = slotIndex(layer, tileIndex(tileX + 1, tileY), totalTileCount_);
        if (HasHorizontalLane(buildCells_[neighborSlot].laneTravelMask)) {
            junctionMask |= kRoadDirectionEast;
        }
    }

    if (HasVerticalLane(buildCell.laneTravelMask) && hasSameFamilyNeighbor(layer, tileX, tileY + 1, family)) {
        const std::size_t neighborSlot = slotIndex(layer, tileIndex(tileX, tileY + 1), totalTileCount_);
        if (HasVerticalLane(buildCells_[neighborSlot].laneTravelMask)) {
            junctionMask |= kRoadDirectionSouth;
        }
    }

    if (HasHorizontalLane(buildCell.laneTravelMask) && hasSameFamilyNeighbor(layer, tileX - 1, tileY, family)) {
        const std::size_t neighborSlot = slotIndex(layer, tileIndex(tileX - 1, tileY), totalTileCount_);
        if (HasHorizontalLane(buildCells_[neighborSlot].laneTravelMask)) {
            junctionMask |= kRoadDirectionWest;
        }
    }

    const std::uint8_t exitMask = buildExitMask(layer, tileX, tileY, family, buildCell.laneTravelMask);
    if ((exitMask & kRoadDirectionNorth) != 0) {
        junctionMask |= kRoadDirectionNorth;
    }

    if ((exitMask & kRoadDirectionEast) != 0) {
        junctionMask |= kRoadDirectionEast;
    }

    if ((exitMask & kRoadDirectionSouth) != 0) {
        junctionMask |= kRoadDirectionSouth;
    }

    if ((exitMask & kRoadDirectionWest) != 0) {
        junctionMask |= kRoadDirectionWest;
    }

    resolvedCell.family = buildCell.family;
    resolvedCell.laneIntentMask = buildCell.laneTravelMask;
    resolvedCell.elementMask = buildCell.elementMask;
    resolvedCell.laneCount = buildCell.laneCount;
    resolvedCell.junctionMask = junctionMask;
    resolvedCell.renderVariant = static_cast<std::uint8_t>(ChooseRenderVariant(junctionMask));
    resolvedCell.sidewalkMask = buildCell.sidewalkMask;
    resolvedCell.dividerMask = buildCell.dividerMask;
    if (HasHorizontalLane(buildCell.laneTravelMask) && HasVerticalLane(buildCell.laneTravelMask)) {
        resolvedCell.dividerMask = 0;
    }
    resolvedCell.exitMask = exitMask;
    resolvedCell.baseGlyph = static_cast<std::uint8_t>(ChooseBaseGlyph(family, static_cast<RoadRenderVariant>(resolvedCell.renderVariant), junctionMask));
    resolvedCell.arrowGlyph = static_cast<std::uint8_t>(ChooseArrowGlyph(buildCell.laneTravelMask));

    if ((buildCell.elementMask & kRoadElementLane) != 0 && family == RoadFamily::LocalStreet) {
        resolvedCell.carCost = 10;
    } else if ((buildCell.elementMask & kRoadElementLane) != 0 && family == RoadFamily::Highway) {
        resolvedCell.carCost = 4;
    }

    if ((buildCell.elementMask & kRoadElementSidewalk) != 0) {
        resolvedCell.pedestrianCost = 12;
    }

    resolvedCells_[slot] = resolvedCell;
    if (layer == TransportLayerId::Ground) {
        groundRoadRenderState_[groundRenderOffset + 0u] = resolvedCell.baseGlyph;
        groundRoadRenderState_[groundRenderOffset + 1u] = resolvedCell.arrowGlyph;
        groundRoadRenderState_[groundRenderOffset + 2u] = resolvedCell.sidewalkMask;
        groundRoadRenderState_[groundRenderOffset + 3u] = resolvedCell.dividerMask;
    }
}

// Checks adjacent connectivity against the same road family.
bool TransportNetwork::hasSameFamilyNeighbor(TransportLayerId layer, int tileX, int tileY, RoadFamily family) const {
    if (!isTileInsideMap(tileX, tileY)) {
        return false;
    }

    const std::size_t slot = slotIndex(layer, tileIndex(tileX, tileY), totalTileCount_);
    const BuildRoadCell& buildCell = buildCells_[slot];
    return buildCell.family == static_cast<std::uint8_t>(family) && buildCell.elementMask != 0;
}

// Checks whether an adjacent road lane can carry travel through the requested side.
bool TransportNetwork::hasCompatibleLaneNeighbor(TransportLayerId layer, int tileX, int tileY, RoadFamily family, std::uint8_t directionBit) const {
    if (!isTileInsideMap(tileX, tileY)) {
        return false;
    }

    const std::size_t slot = slotIndex(layer, tileIndex(tileX, tileY), totalTileCount_);
    const BuildRoadCell& buildCell = buildCells_[slot];
    return buildCell.family == static_cast<std::uint8_t>(family) &&
        (buildCell.laneTravelMask & LaneIntentFromRoadDirection(directionBit)) != 0;
}

// Combines lane travel and neighboring compatible lanes into pathfinding exit bits.
std::uint8_t TransportNetwork::buildExitMask(TransportLayerId layer, int tileX, int tileY, RoadFamily family, std::uint8_t laneTravelMask) const {
    std::uint8_t exitMask = 0;

    if ((laneTravelMask & kLaneIntentNorth) != 0 && hasCompatibleLaneNeighbor(layer, tileX, tileY - 1, family, kRoadDirectionNorth)) {
        exitMask |= kRoadDirectionNorth;
    }

    if ((laneTravelMask & kLaneIntentEast) != 0 && hasCompatibleLaneNeighbor(layer, tileX + 1, tileY, family, kRoadDirectionEast)) {
        exitMask |= kRoadDirectionEast;
    }

    if ((laneTravelMask & kLaneIntentSouth) != 0 && hasCompatibleLaneNeighbor(layer, tileX, tileY + 1, family, kRoadDirectionSouth)) {
        exitMask |= kRoadDirectionSouth;
    }

    if ((laneTravelMask & kLaneIntentWest) != 0 && hasCompatibleLaneNeighbor(layer, tileX - 1, tileY, family, kRoadDirectionWest)) {
        exitMask |= kRoadDirectionWest;
    }

    const std::uint8_t travelDirections = RoadDirectionsFromLaneIntent(laneTravelMask);
    if ((travelDirections & kRoadDirectionNorth) != 0 &&
        (travelDirections & kRoadDirectionEast) != 0 &&
        hasCompatibleLaneNeighbor(layer, tileX + 1, tileY - 1, family, kRoadDirectionNorth)) {
        exitMask |= kRoadDirectionNorthEast;
    }

    if ((travelDirections & kRoadDirectionSouth) != 0 &&
        (travelDirections & kRoadDirectionEast) != 0 &&
        hasCompatibleLaneNeighbor(layer, tileX + 1, tileY + 1, family, kRoadDirectionSouth)) {
        exitMask |= kRoadDirectionSouthEast;
    }

    if ((travelDirections & kRoadDirectionSouth) != 0 &&
        (travelDirections & kRoadDirectionWest) != 0 &&
        hasCompatibleLaneNeighbor(layer, tileX - 1, tileY + 1, family, kRoadDirectionSouth)) {
        exitMask |= kRoadDirectionSouthWest;
    }

    if ((travelDirections & kRoadDirectionNorth) != 0 &&
        (travelDirections & kRoadDirectionWest) != 0 &&
        hasCompatibleLaneNeighbor(layer, tileX - 1, tileY - 1, family, kRoadDirectionNorth)) {
        exitMask |= kRoadDirectionNorthWest;
    }

    return exitMask;
}
