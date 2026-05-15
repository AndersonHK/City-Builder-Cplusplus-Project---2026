#include "Road.h"

#include <algorithm>
#include <cmath>

namespace {
const float kRoadLayoutEpsilon = 0.0001f;

std::uint8_t LowSideBitForAxis(RoadAxis axis) {
    return axis == RoadAxis::Horizontal ? kRoadDirectionNorth : kRoadDirectionWest;
}

std::uint8_t HighSideBitForAxis(RoadAxis axis) {
    return axis == RoadAxis::Horizontal ? kRoadDirectionSouth : kRoadDirectionEast;
}

bool IsTileInsideMap(int tileX, int tileY, int mapWidth, int mapHeight) {
    return tileX >= 0 && tileX < mapWidth && tileY >= 0 && tileY < mapHeight;
}

bool LaneRoleUsesDirectedFlow(RoadLaneRole laneRole) {
    return laneRole == RoadLaneRole::Through || laneRole == RoadLaneRole::Turn || laneRole == RoadLaneRole::Transit;
}

RoadAxis AxisBetween(const Int2& startTile, const Int2& endTile) {
    if (startTile == endTile) {
        return RoadAxis::None;
    }
    if (startTile.y == endTile.y) {
        return RoadAxis::Horizontal;
    }
    if (startTile.x == endTile.x) {
        return RoadAxis::Vertical;
    }
    return RoadAxis::None;
}

std::uint8_t DirectionBetween(const Int2& startTile, const Int2& endTile) {
    if (startTile.y == endTile.y) {
        return startTile.x <= endTile.x ? kRoadDirectionEast : kRoadDirectionWest;
    }
    if (startTile.x == endTile.x) {
        return startTile.y <= endTile.y ? kRoadDirectionSouth : kRoadDirectionNorth;
    }
    return 0;
}

bool DirectionIsPositive(std::uint8_t direction) {
    return direction == kRoadDirectionEast || direction == kRoadDirectionSouth;
}

int AxisCoordinate(const Int2& tile, RoadAxis axis) {
    return axis == RoadAxis::Horizontal ? tile.x : tile.y;
}

void SetAxisCoordinate(Int2& tile, RoadAxis axis, int coordinate) {
    if (axis == RoadAxis::Horizontal) {
        tile.x = coordinate;
    } else if (axis == RoadAxis::Vertical) {
        tile.y = coordinate;
    }
}

int DistanceToHorizontalSide(int localX, int footprint, std::uint8_t outsideDirection) {
    return outsideDirection == kRoadDirectionEast ? footprint - 1 - localX : localX;
}

int DistanceToVerticalSide(int localY, int footprint, std::uint8_t outsideDirection) {
    return outsideDirection == kRoadDirectionSouth ? footprint - 1 - localY : localY;
}

bool CornerWantsCarAxis(const RoadTilePlacement& placement, const Int2& cornerTile, int footprint, std::uint8_t horizontalOutsideDirection, std::uint8_t verticalOutsideDirection) {
    if (!placement.lanePlacement.isCar()) {
        return true;
    }
    if (placement.tileX < cornerTile.x ||
        placement.tileX >= cornerTile.x + footprint ||
        placement.tileY < cornerTile.y ||
        placement.tileY >= cornerTile.y + footprint) {
        return true;
    }

    const int localX = placement.tileX - cornerTile.x;
    const int localY = placement.tileY - cornerTile.y;
    const int horizontalDistance = DistanceToHorizontalSide(localX, footprint, horizontalOutsideDirection);
    const int verticalDistance = DistanceToVerticalSide(localY, footprint, verticalOutsideDirection);
    if (placement.lanePlacement.axis == RoadAxis::Horizontal) {
        return horizontalDistance <= verticalDistance;
    }
    if (placement.lanePlacement.axis == RoadAxis::Vertical) {
        return verticalDistance <= horizontalDistance;
    }
    return true;
}
}

RoadTemplate::RoadTemplate()
    : family(RoadFamily::LocalStreet),
      layer(TransportLayerId::Ground),
      trafficSide(RoadTrafficSide::RightHand),
      directionMode(RoadDirectionMode::TwoWay),
      overlapPolicy(RoadTemplateOverlapPolicy::AdapterFriendly),
      laneCount(1) {
}

