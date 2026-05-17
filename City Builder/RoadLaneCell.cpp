#include "RoadLaneCell.h"

#include <cassert>
#include <cstddef>

#include "RoadRenderState.h"

namespace {
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
      capacity(0),
      directionMask(0),
      travelDirectionMask(0),
      centerSide(false),
      parallelGraphic(RoadGraphic::none()),
      crossingGraphic(RoadGraphic::none()) {
}

std::uint8_t Lane::centerMask() const {
    std::uint8_t mask = 0;
    const std::uint8_t graphicMask = directionMask & kRoadSurfaceSidewalkEdgeMask;
    const std::uint8_t outgoingMask = static_cast<std::uint8_t>((travelDirectionMask != 0 ? travelDirectionMask : graphicMask) & graphicMask);
    const std::uint8_t incomingMask = static_cast<std::uint8_t>(graphicMask & ~outgoingMask);
    assert(CountCardinalDirections(outgoingMask) <= 2);
    assert(CountCardinalDirections(incomingMask) <= 2);
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

    secondary.parallelGraphic.applyToRenderState(renderState, secondaryEdgeMask());
}
