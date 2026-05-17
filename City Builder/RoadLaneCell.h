#pragma once

#include <cstdint>

#include "RoadGraphic.h"

enum class CommuterMode : std::uint8_t {
    None = 0,
    Car,
    Pedestrian
};

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
