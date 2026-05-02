#include "TransportTile.h"

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
        const RoadLanePlacement& existingLane = lanes_[laneIndex];
        if (existingLane.family != lanePlacement.family) {
            return RoadTileLaneAddResult::Rejected;
        }

        if (existingLane.isExactReplayOf(lanePlacement)) {
            return RoadTileLaneAddResult::Replay;
        }

        if (existingLane.isSameAxis(lanePlacement) && existingLane.sideOverlaps(lanePlacement)) {
            return RoadTileLaneAddResult::Rejected;
        }
    }

    lanes_.push_back(lanePlacement);
    return RoadTileLaneAddResult::Added;
}

bool TransportTile::empty() const {
    return lanes_.empty();
}

RoadFamily TransportTile::family() const {
    return lanes_.empty() ? RoadFamily::None : lanes_.front().family;
}

const std::vector<RoadLanePlacement>& TransportTile::lanes() const {
    return lanes_;
}

bool TransportTile::hasLaneType(RoadLaneTypeId laneType) const {
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes_.size(); ++laneIndex) {
        if (lanes_[laneIndex].laneType == laneType) {
            return true;
        }
    }

    return false;
}

bool TransportTile::hasAxis(RoadAxis axis) const {
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes_.size(); ++laneIndex) {
        if (lanes_[laneIndex].axis == axis) {
            return true;
        }
    }

    return false;
}

bool TransportTile::hasCarAxis(RoadAxis axis) const {
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes_.size(); ++laneIndex) {
        if (lanes_[laneIndex].isCar() && lanes_[laneIndex].axis == axis) {
            return true;
        }
    }

    return false;
}

bool TransportTile::hasCompatibleLane(RoadFamily family, RoadLaneTypeId laneType, RoadAxis axis, std::uint8_t roadDirection) const {
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes_.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes_[laneIndex];
        if (lane.family == family &&
            lane.laneType == laneType &&
            lane.axis == axis &&
            lane.hasTravelDirection(roadDirection)) {
            return true;
        }
    }

    return false;
}

bool TransportTile::hasLaneContinuation(const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection) const {
    return hasCompatibleLane(lanePlacement.family, lanePlacement.laneType, lanePlacement.axis, roadDirection);
}

bool TransportTile::hasCarLaneThrough(std::uint8_t roadDirection) const {
    const RoadAxis axis = (roadDirection == kRoadDirectionEast || roadDirection == kRoadDirectionWest)
        ? RoadAxis::Horizontal
        : RoadAxis::Vertical;
    return hasCompatibleLane(family(), RoadLaneTypeId::Car, axis, roadDirection);
}
