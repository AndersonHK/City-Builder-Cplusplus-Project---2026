#include "RoadLane.h"

#include <algorithm>
#include <cmath>

namespace {
const float kLaneSpanEpsilon = 0.0001f;
const std::uint16_t kTravelCostScale = 1000u;
}

RoadLane::RoadLane()
    : laneIndex_(0),
      laneTravelMask_(0),
      arrowTravelMask_(0) {
}

RoadLane::RoadLane(const RoadTemplateElement& element, int laneIndex)
    : element_(element),
      laneIndex_(laneIndex),
      laneTravelMask_(0),
      arrowTravelMask_(0) {
}

RoadLaneTypeId RoadLane::laneType() const {
    return element_.laneType;
}

RoadLaneSurface RoadLane::surface() const {
    return element_.surface;
}

RoadLaneRole RoadLane::role() const {
    return element_.laneRole;
}

const RoadElementBehavior& RoadLane::behavior() const {
    return element_.behavior;
}

int RoadLane::laneIndex() const {
    return laneIndex_;
}

std::uint8_t RoadLane::laneTravelMask() const {
    return laneTravelMask_;
}

std::uint8_t RoadLane::arrowTravelMask() const {
    return arrowTravelMask_;
}

void RoadLane::setTravel(std::uint8_t laneTravelMask) {
    laneTravelMask_ = laneTravelMask;
    arrowTravelMask_ = usesRoadArrows() ? laneTravelMask : 0;
}

bool RoadLane::isCar() const {
    return IsRoadCarLaneType(element_.laneType);
}

bool RoadLane::isPedestrian() const {
    return element_.laneType == RoadLaneTypeId::Pedestrian;
}

bool RoadLane::isSeparator() const {
    return element_.laneType == RoadLaneTypeId::Separator;
}

bool RoadLane::usesRoadArrows() const {
    return IsRoadCarLaneType(element_.laneType) || element_.laneType == RoadLaneTypeId::Bus;
}

bool RoadLane::usesDirectedFlow() const {
    return element_.laneRole == RoadLaneRole::Through || element_.laneRole == RoadLaneRole::Turn || element_.laneRole == RoadLaneRole::Transit;
}

std::uint16_t RoadLane::traversalCost(RoadFamily family) const {
    switch (element_.laneType) {
        case RoadLaneTypeId::Slow:
            return kTravelCostScale / 9u;
        case RoadLaneTypeId::Medium:
            return kTravelCostScale / 11u;
        case RoadLaneTypeId::Fast:
            return family == RoadFamily::Highway ? kTravelCostScale / 14u : kTravelCostScale / 13u;
        case RoadLaneTypeId::Pedestrian:
            return kTravelCostScale / 2u;
        case RoadLaneTypeId::Bike:
            return kTravelCostScale / 4u;
        case RoadLaneTypeId::Bus:
            return family == RoadFamily::Highway ? kTravelCostScale / 14u : kTravelCostScale / 8u;
        case RoadLaneTypeId::Separator:
            return 0;
        default:
            return 0;
    }
}

RoadLanePlacement::RoadLanePlacement()
    : tileX(0),
      tileY(0),
      tileIndex(0),
      family(RoadFamily::None),
      layer(TransportLayerId::Ground),
      templateId(0),
      strokeId(0),
      laneIndex(0),
      axis(RoadAxis::None),
      crossSectionMask(0),
      laneType(RoadLaneTypeId::Slow),
      surface(RoadLaneSurface::Asphalt),
      role(RoadLaneRole::Through),
      separatorStyle(RoadSeparatorStyle::None),
      laneTravelMask(0),
      arrowTravelMask(0),
      sideMin(0.0f),
      sideMax(1.0f),
      sidewalkEdgeMask(0),
      sameDirectionDividerMask(0),
      opposingDirectionDividerMask(0),
      active(true) {
}

bool RoadLanePlacement::isCar() const {
    return IsRoadCarLaneType(laneType);
}

bool RoadLanePlacement::isPedestrian() const {
    return laneType == RoadLaneTypeId::Pedestrian;
}

bool RoadLanePlacement::isSeparator() const {
    return laneType == RoadLaneTypeId::Separator;
}

bool RoadLanePlacement::isSameAxis(const RoadLanePlacement& other) const {
    return axis == other.axis;
}

bool RoadLanePlacement::sideOverlaps(const RoadLanePlacement& other) const {
    return sideMin < other.sideMax - kLaneSpanEpsilon && sideMax > other.sideMin + kLaneSpanEpsilon;
}

bool RoadLanePlacement::isExactReplayOf(const RoadLanePlacement& other) const {
    return family == other.family &&
        layer == other.layer &&
        templateId == other.templateId &&
        laneIndex == other.laneIndex &&
        axis == other.axis &&
        crossSectionMask == other.crossSectionMask &&
        laneType == other.laneType &&
        surface == other.surface &&
        role == other.role &&
        separatorStyle == other.separatorStyle &&
        laneTravelMask == other.laneTravelMask &&
        arrowTravelMask == other.arrowTravelMask &&
        std::fabs(sideMin - other.sideMin) <= kLaneSpanEpsilon &&
        std::fabs(sideMax - other.sideMax) <= kLaneSpanEpsilon &&
        sidewalkEdgeMask == other.sidewalkEdgeMask &&
        sameDirectionDividerMask == other.sameDirectionDividerMask &&
        opposingDirectionDividerMask == other.opposingDirectionDividerMask;
}

bool RoadLanePlacement::hasTravelDirection(std::uint8_t roadDirection) const {
    return (laneTravelMask & LaneIntentFromRoadDirection(roadDirection)) != 0;
}

RoadTemplateSeamKind RoadTemplateSeamBetween(const RoadLane& first, const RoadLane& second) {
    if (!first.isCar() || !second.isCar()) {
        return RoadTemplateSeamKind::None;
    }

    if (first.role() != RoadLaneRole::Through || second.role() != RoadLaneRole::Through) {
        return RoadTemplateSeamKind::None;
    }

    if (first.laneTravelMask() == 0 || second.laneTravelMask() == 0) {
        return RoadTemplateSeamKind::None;
    }

    return first.laneTravelMask() == second.laneTravelMask()
        ? RoadTemplateSeamKind::SameDirectionLaneDivider
        : RoadTemplateSeamKind::OpposingDirectionLaneDivider;
}
