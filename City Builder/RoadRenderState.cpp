#include "RoadRenderState.h"

RoadRenderState::RoadRenderState()
    : variant(RoadRenderVariant::None),
      baseGlyph(RoadBaseGlyph::None),
      arrowGlyph(RoadArrowGlyph::None),
      laneGraphicMask(0),
      dividerMask(0) {
}

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

std::uint8_t PackLaneGraphicMask(std::uint8_t sidewalkEdges, std::uint8_t crosswalkEdges) {
    return static_cast<std::uint8_t>((sidewalkEdges & kRoadSurfaceSidewalkEdgeMask) | ((crosswalkEdges & kRoadSurfaceSidewalkEdgeMask) << kRoadSurfaceCrosswalkShift));
}

std::uint8_t PackDividerMask(std::uint8_t sameDirectionEdges, std::uint8_t opposingDirectionEdges) {
    return static_cast<std::uint8_t>((sameDirectionEdges << kRoadDividerWhiteShift) | (opposingDirectionEdges << kRoadDividerYellowShift));
}
