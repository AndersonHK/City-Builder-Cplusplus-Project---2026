#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "LotModule.h"

enum class TransportMode : std::uint8_t {
    Car = 0,
    Pedestrian,
    Count
};

enum class TransportLayerId : std::uint8_t {
    Ground = 0,
    Elevated,
    Underground,
    Count
};

enum class CommuteTimeOfDay : std::uint8_t {
    Morning = 0,
    Evening,
    Count
};

enum class RoadFamily : std::uint8_t {
    None = 0,
    LocalStreet,
    Highway
};

enum class RoadStrokeOperation : std::uint8_t {
    Place = 0
};

enum class RoadTrafficSide : std::uint8_t {
    RightHand = 0,
    LeftHand
};

enum class RoadDirectionMode : std::uint8_t {
    TwoWay = 0,
    OneWayForward,
    OneWayReverse
};

enum class RoadTemplateKind : std::uint8_t {
    Street = 0,
    Avenue,
    Highway,
    Road
};

enum class RoadLaneTypeId : std::uint8_t {
    Slow = 0,
    Car = Slow,
    Medium,
    Fast,
    Pedestrian,
    Bike,
    Bus,
    Separator,
    Count
};

enum class RoadLaneSurface : std::uint8_t {
    Asphalt = 0,
    Sidewalk,
    Crosswalk,
    Shoulder,
    Median
};

enum class RoadLaneRole : std::uint8_t {
    None = 0,
    Through,
    Turn,
    Transit,
    Access,
    Separator
};

enum class RoadSeparatorStyle : std::uint8_t {
    None = 0,
    PaintedLine,
    Median
};

enum class RoadTemplateOverlapPolicy : std::uint8_t {
    StrictSameTemplate = 0,
    AdapterFriendly
};

enum class RoadRenderVariant : std::uint8_t {
    None = 0,
    Isolated,
    DeadEnd,
    Straight,
    Corner,
    Tee,
    Cross
};

enum class RoadBaseGlyph : std::uint8_t {
    None = 0,
    LocalIsolated,
    LocalDeadEndNorth,
    LocalDeadEndEast,
    LocalDeadEndSouth,
    LocalDeadEndWest,
    LocalStraightVertical,
    LocalStraightHorizontal,
    LocalCornerNorthEast,
    LocalCornerSouthEast,
    LocalCornerSouthWest,
    LocalCornerNorthWest,
    LocalTeeMissingNorth,
    LocalTeeMissingEast,
    LocalTeeMissingSouth,
    LocalTeeMissingWest,
    LocalCross,
    HighwayIsolated,
    HighwayDeadEndNorth,
    HighwayDeadEndEast,
    HighwayDeadEndSouth,
    HighwayDeadEndWest,
    HighwayStraightVertical,
    HighwayStraightHorizontal,
    HighwayCornerNorthEast,
    HighwayCornerSouthEast,
    HighwayCornerSouthWest,
    HighwayCornerNorthWest,
    HighwayTeeMissingNorth,
    HighwayTeeMissingEast,
    HighwayTeeMissingSouth,
    HighwayTeeMissingWest,
    HighwayCross
};

enum class RoadArrowGlyph : std::uint8_t {
    None = 0,
    East = 1,
    West = 2,
    North = 4,
    NorthEast = 5,
    NorthWest = 6,
    South = 8,
    SouthEast = 9,
    SouthWest = 10
};

enum class RoadAxis : std::uint8_t {
    None = 0,
    Horizontal = 1u << 0,
    Vertical = 1u << 1
};

struct RoadElementBehavior {
    float minimumWidth;
    float preferredWidth;
    float maximumWidth;
    std::uint8_t connectorMask;

    RoadElementBehavior()
        : minimumWidth(1.0f),
          preferredWidth(1.0f),
          maximumWidth(1.0f),
          connectorMask(0) {
    }
};

struct RoadLaneFlow {
    std::uint8_t fromMask;
    std::uint8_t toMask;

    RoadLaneFlow()
        : fromMask(0),
          toMask(0) {
    }
};

