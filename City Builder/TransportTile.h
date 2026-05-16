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
    std::vector<RoadLanePlacement>& lanesForMutation();

    bool hasLaneType(RoadLaneTypeId laneType) const;
    bool hasCarAxis(RoadAxis axis) const;
private:
    std::vector<RoadLanePlacement> lanes_;
};
