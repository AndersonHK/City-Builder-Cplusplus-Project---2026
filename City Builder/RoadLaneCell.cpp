#include "RoadLaneCell.h"

#include <cstddef>

#include "RoadRenderState.h"

namespace {
int CountCardinalDirections(std::uint8_t directionMask);

std::uint8_t OppositeMask(std::uint8_t directionMask) {
    std::uint8_t oppositeMask = 0;
    if ((directionMask & kRoadDirectionNorth) != 0) {
        oppositeMask |= kRoadDirectionSouth;
    }
    if ((directionMask & kRoadDirectionEast) != 0) {
        oppositeMask |= kRoadDirectionWest;
    }
    if ((directionMask & kRoadDirectionSouth) != 0) {
        oppositeMask |= kRoadDirectionNorth;
    }
    if ((directionMask & kRoadDirectionWest) != 0) {
        oppositeMask |= kRoadDirectionEast;
    }
    return oppositeMask;
}

std::uint8_t CrosswalkMaskForCell(const Lane& primary, std::uint8_t secondaryEdgeMask) {
    const std::uint8_t centerMask = primary.centerMask();
    const std::uint8_t graphicMask = primary.directionMask & kRoadSurfaceSidewalkEdgeMask;
    const std::uint8_t outgoingMask = static_cast<std::uint8_t>((primary.travelDirectionMask != 0 ? primary.travelDirectionMask : graphicMask) & graphicMask);
    const std::uint8_t incomingMask = static_cast<std::uint8_t>(graphicMask & ~outgoingMask);
    const std::uint8_t pathMask = static_cast<std::uint8_t>((primary.pathDirectionMask != 0 ? primary.pathDirectionMask : outgoingMask) & graphicMask);
    if (CountCardinalDirections(outgoingMask) != 1) {
        return 0;
    }

    const std::uint8_t sidewalkDetectMask = static_cast<std::uint8_t>(pathMask | incomingMask);
    return static_cast<std::uint8_t>(secondaryEdgeMask & sidewalkDetectMask & OppositeMask(centerMask));
}

std::uint8_t CenterSideForTravelDirection(std::uint8_t direction) {
    if (direction == kRoadDirectionNorth) {
        return kRoadDirectionWest;
    }
    if (direction == kRoadDirectionEast) {
        return kRoadDirectionNorth;
    }
    if (direction == kRoadDirectionSouth) {
        return kRoadDirectionEast;
    }
    if (direction == kRoadDirectionWest) {
        return kRoadDirectionSouth;
    }
    return 0;
}

int CountCardinalDirections(std::uint8_t directionMask) {
    const std::uint8_t directions[] = {
        kRoadDirectionNorth,
        kRoadDirectionEast,
        kRoadDirectionSouth,
        kRoadDirectionWest
    };
    int directionCount = 0;
    for (std::size_t directionIndex = 0; directionIndex < sizeof(directions) / sizeof(directions[0]); ++directionIndex) {
        if ((directionMask & directions[directionIndex]) == 0) {
            continue;
        }

        ++directionCount;
    }
    return directionCount;
}
}

Lane::Lane()
    : mode(CommuterMode::None),
      laneType(RoadLaneTypeId::Separator),
      capacity(0),
      directionMask(0),
      travelDirectionMask(0),
      pathDirectionMask(0),
      centerSide(false),
      parallelGraphic(RoadGraphic::none()),
      crossingGraphic(RoadGraphic::none()) {
}

std::uint8_t Lane::centerMask() const {
    std::uint8_t mask = 0;
    const std::uint8_t graphicMask = directionMask & kRoadSurfaceSidewalkEdgeMask;
    const std::uint8_t outgoingMask = static_cast<std::uint8_t>((travelDirectionMask != 0 ? travelDirectionMask : graphicMask) & graphicMask);
    const std::uint8_t incomingMask = static_cast<std::uint8_t>(graphicMask & ~outgoingMask);
    if (CountCardinalDirections(outgoingMask) > 2 || CountCardinalDirections(incomingMask) > 2) {
        return 0;
    }

    const std::uint8_t directions[] = {
        kRoadDirectionNorth,
        kRoadDirectionEast,
        kRoadDirectionSouth,
        kRoadDirectionWest
    };
    for (std::size_t directionIndex = 0; directionIndex < sizeof(directions) / sizeof(directions[0]); ++directionIndex) {
        if ((outgoingMask & directions[directionIndex]) != 0) {
            mask |= CenterSideForTravelDirection(directions[directionIndex]);
        }
        if ((incomingMask & directions[directionIndex]) != 0) {
            mask |= CenterSideForTravelDirection(OppositeMask(directions[directionIndex]));
        }
    }
    return mask;
}

RoadLaneCell::RoadLaneCell() {
}

bool RoadLaneCell::hasSecondary() const {
    return secondary.mode != CommuterMode::None || secondary.parallelGraphic.primitive() != RoadGraphicPrimitive::None || secondary.crossingGraphic.primitive() != RoadGraphicPrimitive::None;
}

void RoadLaneCell::clearSecondary() {
    secondary = Lane();
}

std::uint8_t RoadLaneCell::secondaryEdgeMask() const {
    if (!hasSecondary()) {
        return 0;
    }

    const std::uint8_t centerMask = primary.centerMask();
    return secondary.centerSide ? centerMask : OppositeMask(centerMask);
}

void RoadLaneCell::applyGraphics(RoadRenderState& renderState) const {
    primary.parallelGraphic.applyToRenderState(renderState);
    if (!hasSecondary()) {
        return;
    }

    const std::uint8_t edgeMask = secondaryEdgeMask();
    const std::uint8_t crossingMask = secondary.crossingGraphic.primitive() == RoadGraphicPrimitive::None
        ? 0
        : CrosswalkMaskForCell(primary, edgeMask);
    secondary.parallelGraphic.applyToRenderState(renderState, static_cast<std::uint8_t>(edgeMask & ~crossingMask));
    secondary.crossingGraphic.applyToRenderState(renderState, crossingMask);
}
