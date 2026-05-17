#include "RoadTemplateDefinition.h"

#include <algorithm>

namespace {
const int kSlowLaneCapacity = 240;
const int kMediumLaneCapacity = 440;
const int kFastLaneCapacity = 680;
const int kPedestrianLaneCapacity = 1500;

RoadDirectionMode DirectionModeFromTemplateId(std::uint16_t templateId) {
    return static_cast<RoadDirectionMode>((templateId >> 6) & 0x3u);
}

RoadTemplateElement PrimaryLane(RoadLaneTypeId laneType) {
    RoadTemplateElement element;
    element.laneType = laneType;
    element.surface = RoadLaneSurface::Asphalt;
    element.laneRole = RoadLaneRole::Through;
    element.behavior.minimumWidth = 1.0f;
    element.behavior.preferredWidth = 1.0f;
    element.behavior.maximumWidth = 1.0f;
    element.behavior.connectorMask = kRoadDirectionNorth | kRoadDirectionEast | kRoadDirectionSouth | kRoadDirectionWest;
    return element;
}

int CapacityForLaneType(RoadLaneTypeId laneType) {
    if (laneType == RoadLaneTypeId::Fast) {
        return kFastLaneCapacity;
    }
    if (laneType == RoadLaneTypeId::Medium) {
        return kMediumLaneCapacity;
    }
    return kSlowLaneCapacity;
}

void SetPrimaryLane(RoadLaneCell& cell, RoadLaneTypeId laneType, const RoadLaneCellContext& context) {
    std::uint8_t travelDirectionMask = context.travelDirectionMask;
    if (travelDirectionMask == 0) {
        travelDirectionMask = context.directionMask;
    }
    std::uint8_t pathDirectionMask = context.pathDirectionMask;
    if (pathDirectionMask == 0) {
        pathDirectionMask = travelDirectionMask;
    }

    cell.primary.mode = CommuterMode::Car;
    cell.primary.laneType = laneType;
    cell.primary.capacity = CapacityForLaneType(laneType);
    cell.primary.directionMask = context.directionMask;
    cell.primary.travelDirectionMask = travelDirectionMask;
    cell.primary.pathDirectionMask = pathDirectionMask;
    cell.primary.centerSide = true;
    cell.primary.parallelGraphic = RoadGraphic::asphalt(context.directionMask);
}

void AttachSidewalk(RoadLaneCell& cell, const RoadLaneCellContext& context, bool inverted) {
    cell.secondary.mode = CommuterMode::Pedestrian;
    cell.secondary.laneType = RoadLaneTypeId::Pedestrian;
    cell.secondary.capacity = kPedestrianLaneCapacity;
    cell.secondary.directionMask = context.directionMask;
    cell.secondary.travelDirectionMask = cell.primary.travelDirectionMask;
    cell.secondary.pathDirectionMask = cell.primary.pathDirectionMask;
    cell.secondary.centerSide = inverted;
    cell.secondary.parallelGraphic = RoadGraphic::sidewalk(context.directionMask);
    cell.secondary.crossingGraphic = RoadGraphic::crosswalk(context.directionMask);
}

void AttachMedian(RoadLaneCell& cell, const RoadLaneCellContext& context) {
    cell.secondary.mode = CommuterMode::None;
    cell.secondary.laneType = RoadLaneTypeId::Separator;
    cell.secondary.capacity = 0;
    cell.secondary.directionMask = context.directionMask;
    cell.secondary.travelDirectionMask = cell.primary.travelDirectionMask;
    cell.secondary.pathDirectionMask = cell.primary.pathDirectionMask;
    cell.secondary.centerSide = true;
    cell.secondary.parallelGraphic = RoadGraphic::median(context.directionMask);
}

bool IsOneWayTemplate(const RoadLanePlacement& lanePlacement) {
    return DirectionModeFromTemplateId(lanePlacement.templateId) != RoadDirectionMode::TwoWay;
}

RoadLaneCell MakeCarAndSidewalkCell(const RoadLanePlacement& lanePlacement, RoadLaneTypeId laneType, const RoadLaneCellContext& context) {
    RoadLaneCell cell;
    if (!lanePlacement.isCar()) {
        return cell;
    }

    SetPrimaryLane(cell, laneType, context);
    AttachSidewalk(cell, context, IsOneWayTemplate(lanePlacement) && context.hasOneWayLeftNeighbor && !context.oneWayLeftNeighbor);
    return cell;
}

class StreetTemplateDefinition : public RoadTemplateDefinition {
public:
    RoadTemplateKind kind() const override {
        return RoadTemplateKind::Street;
    }

    int normalizeLaneCount(int laneCount, RoadDirectionMode directionMode) const override {
        if (directionMode != RoadDirectionMode::TwoWay) {
            return 2;
        }
        return std::max(1, laneCount);
    }

    void buildElements(RoadTemplate& roadTemplate) const override {
        const RoadLaneTypeId laneType = roadTemplate.directionMode == RoadDirectionMode::TwoWay ? RoadLaneTypeId::Slow : RoadLaneTypeId::Fast;
        const int elementCount = roadTemplate.directionMode == RoadDirectionMode::TwoWay ? roadTemplate.laneCount * 2 : roadTemplate.laneCount;
        for (int elementIndex = 0; elementIndex < elementCount; ++elementIndex) {
            roadTemplate.elements.push_back(PrimaryLane(laneType));
        }
    }

    RoadLaneCell makeLaneCell(const RoadLanePlacement& lanePlacement, const RoadLaneCellContext& context) const override {
        const RoadLaneTypeId laneType = IsOneWayTemplate(lanePlacement) ? RoadLaneTypeId::Fast : RoadLaneTypeId::Slow;
        return MakeCarAndSidewalkCell(lanePlacement, laneType, context);
    }
};

class RoadTemplateDefinitionImpl : public RoadTemplateDefinition {
public:
    RoadTemplateKind kind() const override {
        return RoadTemplateKind::Road;
    }

