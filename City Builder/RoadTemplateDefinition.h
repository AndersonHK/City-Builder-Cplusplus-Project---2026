#pragma once

#include <cstdint>

#include "RoadLaneCell.h"
#include "RoadTemplate.h"

struct RoadLaneCellContext {
    std::uint8_t directionMask;
    std::uint8_t travelDirectionMask;
    std::uint8_t pathDirectionMask;
    bool hasAvenueTileRole;
    bool avenueOuterTile;
    bool hasOneWayLeftNeighbor;
    bool oneWayLeftNeighbor;

    RoadLaneCellContext();
};

class RoadTemplateDefinition {
public:
    virtual ~RoadTemplateDefinition();

    virtual RoadTemplateKind kind() const = 0;
    virtual RoadFamily family() const;
    virtual TransportLayerId layer() const;
    virtual RoadDirectionMode normalizeDirectionMode(RoadDirectionMode directionMode) const;
    virtual int normalizeLaneCount(int laneCount, RoadDirectionMode directionMode) const;
    virtual void buildElements(RoadTemplate& roadTemplate) const = 0;
    virtual RoadLaneCell makeLaneCell(const RoadLanePlacement& lanePlacement, const RoadLaneCellContext& context) const = 0;

    static const RoadTemplateDefinition& forKind(RoadTemplateKind templateKind);
};