RoadStrokeCommand::RoadStrokeCommand()
    : startTile(0, 0),
      cornerTile(0, 0),
      endTile(0, 0),
      family(RoadFamily::None),
      layer(TransportLayerId::Ground),
      operation(RoadStrokeOperation::Place) {
}

RoadTilePlacement::RoadTilePlacement()
    : tileX(0),
      tileY(0),
      tileIndex(0) {
}

Road::LayoutLane::LayoutLane()
    : start(0.0f),
      end(1.0f) {
}

Road::Road(const RoadTemplate& roadTemplate)
    : roadTemplate_(roadTemplate) {
    if (roadTemplate_.identity.id == 0) {
        roadTemplate_.identity.id = makeTemplateId(roadTemplate_.family, roadTemplate_.layer, roadTemplate_.laneCount, roadTemplate_.trafficSide, roadTemplate_.directionMode);
    }
    if (roadTemplate_.identity.footprint == 0) {
        roadTemplate_.identity.footprint = static_cast<std::uint8_t>(std::min(255, chooseTemplateFootprint(roadTemplate_)));
    }
}

const RoadTemplate& Road::templateData() const {
    return roadTemplate_;
}

bool Road::appendStrokePlacements(const Int2& startTile, const Int2& cornerTile, const Int2& endTile, int mapWidth, int mapHeight, std::vector<RoadTilePlacement>& placements) const {
    const RoadAxis firstAxis = AxisBetween(startTile, cornerTile);
    const RoadAxis secondAxis = AxisBetween(cornerTile, endTile);
    const int footprint = chooseTemplateFootprint(roadTemplate_);
    if (firstAxis != RoadAxis::None && secondAxis != RoadAxis::None && firstAxis != secondAxis && footprint > 1) {
        const std::uint8_t firstDirection = DirectionBetween(startTile, cornerTile);
        const std::uint8_t secondDirection = DirectionBetween(cornerTile, endTile);
        Int2 adjustedFirstCornerTile = cornerTile;
        if (DirectionIsPositive(firstDirection)) {
            SetAxisCoordinate(adjustedFirstCornerTile, firstAxis, AxisCoordinate(cornerTile, firstAxis) + footprint - 1);
        }

        Int2 adjustedEndTile = endTile;
        if (DirectionIsPositive(secondDirection)) {
            adjustedEndTile.x = secondAxis == RoadAxis::Horizontal ? std::max(adjustedEndTile.x, cornerTile.x + footprint) : adjustedEndTile.x;
            adjustedEndTile.y = secondAxis == RoadAxis::Vertical ? std::max(adjustedEndTile.y, cornerTile.y + footprint) : adjustedEndTile.y;
        } else if (secondDirection != 0) {
            adjustedEndTile.x = secondAxis == RoadAxis::Horizontal ? std::min(adjustedEndTile.x, cornerTile.x - 1) : adjustedEndTile.x;
            adjustedEndTile.y = secondAxis == RoadAxis::Vertical ? std::min(adjustedEndTile.y, cornerTile.y - 1) : adjustedEndTile.y;
        }

        std::vector<RoadTilePlacement> cornerPlacements;
        if (!appendLegPlacements(startTile, adjustedFirstCornerTile, mapWidth, mapHeight, cornerPlacements) ||
            !appendLegPlacements(cornerTile, adjustedEndTile, mapWidth, mapHeight, cornerPlacements)) {
            return false;
        }

        const std::uint8_t horizontalOutsideDirection = firstAxis == RoadAxis::Horizontal ? OppositeRoadDirection(firstDirection) : secondDirection;
        const std::uint8_t verticalOutsideDirection = firstAxis == RoadAxis::Vertical ? OppositeRoadDirection(firstDirection) : secondDirection;
        std::size_t placementIndex = 0;
        for (; placementIndex < cornerPlacements.size(); ++placementIndex) {
            if (CornerWantsCarAxis(cornerPlacements[placementIndex], cornerTile, footprint, horizontalOutsideDirection, verticalOutsideDirection)) {
                placements.push_back(cornerPlacements[placementIndex]);
            }
        }

        return true;
    }

    if (!appendLegPlacements(startTile, cornerTile, mapWidth, mapHeight, placements)) {
        return false;
    }

    return appendLegPlacements(cornerTile, endTile, mapWidth, mapHeight, placements);
}

