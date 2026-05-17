#pragma once

#include <cstdint>

#include "TransportTypes.h"

struct RoadRenderState;

enum class RoadGraphicPrimitive : std::uint8_t {
    None = 0,
    Asphalt,
    Sidewalk,
    Crosswalk,
    Median,
    Divider
};

// Render intent emitted by a lane cell. This class only packs primitive masks
// into RoadRenderState; it does not decide topology or pathing.
class RoadGraphic {
public:
    RoadGraphic();

    static RoadGraphic none();
    static RoadGraphic asphalt(std::uint8_t directionMask);
    static RoadGraphic sidewalk(std::uint8_t directionMask);
    static RoadGraphic crosswalk(std::uint8_t directionMask);
    static RoadGraphic median(std::uint8_t directionMask);
    static RoadGraphic divider(std::uint8_t directionMask);

    RoadGraphicPrimitive primitive() const;
    std::uint8_t directionMask() const;

    void setDirectionMask(std::uint8_t directionMask);
    void applyToRenderState(RoadRenderState& renderState) const;
    void applyToRenderState(RoadRenderState& renderState, std::uint8_t directionMask) const;

private:
    RoadGraphicPrimitive primitive_;
    std::uint8_t directionMask_;
};