struct RoadTemplateElement {
    RoadLaneTypeId laneType;
    RoadLaneSurface surface;
    RoadLaneRole laneRole;
    RoadSeparatorStyle separatorStyle;
    RoadElementBehavior behavior;
    RoadLaneFlow flow;

    RoadTemplateElement()
        : laneType(RoadLaneTypeId::Slow),
          surface(RoadLaneSurface::Asphalt),
          laneRole(RoadLaneRole::Through),
          separatorStyle(RoadSeparatorStyle::None) {
    }
};

struct RoadTemplateIdentity {
    std::uint16_t id;
    std::uint8_t footprint;

    RoadTemplateIdentity()
        : id(0),
          footprint(0) {
    }
};

struct ResolvedRoadCell {
    std::uint8_t family;
    std::uint8_t travelMask;
    std::uint8_t laneTypeMask;
    std::uint8_t surfaceMask;
    std::uint8_t laneCount;
    std::uint8_t exitMask;
    std::uint8_t surfaceEdgeMask;
    std::uint8_t dividerMask;
    std::uint8_t junctionMask;
    std::uint8_t renderVariant;
    std::uint8_t baseGlyph;
    std::uint8_t arrowGlyph;
    std::array<std::uint16_t, static_cast<std::size_t>(RoadLaneTypeId::Count)> laneTypeCosts;

    ResolvedRoadCell()
        : family(static_cast<std::uint8_t>(RoadFamily::None)),
          travelMask(0),
          laneTypeMask(0),
          surfaceMask(0),
          laneCount(0),
          exitMask(0),
          surfaceEdgeMask(0),
          dividerMask(0),
          junctionMask(0),
          renderVariant(static_cast<std::uint8_t>(RoadRenderVariant::None)),
          baseGlyph(static_cast<std::uint8_t>(RoadBaseGlyph::None)),
          arrowGlyph(static_cast<std::uint8_t>(RoadArrowGlyph::None)) {
        laneTypeCosts.fill(0);
    }
};

struct RoadResolvedTile {
    std::uint8_t family;
    std::uint8_t laneTypeMask;
    std::uint8_t laneCount;
    std::uint8_t travelMask;
    std::uint8_t exitMask;

    RoadResolvedTile()
        : family(static_cast<std::uint8_t>(RoadFamily::None)),
          laneTypeMask(0),
          laneCount(0),
          travelMask(0),
          exitMask(0) {
    }
};

constexpr std::uint8_t kRoadDirectionNorth = 1u << 0;
constexpr std::uint8_t kRoadDirectionEast = 1u << 1;
constexpr std::uint8_t kRoadDirectionSouth = 1u << 2;
constexpr std::uint8_t kRoadDirectionWest = 1u << 3;
constexpr std::uint8_t kRoadDirectionNorthEast = 1u << 4;
constexpr std::uint8_t kRoadDirectionSouthEast = 1u << 5;
constexpr std::uint8_t kRoadDirectionSouthWest = 1u << 6;
constexpr std::uint8_t kRoadDirectionNorthWest = 1u << 7;
constexpr std::size_t kRoadDirectionCount = 8u;

// Counts only cardinal travel directions. Diagonal road-direction flags are
// visual/adjacency hints and should not turn a road cell into an intersection.
inline int CountRoadCardinalDirections(std::uint8_t directionMask) {
    return ((directionMask & kRoadDirectionNorth) != 0 ? 1 : 0) +
        ((directionMask & kRoadDirectionEast) != 0 ? 1 : 0) +
        ((directionMask & kRoadDirectionSouth) != 0 ? 1 : 0) +
        ((directionMask & kRoadDirectionWest) != 0 ? 1 : 0);
}

constexpr std::uint8_t kLaneIntentEast = 1u << 0;
constexpr std::uint8_t kLaneIntentWest = 1u << 1;
constexpr std::uint8_t kLaneIntentNorth = 1u << 2;
constexpr std::uint8_t kLaneIntentSouth = 1u << 3;
constexpr std::uint8_t kRoadArrowDebugFlag = 1u << 7;

