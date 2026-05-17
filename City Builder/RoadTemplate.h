#pragma once

#include <cstdint>
#include <vector>

#include "RoadLane.h"

// Stable, tool-facing road template data. A stroke chooses a template kind and
// direction; the template expands that into tile lane placements and later
// recreates RoadLaneCells during dirty cleanup.
struct RoadTemplate {
    RoadTemplateKind templateKind;
    RoadFamily family;
    TransportLayerId layer;
    RoadTrafficSide trafficSide;
    RoadDirectionMode directionMode;
    RoadTemplateIdentity identity;
    RoadTemplateOverlapPolicy overlapPolicy;
    int laneCount;
    std::vector<RoadTemplateElement> elements;

    RoadTemplate();
};

// Transient brush command. Save data keeps authored tile lanes, not these
// command objects.
struct RoadStrokeCommand {
    Int2 startTile;
    Int2 cornerTile;
    Int2 endTile;
    RoadTemplateKind templateKind;
    RoadFamily family;
    TransportLayerId layer;
    RoadStrokeOperation operation;
    RoadTemplate roadTemplate;

    RoadStrokeCommand();
};

struct RoadTilePlacement {
    int tileX;
    int tileY;
    int tileIndex;
    RoadLanePlacement lanePlacement;

    RoadTilePlacement();
};
