#pragma once

#include <cstdint>

#include "TransportTypes.h"

struct CommuteRouteSegment {
    int startTileX;
    int startTileY;
    int endTileX;
    int endTileY;
    TransportLayerId layer;
    TransportMode mode;
    std::uint8_t direction;
    std::uint16_t demand;

    CommuteRouteSegment()
        : startTileX(0),
          startTileY(0),
          endTileX(0),
          endTileY(0),
          layer(TransportLayerId::Ground),
          mode(TransportMode::Car),
          direction(0),
          demand(0) {
    }
};

