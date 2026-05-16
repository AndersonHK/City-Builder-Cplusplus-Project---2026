#include "TransportTile.h"

#include <algorithm>

namespace {
bool LaneTypeCollapsesToOneTile(RoadLaneTypeId laneType) {
    return laneType == RoadLaneTypeId::Car ||
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
        existingLane.crossSectionMask == lanePlacement.crossSectionMask &&
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

        if (existingLane.laneType == lanePlacement.laneType &&
            LaneTypeCollapsesToOneTile(existingLane.laneType)) {
            if (RoadAxesOverlap(existingLane.axis, lanePlacement.axis) &&
                (existingLane.crossSectionMask & lanePlacement.crossSectionMask) == 0) {
                return RoadTileLaneAddResult::Rejected;
            }

            existingLane.laneTravelMask |= lanePlacement.laneTravelMask;
            existingLane.arrowTravelMask |= lanePlacement.arrowTravelMask;
            existingLane.sidewalkEdgeMask |= lanePlacement.sidewalkEdgeMask;
            existingLane.sameDirectionDividerMask |= lanePlacement.sameDirectionDividerMask;
            existingLane.opposingDirectionDividerMask |= lanePlacement.opposingDirectionDividerMask;
            existingLane.axis = MergeRoadAxes(existingLane.axis, lanePlacement.axis);
            existingLane.crossSectionMask |= lanePlacement.crossSectionMask;
            existingLane.sideMin = std::min(existingLane.sideMin, lanePlacement.sideMin);
            existingLane.sideMax = std::max(existingLane.sideMax, lanePlacement.sideMax);
            if (existingLane.separatorStyle == RoadSeparatorStyle::None) {
                existingLane.separatorStyle = lanePlacement.separatorStyle;
            }
            existingLane.active = existingLane.active || lanePlacement.active;
            return RoadTileLaneAddResult::Added;
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
        if (lanes_[laneIndex].active && lanes_[laneIndex].laneType == laneType) {
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
