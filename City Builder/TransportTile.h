#pragma once

#include <cstdint>
#include <vector>

#include "RoadLane.h"
#include "RoadRenderState.h"

class TransportTile {
public:
    TransportTile();

    void clear();
    RoadTileLaneAddResult tryAddLane(const RoadLanePlacement& lanePlacement);

    bool empty() const;
    RoadFamily family() const;
    const std::vector<RoadLanePlacement>& lanes() const;

    bool hasLaneType(RoadLaneTypeId laneType) const;
    bool hasAxis(RoadAxis axis) const;
    bool hasCarAxis(RoadAxis axis) const;
    bool hasCompatibleLane(RoadFamily family, RoadLaneTypeId laneType, RoadAxis axis, std::uint8_t roadDirection) const;
    bool hasLaneContinuation(const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection) const;
    bool hasCarLaneThrough(std::uint8_t roadDirection) const;

private:
    std::vector<RoadLanePlacement> lanes_;
};
