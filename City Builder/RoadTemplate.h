#pragma once

#include <cstdint>
#include <vector>

#include "RoadLane.h"

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