RoadTemplate Road::makeTemplate(RoadFamily family, TransportLayerId layer, int laneCount, RoadTrafficSide trafficSide, RoadDirectionMode directionMode) {
    RoadTemplate roadTemplate;
    roadTemplate.family = family;
    roadTemplate.layer = layer;
    roadTemplate.trafficSide = trafficSide;
    roadTemplate.directionMode = directionMode;
    roadTemplate.laneCount = std::max(1, laneCount);
    roadTemplate.identity.id = makeTemplateId(family, layer, roadTemplate.laneCount, trafficSide, directionMode);
    roadTemplate.overlapPolicy = RoadTemplateOverlapPolicy::AdapterFriendly;

    const bool hasPedestrianEdgeLanes = family == RoadFamily::LocalStreet && layer == TransportLayerId::Ground;

    RoadTemplateElement pedestrianElement;
    pedestrianElement.laneType = RoadLaneTypeId::Pedestrian;
    pedestrianElement.surface = RoadLaneSurface::Sidewalk;
    pedestrianElement.laneRole = RoadLaneRole::Access;
    pedestrianElement.behavior.minimumWidth = hasPedestrianEdgeLanes ? 0.18f : 0.0f;
    pedestrianElement.behavior.preferredWidth = hasPedestrianEdgeLanes ? 0.25f : 0.0f;
    pedestrianElement.behavior.maximumWidth = hasPedestrianEdgeLanes ? 0.45f : 0.0f;
    pedestrianElement.behavior.connectorMask = kRoadDirectionNorth | kRoadDirectionEast | kRoadDirectionSouth | kRoadDirectionWest;

    RoadTemplateElement carElement;
    carElement.laneType = RoadLaneTypeId::Car;
    carElement.surface = RoadLaneSurface::Asphalt;
    carElement.laneRole = RoadLaneRole::Through;
    carElement.behavior.minimumWidth = 0.60f;
    carElement.behavior.preferredWidth = family == RoadFamily::Highway ? 0.90f : 0.75f;
    carElement.behavior.maximumWidth = 1.0f;
    carElement.behavior.connectorMask = kRoadDirectionNorth | kRoadDirectionEast | kRoadDirectionSouth | kRoadDirectionWest;

    if (hasPedestrianEdgeLanes) {
        roadTemplate.elements.push_back(pedestrianElement);
    }

    const int totalLaneElements = directionMode == RoadDirectionMode::TwoWay ? roadTemplate.laneCount * 2 : roadTemplate.laneCount;
    int laneIndex = 0;
    for (; laneIndex < totalLaneElements; ++laneIndex) {
        roadTemplate.elements.push_back(carElement);
    }

    if (hasPedestrianEdgeLanes) {
        roadTemplate.elements.push_back(pedestrianElement);
    }

    roadTemplate.identity.footprint = static_cast<std::uint8_t>(std::min(255, chooseTemplateFootprint(roadTemplate)));
    return roadTemplate;
}

std::uint16_t Road::makeTemplateId(RoadFamily family, TransportLayerId layer, int laneCount, RoadTrafficSide trafficSide, RoadDirectionMode directionMode) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(family) << 12) |
        (static_cast<std::uint16_t>(layer) << 10) |
        (static_cast<std::uint16_t>(trafficSide) << 8) |
        (static_cast<std::uint16_t>(directionMode) << 6) |
        static_cast<std::uint16_t>(std::max(0, std::min(63, laneCount))));
}

