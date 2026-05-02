#pragma once

#include <cstdint>

#include "TransportTypes.h"

enum class RoadTileLaneAddResult {
    Added,
    Replay,
    Rejected
};

enum class RoadTemplateSeamKind : std::uint8_t {
    None = 0,
    SameDirectionLaneDivider,
    OpposingDirectionLaneDivider
};

class RoadLane {
public:
    RoadLane();
    RoadLane(const RoadTemplateElement& element, int laneIndex);

    RoadLaneTypeId laneType() const;
    RoadLaneSurface surface() const;
    RoadLaneRole role() const;
    const RoadElementBehavior& behavior() const;
    int laneIndex() const;
    std::uint8_t laneTravelMask() const;
    std::uint8_t arrowTravelMask() const;

    void setTravel(std::uint8_t laneTravelMask);

    bool isCar() const;
    bool isPedestrian() const;
    bool usesRoadArrows() const;
    bool usesDirectedFlow() const;
    std::uint16_t traversalCost(RoadFamily family) const;

private:
    RoadTemplateElement element_;
    int laneIndex_;
    std::uint8_t laneTravelMask_;
    std::uint8_t arrowTravelMask_;
};

struct RoadLanePlacement {
    int tileX;
    int tileY;
    int tileIndex;
    RoadFamily family;
    TransportLayerId layer;
    std::uint16_t templateId;
    int laneIndex;
    RoadAxis axis;
    std::uint8_t crossSectionMask;
    RoadLaneTypeId laneType;
    RoadLaneSurface surface;
    RoadLaneRole role;
    std::uint8_t laneTravelMask;
    std::uint8_t arrowTravelMask;
    float sideMin;
    float sideMax;
    std::uint8_t sidewalkEdgeMask;
    std::uint8_t sameDirectionDividerMask;
    std::uint8_t opposingDirectionDividerMask;

    RoadLanePlacement();

    bool isCar() const;
    bool isPedestrian() const;
    bool isSameAxis(const RoadLanePlacement& other) const;
    bool sideOverlaps(const RoadLanePlacement& other) const;
    bool isExactReplayOf(const RoadLanePlacement& other) const;
    bool hasTravelDirection(std::uint8_t roadDirection) const;
};

RoadTemplateSeamKind RoadTemplateSeamBetween(const RoadLane& first, const RoadLane& second);
