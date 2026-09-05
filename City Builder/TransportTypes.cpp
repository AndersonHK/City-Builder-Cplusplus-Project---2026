#include "TransportTypes.h"

std::uint8_t LaneIntentFromRoadDirection(std::uint8_t roadDirection) {
    switch (roadDirection) {
        case kRoadDirectionNorth:
            return kLaneIntentNorth;
        case kRoadDirectionEast:
            return kLaneIntentEast;
        case kRoadDirectionSouth:
            return kLaneIntentSouth;
        case kRoadDirectionWest:
            return kLaneIntentWest;
        default:
            return 0;
    }
}

std::uint8_t RoadDirectionsFromLaneIntent(std::uint8_t laneIntentMask) {
    std::uint8_t roadDirectionMask = 0;
    if ((laneIntentMask & kLaneIntentNorth) != 0) {
        roadDirectionMask |= kRoadDirectionNorth;
    }
    if ((laneIntentMask & kLaneIntentEast) != 0) {
        roadDirectionMask |= kRoadDirectionEast;
    }
    if ((laneIntentMask & kLaneIntentSouth) != 0) {
        roadDirectionMask |= kRoadDirectionSouth;
    }
    if ((laneIntentMask & kLaneIntentWest) != 0) {
        roadDirectionMask |= kRoadDirectionWest;
    }

    return roadDirectionMask;
}

std::uint8_t OppositeRoadDirection(std::uint8_t roadDirection) {
    switch (roadDirection) {
        case kRoadDirectionNorth:
            return kRoadDirectionSouth;
        case kRoadDirectionEast:
            return kRoadDirectionWest;
        case kRoadDirectionSouth:
            return kRoadDirectionNorth;
        case kRoadDirectionWest:
            return kRoadDirectionEast;
        default:
            return 0;
    }
}

std::uint8_t LaneTypeMaskFor(RoadLaneTypeId laneType) {
    return static_cast<std::uint8_t>(1u << static_cast<std::uint8_t>(laneType));
}

bool IsRoadCarLaneType(RoadLaneTypeId laneType) {
    return laneType == RoadLaneTypeId::Slow ||
        laneType == RoadLaneTypeId::Medium ||
        laneType == RoadLaneTypeId::Fast;
}

std::uint8_t SurfaceMaskFor(RoadLaneSurface surface) {
    return static_cast<std::uint8_t>(1u << static_cast<std::uint8_t>(surface));
}

std::uint8_t AxisMaskFor(RoadAxis axis) {
    return static_cast<std::uint8_t>(axis);
}

RoadAxis AxisFromMask(std::uint8_t axisMask) {
    if ((axisMask & AxisMaskFor(RoadAxis::Horizontal)) != 0) {
        return RoadAxis::Horizontal;
    }
    if ((axisMask & AxisMaskFor(RoadAxis::Vertical)) != 0) {
        return RoadAxis::Vertical;
    }
    return RoadAxis::None;
}

bool IsHorizontalAxis(RoadAxis axis) {
    return axis == RoadAxis::Horizontal;
}

bool IsVerticalAxis(RoadAxis axis) {
    return axis == RoadAxis::Vertical;
}

bool HasHorizontalLane(std::uint8_t laneTravelMask) {
    return (laneTravelMask & (kLaneIntentEast | kLaneIntentWest)) != 0;
}

bool HasVerticalLane(std::uint8_t laneTravelMask) {
    return (laneTravelMask & (kLaneIntentNorth | kLaneIntentSouth)) != 0;
}

std::uint8_t TransportModeMaskFor(TransportMode mode) {
    return static_cast<std::uint8_t>(1u << static_cast<std::uint8_t>(mode));
}

int RoadDirectionIndex(std::uint8_t roadDirection) {
    switch (roadDirection) {
        case kRoadDirectionNorth:
            return 0;
        case kRoadDirectionEast:
            return 1;
        case kRoadDirectionSouth:
            return 2;
        case kRoadDirectionWest:
            return 3;
        case kRoadDirectionNorthEast:
            return 4;
        case kRoadDirectionSouthEast:
            return 5;
        case kRoadDirectionSouthWest:
            return 6;
        case kRoadDirectionNorthWest:
            return 7;
        default:
            return -1;
    }
}

std::uint8_t RoadDirectionFromIndex(int directionIndex) {
    switch (directionIndex) {
        case 0:
            return kRoadDirectionNorth;
        case 1:
            return kRoadDirectionEast;
        case 2:
            return kRoadDirectionSouth;
        case 3:
            return kRoadDirectionWest;
        case 4:
            return kRoadDirectionNorthEast;
        case 5:
            return kRoadDirectionSouthEast;
        case 6:
            return kRoadDirectionSouthWest;
        case 7:
            return kRoadDirectionNorthWest;
        default:
            return 0;
    }
}

int RoadDirectionDeltaX(std::uint8_t roadDirection) {
    switch (roadDirection) {
        case kRoadDirectionEast:
        case kRoadDirectionNorthEast:
        case kRoadDirectionSouthEast:
            return 1;
        case kRoadDirectionWest:
        case kRoadDirectionSouthWest:
        case kRoadDirectionNorthWest:
            return -1;
        default:
            return 0;
    }
}

int RoadDirectionDeltaY(std::uint8_t roadDirection) {
    switch (roadDirection) {
        case kRoadDirectionSouth:
        case kRoadDirectionSouthEast:
        case kRoadDirectionSouthWest:
            return 1;
        case kRoadDirectionNorth:
        case kRoadDirectionNorthEast:
        case kRoadDirectionNorthWest:
            return -1;
        default:
            return 0;
    }
}