int Road::chooseTemplateFootprint(const RoadTemplate& roadTemplate) {
    float preferredWidth = 0.0f;
    float minimumWidth = 0.0f;
    std::size_t elementIndex = 0;
    for (; elementIndex < roadTemplate.elements.size(); ++elementIndex) {
        preferredWidth += roadTemplate.elements[elementIndex].behavior.preferredWidth;
        minimumWidth += roadTemplate.elements[elementIndex].behavior.minimumWidth;
    }

    int footprint = static_cast<int>(std::floor(preferredWidth + 0.5f));
    footprint = std::max(1, footprint);
    footprint = std::max(footprint, static_cast<int>(std::ceil(minimumWidth - kRoadLayoutEpsilon)));
    return footprint;
}

bool Road::appendLegPlacements(const Int2& startTile, const Int2& endTile, int mapWidth, int mapHeight, std::vector<RoadTilePlacement>& placements) const {
    if (startTile == endTile) {
        return true;
    }

    std::uint8_t forwardDirection = 0;
    std::uint8_t reverseDirection = 0;
    RoadAxis axis = RoadAxis::None;
    bool horizontal = false;
    if (startTile.y == endTile.y) {
        horizontal = true;
        axis = RoadAxis::Horizontal;
        forwardDirection = startTile.x <= endTile.x ? kRoadDirectionEast : kRoadDirectionWest;
        if (roadTemplate_.directionMode == RoadDirectionMode::TwoWay) {
            forwardDirection = kRoadDirectionEast;
        }
        reverseDirection = OppositeRoadDirection(forwardDirection);
    } else if (startTile.x == endTile.x) {
        axis = RoadAxis::Vertical;
        forwardDirection = startTile.y <= endTile.y ? kRoadDirectionSouth : kRoadDirectionNorth;
        if (roadTemplate_.directionMode == RoadDirectionMode::TwoWay) {
            forwardDirection = kRoadDirectionNorth;
        }
        reverseDirection = OppositeRoadDirection(forwardDirection);
    } else {
        return false;
    }

    const int footprint = chooseTemplateFootprint(roadTemplate_);
    std::vector<LayoutLane> layoutLanes = buildLayoutLanes(forwardDirection, reverseDirection, footprint);
    if (roadTemplate_.trafficSide == RoadTrafficSide::LeftHand && roadTemplate_.directionMode == RoadDirectionMode::TwoWay) {
        std::reverse(layoutLanes.begin(), layoutLanes.end());
        std::size_t mirroredIndex = 0;
        for (; mirroredIndex < layoutLanes.size(); ++mirroredIndex) {
            const float oldStart = layoutLanes[mirroredIndex].start;
            const float oldEnd = layoutLanes[mirroredIndex].end;
            layoutLanes[mirroredIndex].start = static_cast<float>(footprint) - oldEnd;
            layoutLanes[mirroredIndex].end = static_cast<float>(footprint) - oldStart;
        }
    }

    std::vector<RoadTemplateSeamKind> seamAfter(layoutLanes.size(), RoadTemplateSeamKind::None);
    std::size_t laneIndex = 0;
    for (; laneIndex + 1u < layoutLanes.size(); ++laneIndex) {
        seamAfter[laneIndex] = RoadTemplateSeamBetween(layoutLanes[laneIndex].lane, layoutLanes[laneIndex + 1u].lane);
    }

    const int minimumX = std::min(startTile.x, endTile.x);
    const int maximumX = std::max(startTile.x, endTile.x);
    const int minimumY = std::min(startTile.y, endTile.y);
    const int maximumY = std::max(startTile.y, endTile.y);

    int longitudinal = horizontal ? minimumX : minimumY;
    const int longitudinalEnd = horizontal ? maximumX : maximumY;
    for (; longitudinal <= longitudinalEnd; ++longitudinal) {
        for (laneIndex = 0; laneIndex < layoutLanes.size(); ++laneIndex) {
            const LayoutLane& layoutLane = layoutLanes[laneIndex];
            const int firstTile = std::max(0, static_cast<int>(std::floor(layoutLane.start + kRoadLayoutEpsilon)));
            const int lastTile = std::min(footprint - 1, static_cast<int>(std::ceil(layoutLane.end - kRoadLayoutEpsilon)) - 1);
            int crossOffset = firstTile;
            for (; crossOffset <= lastTile; ++crossOffset) {
                const int tileX = horizontal ? longitudinal : startTile.x + crossOffset;
                const int tileY = horizontal ? startTile.y + crossOffset : longitudinal;
                if (!IsTileInsideMap(tileX, tileY, mapWidth, mapHeight)) {
                    return false;
                }

                RoadTilePlacement tilePlacement;
                tilePlacement.tileX = tileX;
                tilePlacement.tileY = tileY;
                tilePlacement.tileIndex = (tileY * mapWidth) + tileX;
                tilePlacement.lanePlacement = makeLanePlacement(layoutLane, static_cast<int>(laneIndex), tileX, tileY, tilePlacement.tileIndex, axis, crossOffset, footprint, seamAfter);
                placements.push_back(tilePlacement);
            }
        }
    }

    return true;
}

