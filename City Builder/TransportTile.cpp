#include "TransportTile.h"

#include <algorithm>

namespace {
bool LaneTypeCollapsesToOneTile(RoadLaneTypeId laneType) {
    return IsRoadCarLaneType(laneType) ||
        laneType == RoadLaneTypeId::Pedestrian ||
        laneType == RoadLaneTypeId::Separator;
}

bool RoadAxesOverlap(RoadAxis left, RoadAxis right) {
    return (AxisMaskFor(left) & AxisMaskFor(right)) != 0;
}

RoadAxis MergeRoadAxes(RoadAxis left, RoadAxis right) {
    return static_cast<RoadAxis>(AxisMaskFor(left) | AxisMaskFor(right));
}

bool IsSameAuthoredLaneReplay(const RoadLanePlacement& existingLane, const RoadLanePlacement& lanePlacement) {
    return existingLane.family == lanePlacement.family &&
        existingLane.layer == lanePlacement.layer &&
        existingLane.templateId == lanePlacement.templateId &&
        existingLane.laneIndex == lanePlacement.laneIndex &&
        existingLane.axis == lanePlacement.axis &&
        (existingLane.crossSectionMask & lanePlacement.crossSectionMask) == lanePlacement.crossSectionMask &&
        existingLane.laneType == lanePlacement.laneType &&
        existingLane.surface == lanePlacement.surface &&
        existingLane.role == lanePlacement.role &&
        existingLane.separatorStyle == lanePlacement.separatorStyle &&
        existingLane.sideOverlaps(lanePlacement);
}

bool IsSameAuthoredLaneAxisSubsetReplay(const RoadLanePlacement& existingLane, const RoadLanePlacement& lanePlacement) {
    const std::uint8_t existingAxisMask = AxisMaskFor(existingLane.axis);
    const std::uint8_t laneAxisMask = AxisMaskFor(lanePlacement.axis);
    return existingLane.family == lanePlacement.family &&
        existingLane.layer == lanePlacement.layer &&
        existingLane.templateId == lanePlacement.templateId &&
        laneAxisMask != 0 &&
        (existingAxisMask & laneAxisMask) == laneAxisMask &&
        (existingLane.crossSectionMask & lanePlacement.crossSectionMask) == lanePlacement.crossSectionMask &&
        existingLane.laneType == lanePlacement.laneType &&
        existingLane.surface == lanePlacement.surface &&
        existingLane.role == lanePlacement.role &&
        existingLane.separatorStyle == lanePlacement.separatorStyle &&
        existingLane.sideOverlaps(lanePlacement);
}
}

TransportTile::TransportTile() {
}

void TransportTile::clear() {
    lanes_.clear();
}

RoadTileLaneAddResult TransportTile::tryAddLane(const RoadLanePlacement& lanePlacement) {
    if (lanePlacement.family == RoadFamily::None) {
        return RoadTileLaneAddResult::Rejected;
    }

    std::size_t laneIndex = 0;
    for (; laneIndex < lanes_.size(); ++laneIndex) {
        RoadLanePlacement& existingLane = lanes_[laneIndex];
        if (existingLane.family != lanePlacement.family) {
            return RoadTileLaneAddResult::Rejected;
        }

        if (existingLane.isExactReplayOf(lanePlacement)) {
            return RoadTileLaneAddResult::Replay;
        }

        if (IsSameAuthoredLaneReplay(existingLane, lanePlacement)) {
            return RoadTileLaneAddResult::Replay;
        }

        if (IsSameAuthoredLaneAxisSubsetReplay(existingLane, lanePlacement)) {
            return RoadTileLaneAddResult::Replay;
        }

        const bool sameLaneCollisionClass = existingLane.laneType == lanePlacement.laneType ||
            (IsRoadCarLaneType(existingLane.laneType) && IsRoadCarLaneType(lanePlacement.laneType));
        if (sameLaneCollisionClass && LaneTypeCollapsesToOneTile(existingLane.laneType)) {
            if (RoadAxesOverlap(existingLane.axis, lanePlacement.axis) &&
                existingLane.sideOverlaps(lanePlacement)) {
                return RoadTileLaneAddResult::Rejected;
            }

            continue;
        }

        if (!LaneTypeCollapsesToOneTile(existingLane.laneType) &&
            !LaneTypeCollapsesToOneTile(lanePlacement.laneType) &&
            existingLane.isSameAxis(lanePlacement) &&
            existingLane.sideOverlaps(lanePlacement)) {
            return RoadTileLaneAddResult::Rejected;
        }
    }

    lanes_.push_back(lanePlacement);
    return RoadTileLaneAddResult::Added;
}

bool TransportTile::empty() const {
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes_.size(); ++laneIndex) {
        if (lanes_[laneIndex].active) {
            return false;
        }
    }

    return true;
}

RoadFamily TransportTile::family() const {
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes_.size(); ++laneIndex) {
        if (lanes_[laneIndex].active) {
            return lanes_[laneIndex].family;
        }
    }

    return lanes_.empty() ? RoadFamily::None : lanes_.front().family;
}

const std::vector<RoadLanePlacement>& TransportTile::lanes() const {
    return lanes_;
}

std::vector<RoadLanePlacement>& TransportTile::lanesForMutation() {
    return lanes_;
}

bool TransportTile::hasLaneType(RoadLaneTypeId laneType) const {
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes_.size(); ++laneIndex) {
        if (!lanes_[laneIndex].active) {
            continue;
        }

        if (IsRoadCarLaneType(laneType) && IsRoadCarLaneType(lanes_[laneIndex].laneType)) {
            return true;
        }

        if (lanes_[laneIndex].laneType == laneType) {
            return true;
        }
    }

    return false;
}

bool TransportTile::hasCarAxis(RoadAxis axis) const {
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes_.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes_[laneIndex];
        if (!lane.active || !lane.isCar()) {
            continue;
        }

        if ((AxisMaskFor(lane.axis) & AxisMaskFor(axis)) != 0) {
            return true;
        }

        if (AxisMaskFor(lane.axis) != 0) {
            continue;
        }

        if (axis == RoadAxis::Horizontal &&
            (lane.hasTravelDirection(kRoadDirectionEast) || lane.hasTravelDirection(kRoadDirectionWest))) {
            return true;
        }

        if (axis == RoadAxis::Vertical &&
            (lane.hasTravelDirection(kRoadDirectionNorth) || lane.hasTravelDirection(kRoadDirectionSouth))) {
            return true;
        }
    }

    return false;
}