constexpr std::uint8_t kRoadLaneTypeSlow = 1u << static_cast<std::uint8_t>(RoadLaneTypeId::Slow);
constexpr std::uint8_t kRoadLaneTypeMedium = 1u << static_cast<std::uint8_t>(RoadLaneTypeId::Medium);
constexpr std::uint8_t kRoadLaneTypeFast = 1u << static_cast<std::uint8_t>(RoadLaneTypeId::Fast);
constexpr std::uint8_t kRoadLaneTypeCar = static_cast<std::uint8_t>(kRoadLaneTypeSlow | kRoadLaneTypeMedium | kRoadLaneTypeFast);
constexpr std::uint8_t kRoadLaneTypePedestrian = 1u << static_cast<std::uint8_t>(RoadLaneTypeId::Pedestrian);
constexpr std::uint8_t kRoadLaneTypeBike = 1u << static_cast<std::uint8_t>(RoadLaneTypeId::Bike);
constexpr std::uint8_t kRoadLaneTypeBus = 1u << static_cast<std::uint8_t>(RoadLaneTypeId::Bus);
constexpr std::uint8_t kRoadLaneTypeSeparator = 1u << static_cast<std::uint8_t>(RoadLaneTypeId::Separator);

constexpr std::uint8_t kTransportModeCar = 1u << static_cast<std::uint8_t>(TransportMode::Car);
constexpr std::uint8_t kTransportModePedestrian = 1u << static_cast<std::uint8_t>(TransportMode::Pedestrian);

constexpr std::uint8_t kRoadSurfaceAsphalt = 1u << static_cast<std::uint8_t>(RoadLaneSurface::Asphalt);
constexpr std::uint8_t kRoadSurfaceSidewalk = 1u << static_cast<std::uint8_t>(RoadLaneSurface::Sidewalk);
constexpr std::uint8_t kRoadSurfaceCrosswalk = 1u << static_cast<std::uint8_t>(RoadLaneSurface::Crosswalk);
constexpr std::uint8_t kRoadSurfaceShoulder = 1u << static_cast<std::uint8_t>(RoadLaneSurface::Shoulder);
constexpr std::uint8_t kRoadSurfaceMedian = 1u << static_cast<std::uint8_t>(RoadLaneSurface::Median);

constexpr std::uint8_t kRoadSurfaceSidewalkEdgeMask = 0x0Fu;
constexpr std::uint8_t kRoadSurfaceCrosswalkShift = 4u;
constexpr std::uint8_t kRoadDividerWhiteShift = 0u;
constexpr std::uint8_t kRoadDividerYellowShift = 4u;
constexpr std::size_t kGroundRoadRenderChannelsPerTile = 4u;

std::uint8_t LaneIntentFromRoadDirection(std::uint8_t roadDirection);
std::uint8_t RoadDirectionsFromLaneIntent(std::uint8_t laneIntentMask);
std::uint8_t OppositeRoadDirection(std::uint8_t roadDirection);
std::uint8_t LaneTypeMaskFor(RoadLaneTypeId laneType);
bool IsRoadCarLaneType(RoadLaneTypeId laneType);
std::uint8_t SurfaceMaskFor(RoadLaneSurface surface);
std::uint8_t AxisMaskFor(RoadAxis axis);
RoadAxis AxisFromMask(std::uint8_t axisMask);
bool IsHorizontalAxis(RoadAxis axis);
bool IsVerticalAxis(RoadAxis axis);
bool HasHorizontalLane(std::uint8_t laneTravelMask);
bool HasVerticalLane(std::uint8_t laneTravelMask);
std::uint8_t TransportModeMaskFor(TransportMode mode);
int RoadDirectionIndex(std::uint8_t roadDirection);
std::uint8_t RoadDirectionFromIndex(int directionIndex);
int RoadDirectionDeltaX(std::uint8_t roadDirection);
int RoadDirectionDeltaY(std::uint8_t roadDirection);