std::vector<Road::LayoutLane> Road::buildLayoutLanes(std::uint8_t forwardDirection, std::uint8_t reverseDirection, int footprint) const {
    std::vector<LayoutLane> lanes;
    lanes.reserve(roadTemplate_.elements.size());

    int directedLaneOrdinal = 0;
    int directedLaneTotal = 0;
    std::size_t elementIndex = 0;
    for (; elementIndex < roadTemplate_.elements.size(); ++elementIndex) {
        if (LaneRoleUsesDirectedFlow(roadTemplate_.elements[elementIndex].laneRole)) {
            ++directedLaneTotal;
        }
    }

    float totalWidth = 0.0f;
    for (elementIndex = 0; elementIndex < roadTemplate_.elements.size(); ++elementIndex) {
        const RoadTemplateElement& element = roadTemplate_.elements[elementIndex];
        LayoutLane layoutLane;
        layoutLane.lane = RoadLane(element, static_cast<int>(elementIndex));
        if (LaneRoleUsesDirectedFlow(element.laneRole)) {
            std::uint8_t laneDirection = forwardDirection;
            if (roadTemplate_.directionMode == RoadDirectionMode::TwoWay && directedLaneOrdinal < directedLaneTotal / 2) {
                laneDirection = reverseDirection;
            } else if (roadTemplate_.directionMode == RoadDirectionMode::OneWayReverse) {
                laneDirection = reverseDirection;
            }
            layoutLane.lane.setTravel(LaneIntentFromRoadDirection(laneDirection));
            ++directedLaneOrdinal;
        } else if (element.laneRole == RoadLaneRole::Access) {
            layoutLane.lane.setTravel(static_cast<std::uint8_t>(LaneIntentFromRoadDirection(forwardDirection) | LaneIntentFromRoadDirection(reverseDirection)));
        }

        layoutLane.start = totalWidth;
        layoutLane.end = totalWidth + element.behavior.preferredWidth;
        totalWidth = layoutLane.end;
        lanes.push_back(layoutLane);
    }

    float remainingDelta = static_cast<float>(footprint) - totalWidth;
    for (int passIndex = 0; passIndex < 16 && std::fabs(remainingDelta) > kRoadLayoutEpsilon; ++passIndex) {
        float capacity = 0.0f;
        for (elementIndex = 0; elementIndex < lanes.size(); ++elementIndex) {
            const RoadElementBehavior& behavior = lanes[elementIndex].lane.behavior();
            const float width = lanes[elementIndex].end - lanes[elementIndex].start;
            capacity += remainingDelta > 0.0f ? std::max(0.0f, behavior.maximumWidth - width) : std::max(0.0f, width - behavior.minimumWidth);
        }

        if (capacity <= kRoadLayoutEpsilon) {
            break;
        }

        float applied = 0.0f;
        for (elementIndex = 0; elementIndex < lanes.size(); ++elementIndex) {
            const RoadElementBehavior& behavior = lanes[elementIndex].lane.behavior();
            const float width = lanes[elementIndex].end - lanes[elementIndex].start;
            const float elementCapacity = remainingDelta > 0.0f ? std::max(0.0f, behavior.maximumWidth - width) : std::max(0.0f, width - behavior.minimumWidth);
            if (elementCapacity <= kRoadLayoutEpsilon) {
                continue;
            }

            const float share = remainingDelta * (elementCapacity / capacity);
            const float clampedShare = remainingDelta > 0.0f ? std::min(share, elementCapacity) : std::max(share, -elementCapacity);
            lanes[elementIndex].end += clampedShare;
            applied += clampedShare;
        }

        remainingDelta -= applied;
    }

    float cursor = 0.0f;
    for (elementIndex = 0; elementIndex < lanes.size(); ++elementIndex) {
        const float width = lanes[elementIndex].end - lanes[elementIndex].start;
        lanes[elementIndex].start = cursor;
        lanes[elementIndex].end = cursor + width;
        cursor = lanes[elementIndex].end;
    }

    return lanes;
}

