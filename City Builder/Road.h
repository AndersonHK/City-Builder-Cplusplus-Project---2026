#pragma once

#include <cstdint>
#include <vector>

#include "RoadLane.h"
#include "RoadLaneCell.h"
#include "RoadTemplate.h"
#include "RoadTemplateDefinition.h"

class Road {
public:
    explicit Road(const RoadTemplate& roadTemplate);

    const RoadTemplate& templateData() const;
    bool appendStrokePlacements(const Int2& startTile, const Int2& cornerTile, const Int2& endTile, int mapWidth, int mapHeight, std::vector<RoadTilePlacement>& placements) const;

    static RoadTemplate makeTemplate(RoadFamily family, TransportLayerId layer, int laneCount, RoadTrafficSide trafficSide, RoadDirectionMode directionMode);
    static RoadTemplate makeTemplate(RoadTemplateKind templateKind, RoadTrafficSide trafficSide, RoadDirectionMode directionMode);
    static RoadTemplateKind inferTemplateKind(RoadFamily family, TransportLayerId layer, int laneCount, RoadDirectionMode directionMode);
    static std::uint16_t makeTemplateId(RoadFamily family, TransportLayerId layer, int laneCount, RoadTrafficSide trafficSide, RoadDirectionMode directionMode);
    static std::uint16_t makeTemplateId(RoadTemplateKind templateKind, RoadFamily family, TransportLayerId layer, int laneCount, RoadTrafficSide trafficSide, RoadDirectionMode directionMode);
    static int chooseTemplateFootprint(const RoadTemplate& roadTemplate);
    static RoadLaneCell makeLaneCell(const RoadLanePlacement& lanePlacement, std::uint8_t directionMask, std::uint8_t travelDirectionMask = 0, std::uint8_t pathDirectionMask = 0);
    static RoadLaneCell makeLaneCell(const RoadLanePlacement& lanePlacement, const RoadLaneCellContext& context);

private:
    struct LayoutLane {
        RoadLane lane;
        float start;
        float end;

        LayoutLane();
    };

    bool appendLegPlacements(const Int2& startTile, const Int2& endTile, int mapWidth, int mapHeight, std::vector<RoadTilePlacement>& placements) const;
    std::vector<LayoutLane> buildLayoutLanes(std::uint8_t forwardDirection, std::uint8_t reverseDirection, int footprint) const;
    RoadLanePlacement makeLanePlacement(const LayoutLane& layoutLane, int laneOrdinal, int tileX, int tileY, int tileIndex, RoadAxis axis, int crossOffset, const std::vector<RoadTemplateSeamKind>& seamAfter) const;

    RoadTemplate roadTemplate_;
};
