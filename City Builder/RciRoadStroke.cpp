#include "RciTool.h"

#include "TransportNetwork.h"

// RCI planners describe local access roads as simple endpoints. This converts
// that compact plan into the normal road stroke command used by renderer ghosts
// and committed simulation placement.
RoadStrokeCommand BuildRciRoadStrokeCommand(const RciRoadPlan& roadPlan) {
    RoadStrokeCommand roadStrokeCommand;
    roadStrokeCommand.startTile = Int2(roadPlan.startTileX, roadPlan.startTileY);
    roadStrokeCommand.cornerTile = Int2(roadPlan.endTileX, roadPlan.endTileY);
    roadStrokeCommand.endTile = Int2(roadPlan.endTileX, roadPlan.endTileY);
    roadStrokeCommand.family = RoadFamily::LocalStreet;
    roadStrokeCommand.layer = TransportLayerId::Ground;
    roadStrokeCommand.templateKind = RoadTemplateKind::Street;
    roadStrokeCommand.roadTemplate = TransportNetwork::makeRoadTemplate(
        roadStrokeCommand.templateKind,
        RoadTrafficSide::RightHand,
        RoadDirectionMode::TwoWay);
    return roadStrokeCommand;
}