RoadLanePlacement Road::makeLanePlacement(const LayoutLane& layoutLane, int laneOrdinal, int tileX, int tileY, int tileIndex, RoadAxis axis, int crossOffset, int footprint, const std::vector<RoadTemplateSeamKind>& seamAfter) const {
    RoadLanePlacement lanePlacement;
    lanePlacement.tileX = tileX;
    lanePlacement.tileY = tileY;
    lanePlacement.tileIndex = tileIndex;
    lanePlacement.family = roadTemplate_.family;
    lanePlacement.layer = roadTemplate_.layer;
    lanePlacement.templateId = roadTemplate_.identity.id;
    lanePlacement.laneIndex = layoutLane.lane.laneIndex();
    lanePlacement.axis = axis;
    lanePlacement.crossSectionMask = static_cast<std::uint8_t>(1u << std::min(crossOffset, 7));
    lanePlacement.laneType = layoutLane.lane.laneType();
    lanePlacement.surface = layoutLane.lane.surface();
    lanePlacement.role = layoutLane.lane.role();
    lanePlacement.laneTravelMask = layoutLane.lane.laneTravelMask();
    lanePlacement.arrowTravelMask = layoutLane.lane.arrowTravelMask();
    lanePlacement.sideMin = std::max(0.0f, layoutLane.start - static_cast<float>(crossOffset));
    lanePlacement.sideMax = std::min(1.0f, layoutLane.end - static_cast<float>(crossOffset));

    const std::uint8_t lowSideBit = LowSideBitForAxis(axis);
    const std::uint8_t highSideBit = HighSideBitForAxis(axis);
    if (layoutLane.lane.surface() == RoadLaneSurface::Sidewalk) {
        if (lanePlacement.sideMin <= kRoadLayoutEpsilon) {
            lanePlacement.sidewalkEdgeMask |= lowSideBit;
        }
        if (lanePlacement.sideMax >= 1.0f - kRoadLayoutEpsilon) {
            lanePlacement.sidewalkEdgeMask |= highSideBit;
        }
    }

    const int previousLaneIndex = laneOrdinal - 1;
    if (previousLaneIndex >= 0 && previousLaneIndex < static_cast<int>(seamAfter.size()) && seamAfter[static_cast<std::size_t>(previousLaneIndex)] != RoadTemplateSeamKind::None) {
        const std::uint8_t seamBit = lanePlacement.sideMin < 0.5f ? lowSideBit : highSideBit;
        if (seamAfter[static_cast<std::size_t>(previousLaneIndex)] == RoadTemplateSeamKind::SameDirectionLaneDivider) {
            lanePlacement.sameDirectionDividerMask |= seamBit;
        } else {
            lanePlacement.opposingDirectionDividerMask |= seamBit;
        }
    }

    if (laneOrdinal >= 0 && laneOrdinal < static_cast<int>(seamAfter.size()) && seamAfter[static_cast<std::size_t>(laneOrdinal)] != RoadTemplateSeamKind::None) {
        const std::uint8_t seamBit = lanePlacement.sideMax < 0.5f ? lowSideBit : highSideBit;
        if (seamAfter[static_cast<std::size_t>(laneOrdinal)] == RoadTemplateSeamKind::SameDirectionLaneDivider) {
            lanePlacement.sameDirectionDividerMask |= seamBit;
        } else {
            lanePlacement.opposingDirectionDividerMask |= seamBit;
        }
    }

    return lanePlacement;
}
