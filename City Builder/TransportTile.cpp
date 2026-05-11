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

bool TransportTile::hasAxis(RoadAxis axis) const {
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes_.size(); ++laneIndex) {
        if (lanes_[laneIndex].active && lanes_[laneIndex].axis == axis) {
            return true;
        }
    }

    return false;
}

bool TransportTile::hasCarAxis(RoadAxis axis) const {
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes_.size(); ++laneIndex) {
        if (lanes_[laneIndex].active && lanes_[laneIndex].isCar() && lanes_[laneIndex].axis == axis) {
            return true;
        }
    }

    return false;
}

bool TransportTile::hasCompatibleLane(const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection, bool includeInactiveLanes) const {
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes_.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes_[laneIndex];
        if ((includeInactiveLanes || lane.active) &&
            lane.family == lanePlacement.family &&
            lane.laneType == lanePlacement.laneType &&
            lane.axis == lanePlacement.axis &&
            lane.laneIndex == lanePlacement.laneIndex &&
            lane.sideOverlaps(lanePlacement) &&
            lane.hasTravelDirection(roadDirection)) {
            return true;
        }
    }

    return false;
}

bool TransportTile::hasMatchingLaneBody(const RoadLanePlacement& lanePlacement) const {
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes_.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes_[laneIndex];
        if (lane.active &&
            lane.family == lanePlacement.family &&
            lane.laneType == lanePlacement.laneType &&
            lane.axis == lanePlacement.axis &&
            lane.laneIndex == lanePlacement.laneIndex &&
            lane.sideOverlaps(lanePlacement)) {
            return true;
        }
    }

    return false;
}

bool TransportTile::hasMatchingLaneBodyFromStroke(const RoadLanePlacement& lanePlacement, bool includeInactiveLanes) const {
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes_.size(); ++laneIndex) {
        const RoadLanePlacement& lane = lanes_[laneIndex];
        if ((includeInactiveLanes || lane.active) &&
            lane.strokeId == lanePlacement.strokeId &&
            lane.family == lanePlacement.family &&
            lane.laneType == lanePlacement.laneType &&
            lane.axis == lanePlacement.axis &&
            lane.laneIndex == lanePlacement.laneIndex &&
            lane.sideOverlaps(lanePlacement)) {
            return true;
        }
    }

    return false;
}

bool TransportTile::hasLaneContinuation(const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection) const {
    return hasCompatibleLane(lanePlacement, roadDirection, false);
}

bool TransportTile::hasCarLaneThrough(std::uint8_t roadDirection) const {
    std::size_t laneIndex = 0;
    for (; laneIndex < lanes_.size(); ++laneIndex) {
        if (lanes_[laneIndex].active && lanes_[laneIndex].isCar() && lanes_[laneIndex].hasTravelDirection(roadDirection)) {
            return true;
        }
    }

    return false;
}