    int normalizeLaneCount(int laneCount, RoadDirectionMode directionMode) const override {
        if (directionMode != RoadDirectionMode::TwoWay) {
            return 2;
        }
        return std::max(1, laneCount);
    }

    void buildElements(RoadTemplate& roadTemplate) const override {
        const RoadLaneTypeId laneType = roadTemplate.directionMode == RoadDirectionMode::TwoWay ? RoadLaneTypeId::Medium : RoadLaneTypeId::Fast;
        const int elementCount = roadTemplate.directionMode == RoadDirectionMode::TwoWay ? roadTemplate.laneCount * 2 : roadTemplate.laneCount;
        for (int elementIndex = 0; elementIndex < elementCount; ++elementIndex) {
            roadTemplate.elements.push_back(PrimaryLane(laneType));
        }
    }

    RoadLaneCell makeLaneCell(const RoadLanePlacement& lanePlacement, const RoadLaneCellContext& context) const override {
        const RoadLaneTypeId laneType = IsOneWayTemplate(lanePlacement) ? RoadLaneTypeId::Fast : RoadLaneTypeId::Medium;
        return MakeCarAndSidewalkCell(lanePlacement, laneType, context);
    }
};

class AvenueTemplateDefinition : public RoadTemplateDefinition {
public:
    RoadTemplateKind kind() const override {
        return RoadTemplateKind::Avenue;
    }

    RoadDirectionMode normalizeDirectionMode(RoadDirectionMode) const override {
        return RoadDirectionMode::TwoWay;
    }

    int normalizeLaneCount(int, RoadDirectionMode) const override {
        return 2;
    }

    void buildElements(RoadTemplate& roadTemplate) const override {
        roadTemplate.elements.push_back(PrimaryLane(RoadLaneTypeId::Medium));
        roadTemplate.elements.push_back(PrimaryLane(RoadLaneTypeId::Fast));
        roadTemplate.elements.push_back(PrimaryLane(RoadLaneTypeId::Fast));
        roadTemplate.elements.push_back(PrimaryLane(RoadLaneTypeId::Medium));
    }

    RoadLaneCell makeLaneCell(const RoadLanePlacement& lanePlacement, const RoadLaneCellContext& context) const override {
        RoadLaneCell cell;
        if (!lanePlacement.isCar()) {
            return cell;
        }

        const bool outerTile = context.hasAvenueTileRole ? context.avenueOuterTile : lanePlacement.laneType == RoadLaneTypeId::Medium;
        SetPrimaryLane(cell, outerTile ? RoadLaneTypeId::Medium : RoadLaneTypeId::Fast, context);
        if (outerTile) {
            AttachSidewalk(cell, context, false);
        } else {
            AttachMedian(cell, context);
        }
        return cell;
    }
};

class HighwayTemplateDefinition : public RoadTemplateDefinition {
public:
    RoadTemplateKind kind() const override {
        return RoadTemplateKind::Highway;
    }

    RoadFamily family() const override {
        return RoadFamily::Highway;
    }

    TransportLayerId layer() const override {
        return TransportLayerId::Elevated;
    }

    void buildElements(RoadTemplate& roadTemplate) const override {
        const int elementCount = roadTemplate.directionMode == RoadDirectionMode::TwoWay ? roadTemplate.laneCount * 2 : roadTemplate.laneCount;
        for (int elementIndex = 0; elementIndex < elementCount; ++elementIndex) {
            roadTemplate.elements.push_back(PrimaryLane(RoadLaneTypeId::Fast));
        }
    }

    RoadLaneCell makeLaneCell(const RoadLanePlacement& lanePlacement, const RoadLaneCellContext& context) const override {
        RoadLaneCell cell;
        if (!lanePlacement.isCar()) {
            return cell;
        }

        SetPrimaryLane(cell, RoadLaneTypeId::Fast, context);
        return cell;
    }
};

const StreetTemplateDefinition kStreetTemplate;
const RoadTemplateDefinitionImpl kRoadTemplate;
const AvenueTemplateDefinition kAvenueTemplate;
const HighwayTemplateDefinition kHighwayTemplate;
}

RoadLaneCellContext::RoadLaneCellContext()
    : directionMask(0),
      travelDirectionMask(0),
      pathDirectionMask(0),
      hasAvenueTileRole(false),
      avenueOuterTile(true),
      hasOneWayLeftNeighbor(false),
      oneWayLeftNeighbor(false) {
}

RoadTemplateDefinition::~RoadTemplateDefinition() {
}

RoadFamily RoadTemplateDefinition::family() const {
    return RoadFamily::LocalStreet;
}

TransportLayerId RoadTemplateDefinition::layer() const {
    return TransportLayerId::Ground;
}

RoadDirectionMode RoadTemplateDefinition::normalizeDirectionMode(RoadDirectionMode directionMode) const {
    return directionMode;
}

int RoadTemplateDefinition::normalizeLaneCount(int laneCount, RoadDirectionMode) const {
    return std::max(1, laneCount);
}

const RoadTemplateDefinition& RoadTemplateDefinition::forKind(RoadTemplateKind templateKind) {
    switch (templateKind) {
        case RoadTemplateKind::Road:
            return kRoadTemplate;
        case RoadTemplateKind::Avenue:
            return kAvenueTemplate;
        case RoadTemplateKind::Highway:
            return kHighwayTemplate;
        default:
            return kStreetTemplate;
    }
}
