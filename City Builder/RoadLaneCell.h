#pragma once

#include <cstdint>

#include "RoadGraphic.h"

enum class CommuterMode : std::uint8_t {
    None = 0,
    Car,
    Pedestrian
};

// A template-emitted lane intent. Direction masks are local N/E/S/W road
// directions; the renderer and cost map consume them without inventing extra
// topology.
class Lane {
public:
    CommuterMode mode;
    RoadLaneTypeId laneType;
    int capacity;
    std::uint8_t directionMask;
    std::uint8_t travelDirectionMask;
    std::uint8_t pathDirectionMask;
    bool centerSide;
    RoadGraphic parallelGraphic;
    RoadGraphic crossingGraphic;

    Lane();
    std::uint8_t centerMask() const;
};

// One primary car-like lane plus an optional deterministic secondary lane
// such as a sidewalk or median. Road templates emit these cells; transport
// resolution turns them into costs, access masks, and packed render state.
class RoadLaneCell {
public:
    Lane primary;
    Lane secondary;

    RoadLaneCell();

    bool hasSecondary() const;
    void clearSecondary();
    void applyGraphics(RoadRenderState& renderState) const;
    std::uint8_t secondaryEdgeMask() const;
};
