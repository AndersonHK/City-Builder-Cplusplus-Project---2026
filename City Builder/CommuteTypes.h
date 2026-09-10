#pragma once

// Allows the shared benchmark fingerprint to compile against older checkouts.
#define CITY_COMMUTE_ROUTE_TIMING 1

#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

#include "TransportCostMap.h"
#include "TransportTypes.h"

using CommuteRouteClock = std::shared_ptr<const std::vector<float>>;

struct CommuteRouteSegment {
    int startTileX;
    int startTileY;
    int endTileX;
    int endTileY;
    TransportLayerId layer;
    TransportMode mode;
    CommuteTimeOfDay timeOfDay;
    std::uint8_t direction;
    std::uint16_t demand;
    // Non-owning view of immutable path-boundary seconds. The enclosing route,
    // published buffer, query result or controller view retains the clock owner.
    // This keeps hot segment publication trivially copyable. Departure delay is excluded.
    const std::vector<float>* elapsedSeconds = nullptr;
    std::size_t timingBegin = 0;
    std::size_t timingEnd = 0;

    CommuteRouteSegment()
        : startTileX(0),
          startTileY(0),
          endTileX(0),
          endTileY(0),
          layer(TransportLayerId::Ground),
          mode(TransportMode::Car),
          timeOfDay(CommuteTimeOfDay::Morning),
          direction(0),
          demand(0) {
    }
};

static_assert(std::is_trivially_copyable<CommuteRouteSegment>::value, "Published route spans must remain cheap to copy");

struct CommuteRouteRecord {
    // Derived runtime cache; routes are rebuilt on save import.
    std::uint64_t costSnapshot = 0;
    int destinationLotId;
    int demand;
    std::uint16_t transportLoad;
    bool longCommute;
    bool morningMediumRetry;
    bool eveningMediumRetry;
    TransportPathResult morningPathResult;
    TransportPathResult eveningPathResult;
    CommuteRouteClock morningSeconds, eveningSeconds;
    std::vector<CommuteRouteSegment> morningSegments;
    std::vector<CommuteRouteSegment> eveningSegments;

    CommuteRouteRecord()
        : destinationLotId(-1),
          demand(0),
          transportLoad(0u),
          longCommute(false),
          morningMediumRetry(false),
          eveningMediumRetry(false) {
    }
};
