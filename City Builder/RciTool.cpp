#include "RciTool.h"

#include "SimpleXml.h"
#include "Tile.h"
#include "TransportTypes.h"

#include <algorithm>

namespace {
struct Segment {
    int startOffset;
    int length;

    Segment()
        : startOffset(0),
          length(0) {
    }

    Segment(int start, int segmentLength)
        : startOffset(start),
          length(segmentLength) {
    }
};

const int kPlannedLocalRoadFootprint = 2;
const int kGridSnapTolerance = 3;
const int kRoadFacingNorth = 0;
const int kRoadFacingSouth = 1;
const int kRoadFacingWest = 2;
const int kRoadFacingEast = 3;

std::string TrimAscii(const std::string& value) {
    std::string::size_type first = 0u;
    while (first < value.size() && (value[first] == ' ' || value[first] == '\t' || value[first] == '\r' || value[first] == '\n')) {
        ++first;
    }

    std::string::size_type last = value.size();
    while (last > first && (value[last - 1u] == ' ' || value[last - 1u] == '\t' || value[last - 1u] == '\r' || value[last - 1u] == '\n')) {
        --last;
    }

    return value.substr(first, last - first);
}

RciColor AttributeColorValue(const std::string& tag, const RciColor& fallback) {
    return RciColor(
        XmlAttributeFloatValue(tag, "colorR", fallback.r),
        XmlAttributeFloatValue(tag, "colorG", fallback.g),
        XmlAttributeFloatValue(tag, "colorB", fallback.b),
        XmlAttributeFloatValue(tag, "colorA", fallback.a));
}

std::uint16_t ZoningTypeFromText(const std::string& value) {
    const std::string normalized = TrimAscii(value);
    if (normalized == "3" || normalized == "residential_low" || normalized == "low_residential" || normalized == "low_density_residential" || normalized == "residentialLow" || normalized == "TileZoningResidentialLow") {
        return TileZoningResidentialLow;
    }

    if (normalized == "1" || normalized == "residential" || normalized == "residence" || normalized == "residential_high" || normalized == "high_residential" || normalized == "high_density_residential" || normalized == "r" || normalized == "TileZoningResidential" || normalized == "TileZoningResidentialHigh") {
        return TileZoningResidentialHigh;
    }

    if (normalized == "2" || normalized == "industrial" || normalized == "industry" || normalized == "i" || normalized == "TileZoningIndustrial") {
        return TileZoningIndustrial;
    }

    return TileZoningNone;
}

std::uint16_t ZoningTypeFromToolTag(const std::string& tag, const std::string& id) {
    const std::uint16_t explicitType = ZoningTypeFromText(XmlAttributeValue(tag, "zoningType", std::string()));
    if (explicitType != TileZoningNone) {
        return explicitType;
    }

    return ZoningTypeFromText(id);
}

std::vector<std::uint16_t> ZoningTypesFromListText(const std::string& value) {
    std::vector<std::uint16_t> zoningTypes;
    std::string token;
    std::size_t cursor = 0u;
    for (; cursor <= value.size(); ++cursor) {
        if (cursor < value.size() && value[cursor] != ',' && value[cursor] != ';' && value[cursor] != '|') {
            token.push_back(value[cursor]);
            continue;
        }

        const std::string trimmedToken = TrimAscii(token);
        if (!trimmedToken.empty()) {
            const std::uint16_t zoningType = ZoningTypeFromText(trimmedToken);
            if (zoningType != TileZoningNone &&
                std::find(zoningTypes.begin(), zoningTypes.end(), zoningType) == zoningTypes.end()) {
                zoningTypes.push_back(zoningType);
            }
        }
        token.clear();
    }

    return zoningTypes;
}

std::vector<std::uint16_t> ZoningTypesFromRciTypeTag(const std::string& tag) {
    std::vector<std::uint16_t> zoningTypes = ZoningTypesFromListText(XmlAttributeValue(tag, "zoneTypes", std::string()));
    if (!zoningTypes.empty()) {
        return zoningTypes;
    }

    zoningTypes = ZoningTypesFromListText(XmlAttributeValue(tag, "zones", std::string()));
    if (!zoningTypes.empty()) {
        return zoningTypes;
    }

    const std::uint16_t zoningType = ZoningTypeFromText(XmlAttributeValue(tag, "zoningType", std::string()));
    if (zoningType != TileZoningNone) {
        zoningTypes.push_back(zoningType);
    }

    return zoningTypes;
}

bool IsResidentialZoningType(std::uint16_t zoningType) {
    return zoningType == TileZoningResidentialLow || zoningType == TileZoningResidentialHigh;
}

RciColor DefaultZoneColor(std::uint16_t zoningType) {
    if (zoningType == TileZoningResidentialLow) {
        return RciColor(0.44f, 0.92f, 0.46f, 0.50f);
    }
    if (zoningType == TileZoningResidentialHigh) {
        return RciColor(0.10f, 0.48f, 0.20f, 0.50f);
    }
    if (zoningType == TileZoningIndustrial) {
        return RciColor(0.92f, 0.76f, 0.15f, 0.50f);
    }

    return RciColor(0.45f, 0.45f, 0.45f, 0.50f);
}

int ClampInt(int value, int minimum, int maximum) {
    return std::max(minimum, std::min(value, maximum));
}

RciRect NormalizeBounds(int startTileX, int startTileY, int endTileX, int endTileY, int mapWidth, int mapHeight) {
    if (mapWidth <= 0 || mapHeight <= 0) {
        return RciRect();
    }

    return RciRect(
        ClampInt(std::min(startTileX, endTileX), 0, mapWidth - 1),
        ClampInt(std::min(startTileY, endTileY), 0, mapHeight - 1),
        ClampInt(std::max(startTileX, endTileX), 0, mapWidth - 1),
        ClampInt(std::max(startTileY, endTileY), 0, mapHeight - 1));
}

bool PartitionSegments(int length, int minimum, int preferred, int maximum, std::vector<Segment>& segments) {
    segments.clear();
    if (length <= 0 || minimum <= 0 || maximum < minimum || length < minimum) {
        return false;
    }

    int bestCount = 0;
    int bestScore = 0;
    const int maxCount = length / minimum;
    int count = 1;
    for (; count <= maxCount; ++count) {
        const int base = length / count;
        const int extra = length % count;
        const int largest = base + (extra > 0 ? 1 : 0);
        if (base < minimum || largest > maximum) {
            continue;
        }

        const int score = (count - extra) * std::abs(base - preferred) + extra * std::abs(largest - preferred);
        if (bestCount == 0 || score < bestScore) {
            bestCount = count;
            bestScore = score;
        }
    }

    if (bestCount == 0) {
        return false;
    }

    const int base = length / bestCount;
    const int extra = length % bestCount;
    int cursor = 0;
    for (count = 0; count < bestCount; ++count) {
        const int segmentLength = base + (count < extra ? 1 : 0);
        segments.push_back(Segment(cursor, segmentLength));
        cursor += segmentLength;
    }

    return true;
}

bool PartitionBlocksWithRoads(int totalLength, int minimum, int preferred, int maximum, std::vector<Segment>& blocks, std::vector<int>& roadOffsets) {
    blocks.clear();
    roadOffsets.clear();
    if (totalLength < minimum + (kPlannedLocalRoadFootprint * 2) || minimum <= 0 || maximum < minimum) {
        return false;
    }

    const int interiorLength = totalLength - (kPlannedLocalRoadFootprint * 2);
    const int maxCount = std::max(1, (interiorLength + kPlannedLocalRoadFootprint) / (minimum + kPlannedLocalRoadFootprint));
    int bestCount = 0;
    int bestScore = 0;
    int count = 1;
    for (; count <= maxCount; ++count) {
        const int blockOnlyLength = interiorLength - ((count - 1) * kPlannedLocalRoadFootprint);
        if (blockOnlyLength < count * minimum) {
            continue;
        }

        const int base = blockOnlyLength / count;
        const int extra = blockOnlyLength % count;
        const int largest = base + (extra > 0 ? 1 : 0);
        if (base < minimum || largest > maximum) {
            continue;
        }

        const int score = (count - extra) * std::abs(base - preferred) + extra * std::abs(largest - preferred);
        if (bestCount == 0 || score < bestScore) {
            bestCount = count;
            bestScore = score;
        }
    }

    if (bestCount == 0) {
        return false;
    }

    roadOffsets.push_back(0);
    const int blockOnlyLength = interiorLength - ((bestCount - 1) * kPlannedLocalRoadFootprint);
    const int base = blockOnlyLength / bestCount;
    const int extra = blockOnlyLength % bestCount;
    int cursor = kPlannedLocalRoadFootprint;
    for (count = 0; count < bestCount; ++count) {
        const int segmentLength = base + (count < extra ? 1 : 0);
        blocks.push_back(Segment(cursor, segmentLength));
        cursor += segmentLength;
        if (count + 1 < bestCount) {
            roadOffsets.push_back(cursor);
            cursor += kPlannedLocalRoadFootprint;
        }
    }
    roadOffsets.push_back(totalLength - kPlannedLocalRoadFootprint);
    return true;
}

bool SplitBlockIntoTwoDepths(int blockDepth, int minimum, int preferred, int maximum, int& firstDepth, int& secondDepth) {
    int bestFirst = 0;
    int bestSecond = 0;
    int bestScore = 0;
    int candidateFirst = minimum;
    for (; candidateFirst <= maximum; ++candidateFirst) {
        const int candidateSecond = blockDepth - candidateFirst;
        if (candidateSecond < minimum || candidateSecond > maximum) {
            continue;
        }

        const int score = std::abs(candidateFirst - preferred) + std::abs(candidateSecond - preferred) + std::abs(candidateFirst - candidateSecond);
        if (bestFirst == 0 || score < bestScore) {
            bestFirst = candidateFirst;
            bestSecond = candidateSecond;
            bestScore = score;
        }
    }

    if (bestFirst == 0) {
        return false;
    }

    firstDepth = bestFirst;
    secondDepth = bestSecond;
    return true;
}

int TileLinearIndex(int tileX, int tileY, int mapWidth) {
    return (tileY * mapWidth) + tileX;
}

bool MaskAt(const std::vector<std::uint8_t>& mask, int mapWidth, int mapHeight, int tileX, int tileY) {
    if (tileX < 0 || tileY < 0 || tileX >= mapWidth || tileY >= mapHeight) {
        return false;
    }

    const int index = TileLinearIndex(tileX, tileY, mapWidth);
    return index >= 0 && index < static_cast<int>(mask.size()) && mask[static_cast<std::size_t>(index)] != 0;
}

void SetMaskAt(std::vector<std::uint8_t>& mask, int mapWidth, int mapHeight, int tileX, int tileY, std::uint8_t value) {
    if (tileX < 0 || tileY < 0 || tileX >= mapWidth || tileY >= mapHeight) {
        return;
    }

    const int index = TileLinearIndex(tileX, tileY, mapWidth);
    if (index >= 0 && index < static_cast<int>(mask.size())) {
        mask[static_cast<std::size_t>(index)] = value;
    }
}

bool AxisMaskAt(const RciPlanningContext& context, int tileX, int tileY, RoadAxis axis) {
    const std::uint8_t axisMask = AxisMaskFor(axis);
    if (context.groundRoadAxisMasks.empty()) {
        return MaskAt(context.groundRoadTiles, context.mapWidth, context.mapHeight, tileX, tileY);
    }

    if (tileX < 0 || tileY < 0 || tileX >= context.mapWidth || tileY >= context.mapHeight) {
        return false;
    }

    const int index = TileLinearIndex(tileX, tileY, context.mapWidth);
    return index >= 0 &&
        index < static_cast<int>(context.groundRoadAxisMasks.size()) &&
        (context.groundRoadAxisMasks[static_cast<std::size_t>(index)] & axisMask) != 0;
}

bool RoadPlanIsVertical(const RciRoadPlan& roadPlan) {
    return roadPlan.startTileX == roadPlan.endTileX;
}

bool RoadPlanIsHorizontal(const RciRoadPlan& roadPlan) {
    return roadPlan.startTileY == roadPlan.endTileY;
}

bool SameRoadPlan(const RciRoadPlan& left, const RciRoadPlan& right) {
    return left.startTileX == right.startTileX &&
        left.startTileY == right.startTileY &&
        left.endTileX == right.endTileX &&
        left.endTileY == right.endTileY;
}

void AddRoadPlan(RciPlan& plan, const RciRoadPlan& roadPlan) {
    if (roadPlan.startTileX == roadPlan.endTileX && roadPlan.startTileY == roadPlan.endTileY) {
        return;
    }

    std::size_t roadIndex = 0;
    for (; roadIndex < plan.roadPlans.size(); ++roadIndex) {
        if (SameRoadPlan(plan.roadPlans[roadIndex], roadPlan)) {
            return;
        }
    }

    plan.roadPlans.push_back(roadPlan);
}

void MarkRoadFootprint(const RciRoadPlan& roadPlan, const RciPlanningContext& context, std::vector<std::uint8_t>& roadMask) {
    if (RoadPlanIsVertical(roadPlan)) {
        const int minY = std::min(roadPlan.startTileY, roadPlan.endTileY);
        const int maxY = std::max(roadPlan.startTileY, roadPlan.endTileY);
        int tileY = minY;
        for (; tileY <= maxY; ++tileY) {
            int roadOffset = 0;
            for (; roadOffset < kPlannedLocalRoadFootprint; ++roadOffset) {
                SetMaskAt(roadMask, context.mapWidth, context.mapHeight, roadPlan.startTileX + roadOffset, tileY, 1u);
            }
        }
        return;
    }

    if (RoadPlanIsHorizontal(roadPlan)) {
        const int minX = std::min(roadPlan.startTileX, roadPlan.endTileX);
        const int maxX = std::max(roadPlan.startTileX, roadPlan.endTileX);
        int tileX = minX;
        for (; tileX <= maxX; ++tileX) {
            int roadOffset = 0;
            for (; roadOffset < kPlannedLocalRoadFootprint; ++roadOffset) {
                SetMaskAt(roadMask, context.mapWidth, context.mapHeight, tileX, roadPlan.startTileY + roadOffset, 1u);
            }
        }
    }
}

bool PaintableAt(const RciPlanningContext& context, int tileX, int tileY) {
    return MaskAt(context.paintableTiles, context.mapWidth, context.mapHeight, tileX, tileY);
}

bool RoadAt(const RciPlanningContext& context, const std::vector<std::uint8_t>& roadMask, int tileX, int tileY) {
    return MaskAt(context.groundRoadTiles, context.mapWidth, context.mapHeight, tileX, tileY) ||
        MaskAt(roadMask, context.mapWidth, context.mapHeight, tileX, tileY);
}

bool ParcelAvailableAt(const RciPlanningContext& context, const std::vector<std::uint8_t>& roadMask, const std::vector<std::uint8_t>& blockedMask, int tileX, int tileY) {
    return PaintableAt(context, tileX, tileY) &&
        !RoadAt(context, roadMask, tileX, tileY) &&
        !MaskAt(blockedMask, context.mapWidth, context.mapHeight, tileX, tileY);
}

bool RoadableAt(const RciPlanningContext& context, int tileX, int tileY) {
    return PaintableAt(context, tileX, tileY) ||
        MaskAt(context.groundRoadTiles, context.mapWidth, context.mapHeight, tileX, tileY);
}

bool RoadableAt(const RciPlanningContext& context, const std::vector<std::uint8_t>& roadMask, int tileX, int tileY) {
    return RoadableAt(context, tileX, tileY) ||
        MaskAt(roadMask, context.mapWidth, context.mapHeight, tileX, tileY);
}

bool RoadPlanCoversTile(const RciRoadPlan& roadPlan, int tileX, int tileY) {
    int minX = std::min(roadPlan.startTileX, roadPlan.endTileX);
    int maxX = std::max(roadPlan.startTileX, roadPlan.endTileX);
    int minY = std::min(roadPlan.startTileY, roadPlan.endTileY);
    int maxY = std::max(roadPlan.startTileY, roadPlan.endTileY);
    if (RoadPlanIsVertical(roadPlan)) {
        maxX = minX + kPlannedLocalRoadFootprint - 1;
    } else {
        maxY = minY + kPlannedLocalRoadFootprint - 1;
    }

    return tileX >= minX && tileX <= maxX && tileY >= minY && tileY <= maxY;
}

bool PlannedPerpendicularRoadAtLinePosition(const RciPlan& plan, bool vertical, int origin, int position) {
    std::size_t roadIndex = 0;
    for (; roadIndex < plan.roadPlans.size(); ++roadIndex) {
        const RciRoadPlan& roadPlan = plan.roadPlans[roadIndex];
        if (vertical == RoadPlanIsVertical(roadPlan)) {
            continue;
        }

        if (vertical) {
            if (RoadPlanCoversTile(roadPlan, origin, position) ||
                RoadPlanCoversTile(roadPlan, origin + 1, position)) {
                return true;
            }
        } else {
            if (RoadPlanCoversTile(roadPlan, position, origin) ||
                RoadPlanCoversTile(roadPlan, position, origin + 1)) {
                return true;
            }
        }
    }

    return false;
}

bool ExistingPerpendicularRoadAtLinePosition(const RciPlanningContext& context, bool vertical, int origin, int position) {
    if (vertical) {
        return AxisMaskAt(context, origin, position, RoadAxis::Horizontal) ||
            AxisMaskAt(context, origin + 1, position, RoadAxis::Horizontal);
    }

    return AxisMaskAt(context, position, origin, RoadAxis::Vertical) ||
        AxisMaskAt(context, position, origin + 1, RoadAxis::Vertical);
}

bool PerpendicularRoadAtLinePosition(const RciPlanningContext& context, const RciPlan& plan, bool vertical, int origin, int position) {
    return ExistingPerpendicularRoadAtLinePosition(context, vertical, origin, position) ||
        PlannedPerpendicularRoadAtLinePosition(plan, vertical, origin, position);
}

std::uint8_t FrontDirectionForRoadFacing(int roadFacingDirection) {
    if (roadFacingDirection == kRoadFacingSouth) {
        return kRoadDirectionSouth;
    }

    if (roadFacingDirection == kRoadFacingWest) {
        return kRoadDirectionWest;
    }

    if (roadFacingDirection == kRoadFacingEast) {
        return kRoadDirectionEast;
    }

    return kRoadDirectionNorth;
}

RciRect MapRoadFacingLotRect(int roadFacingDirection, int frontageStartX, int frontageStartY, const RciRect& localRect) {
    if (roadFacingDirection == kRoadFacingSouth) {
        return RciRect(
            frontageStartX + localRect.minTileX,
            frontageStartY - localRect.maxTileY,
            frontageStartX + localRect.maxTileX,
            frontageStartY - localRect.minTileY);
    }

    if (roadFacingDirection == kRoadFacingWest) {
        return RciRect(
            frontageStartX + localRect.minTileY,
            frontageStartY + localRect.minTileX,
            frontageStartX + localRect.maxTileY,
            frontageStartY + localRect.maxTileX);
    }

    if (roadFacingDirection == kRoadFacingEast) {
        return RciRect(
            frontageStartX - localRect.maxTileY,
            frontageStartY + localRect.minTileX,
            frontageStartX - localRect.minTileY,
            frontageStartY + localRect.maxTileX);
    }

    return RciRect(
        frontageStartX + localRect.minTileX,
        frontageStartY + localRect.minTileY,
        frontageStartX + localRect.maxTileX,
        frontageStartY + localRect.maxTileY);
}

bool RectParcelAvailable(const RciPlanningContext& context, const std::vector<std::uint8_t>& roadMask, const std::vector<std::uint8_t>& blockedMask, const RciRect& rect) {
    if (!rect.isValid()) {
        return false;
    }

    int tileY = rect.minTileY;
    for (; tileY <= rect.maxTileY; ++tileY) {
        int tileX = rect.minTileX;
        for (; tileX <= rect.maxTileX; ++tileX) {
            if (!ParcelAvailableAt(context, roadMask, blockedMask, tileX, tileY)) {
                return false;
            }
        }
    }

    return true;
}

void MarkParcelBlocked(const RciPlanningContext& context, const RciRect& rect, std::vector<std::uint8_t>& blockedMask) {
    int tileY = rect.minTileY;
    for (; tileY <= rect.maxTileY; ++tileY) {
        int tileX = rect.minTileX;
        for (; tileX <= rect.maxTileX; ++tileX) {
            SetMaskAt(blockedMask, context.mapWidth, context.mapHeight, tileX, tileY, 1u);
        }
    }
}

bool AddContextLot(const RciTool& tool, const RciPlanningContext& context, const std::vector<std::uint8_t>& roadMask, std::vector<std::uint8_t>& blockedMask, const RciRect& rect, std::uint8_t frontDirection, RciPlan& plan) {
    if (!RectParcelAvailable(context, roadMask, blockedMask, rect)) {
        return false;
    }

    RciLot lot;
    lot.toolId = tool.id();
    lot.name = tool.name();
    lot.zoningType = tool.zoningType();
    lot.frontDirection = frontDirection;
    lot.color = tool.color();
    lot.rect = rect;
    plan.lots.push_back(lot);
    plan.zoneRects.push_back(rect);
    MarkParcelBlocked(context, rect, blockedMask);
    return true;
}

struct FrontageDepthInfo {
    int availableDepth;
    int targetDepth;

    FrontageDepthInfo()
        : availableDepth(0),
          targetDepth(0) {
    }

    FrontageDepthInfo(int available, int target)
        : availableDepth(available),
          targetDepth(target) {
    }
};

int RoadFacingDepthAtTile(const RciPlanningContext& context, const std::vector<std::uint8_t>& roadMask, const std::vector<std::uint8_t>& blockedMask, int tileX, int tileY, int deltaX, int deltaY) {
    int depth = 0;
    while (ParcelAvailableAt(context, roadMask, blockedMask, tileX + (deltaX * depth), tileY + (deltaY * depth))) {
        ++depth;
    }

    return depth;
}

FrontageDepthInfo RoadFacingDepthInfoAtTile(const RciTool& tool, const RciPlanningContext& context, const std::vector<std::uint8_t>& roadMask, const std::vector<std::uint8_t>& blockedMask, int tileX, int tileY, int deltaX, int deltaY) {
    const int availableDepth = RoadFacingDepthAtTile(context, roadMask, blockedMask, tileX, tileY, deltaX, deltaY);
    if (availableDepth <= 0) {
        return FrontageDepthInfo();
    }

    int targetDepth = std::min(availableDepth, tool.maxDepth());
    const bool boundedByOppositeRoad = RoadAt(context, roadMask, tileX + (deltaX * availableDepth), tileY + (deltaY * availableDepth));
    if (boundedByOppositeRoad &&
        availableDepth >= tool.minDepth() * 2 &&
        availableDepth <= tool.maxDepth() * 2) {
        targetDepth = (availableDepth + 1) / 2;
    }

    return FrontageDepthInfo(availableDepth, std::max(tool.minDepth(), targetDepth));
}

std::size_t AddRoadFacingParcelsForRun(const RciTool& tool, int roadFacingDirection, int frontageStartX, int frontageStartY, const std::vector<FrontageDepthInfo>& frontageDepths, const RciPlanningContext& context, const std::vector<std::uint8_t>& roadMask, std::vector<std::uint8_t>& blockedMask, RciPlan& plan) {
    std::size_t addedLots = 0u;
    int cursor = 0;
    while (cursor < static_cast<int>(frontageDepths.size())) {
        while (cursor < static_cast<int>(frontageDepths.size()) &&
            frontageDepths[static_cast<std::size_t>(cursor)].targetDepth < tool.minDepth()) {
            ++cursor;
        }
        if (cursor >= static_cast<int>(frontageDepths.size())) {
            break;
        }

        int targetDepth = frontageDepths[static_cast<std::size_t>(cursor)].targetDepth;

        int segmentEnd = cursor;
        while (segmentEnd < static_cast<int>(frontageDepths.size()) &&
            frontageDepths[static_cast<std::size_t>(segmentEnd)].availableDepth >= targetDepth &&
            frontageDepths[static_cast<std::size_t>(segmentEnd)].targetDepth == targetDepth) {
            ++segmentEnd;
        }

        if (segmentEnd - cursor < tool.minWidth() && targetDepth > tool.minDepth()) {
            targetDepth = tool.minDepth();
            segmentEnd = cursor;
            while (segmentEnd < static_cast<int>(frontageDepths.size()) &&
                frontageDepths[static_cast<std::size_t>(segmentEnd)].availableDepth >= targetDepth) {
                ++segmentEnd;
            }
        }

        const int frontageWidth = segmentEnd - cursor;
        if (frontageWidth >= tool.minWidth()) {
            std::vector<Segment> widthSegments;
            if (PartitionSegments(frontageWidth, tool.minWidth(), 2, tool.maxWidth(), widthSegments)) {
                std::size_t widthIndex = 0;
                for (; widthIndex < widthSegments.size(); ++widthIndex) {
                    const RciRect localRect(
                        cursor + widthSegments[widthIndex].startOffset,
                        0,
                        cursor + widthSegments[widthIndex].startOffset + widthSegments[widthIndex].length - 1,
                        targetDepth - 1);
                    const RciRect worldRect = MapRoadFacingLotRect(roadFacingDirection, frontageStartX, frontageStartY, localRect);
                    if (AddContextLot(tool, context, roadMask, blockedMask, worldRect, FrontDirectionForRoadFacing(roadFacingDirection), plan)) {
                        ++addedLots;
                    }
                }
            }
        }

        cursor = std::max(cursor + 1, segmentEnd);
    }

    return addedLots;
}

std::size_t AddRoadFacingParcels(const RciTool& tool, const RciPlanningContext& context, const std::vector<std::uint8_t>& roadMask, std::vector<std::uint8_t>& blockedMask, RciPlan& plan) {
    std::size_t addedLots = 0u;
    int tileY = context.bounds.minTileY;
    for (; tileY <= context.bounds.maxTileY; ++tileY) {
        int tileX = context.bounds.minTileX;
        while (tileX <= context.bounds.maxTileX) {
            if (!ParcelAvailableAt(context, roadMask, blockedMask, tileX, tileY) || !RoadAt(context, roadMask, tileX, tileY - 1)) {
                ++tileX;
                continue;
            }

            const int runStartX = tileX;
            std::vector<FrontageDepthInfo> frontageDepths;
            while (tileX <= context.bounds.maxTileX &&
                ParcelAvailableAt(context, roadMask, blockedMask, tileX, tileY) &&
                RoadAt(context, roadMask, tileX, tileY - 1)) {
                frontageDepths.push_back(RoadFacingDepthInfoAtTile(tool, context, roadMask, blockedMask, tileX, tileY, 0, 1));
                ++tileX;
            }
            addedLots += AddRoadFacingParcelsForRun(tool, kRoadFacingNorth, runStartX, tileY, frontageDepths, context, roadMask, blockedMask, plan);
        }
    }

    for (tileY = context.bounds.maxTileY; tileY >= context.bounds.minTileY; --tileY) {
        int tileX = context.bounds.minTileX;
        while (tileX <= context.bounds.maxTileX) {
            if (!ParcelAvailableAt(context, roadMask, blockedMask, tileX, tileY) || !RoadAt(context, roadMask, tileX, tileY + 1)) {
                ++tileX;
                continue;
            }

            const int runStartX = tileX;
            std::vector<FrontageDepthInfo> frontageDepths;
            while (tileX <= context.bounds.maxTileX &&
                ParcelAvailableAt(context, roadMask, blockedMask, tileX, tileY) &&
                RoadAt(context, roadMask, tileX, tileY + 1)) {
                frontageDepths.push_back(RoadFacingDepthInfoAtTile(tool, context, roadMask, blockedMask, tileX, tileY, 0, -1));
                ++tileX;
            }
            addedLots += AddRoadFacingParcelsForRun(tool, kRoadFacingSouth, runStartX, tileY, frontageDepths, context, roadMask, blockedMask, plan);
        }
    }

    int tileX = context.bounds.minTileX;
    for (; tileX <= context.bounds.maxTileX; ++tileX) {
        tileY = context.bounds.minTileY;
        while (tileY <= context.bounds.maxTileY) {
            if (!ParcelAvailableAt(context, roadMask, blockedMask, tileX, tileY) || !RoadAt(context, roadMask, tileX - 1, tileY)) {
                ++tileY;
                continue;
            }

            const int runStartY = tileY;
            std::vector<FrontageDepthInfo> frontageDepths;
            while (tileY <= context.bounds.maxTileY &&
                ParcelAvailableAt(context, roadMask, blockedMask, tileX, tileY) &&
                RoadAt(context, roadMask, tileX - 1, tileY)) {
                frontageDepths.push_back(RoadFacingDepthInfoAtTile(tool, context, roadMask, blockedMask, tileX, tileY, 1, 0));
                ++tileY;
            }
            addedLots += AddRoadFacingParcelsForRun(tool, kRoadFacingWest, tileX, runStartY, frontageDepths, context, roadMask, blockedMask, plan);
        }
    }

    for (tileX = context.bounds.maxTileX; tileX >= context.bounds.minTileX; --tileX) {
        tileY = context.bounds.minTileY;
        while (tileY <= context.bounds.maxTileY) {
            if (!ParcelAvailableAt(context, roadMask, blockedMask, tileX, tileY) || !RoadAt(context, roadMask, tileX + 1, tileY)) {
                ++tileY;
                continue;
            }

            const int runStartY = tileY;
            std::vector<FrontageDepthInfo> frontageDepths;
            while (tileY <= context.bounds.maxTileY &&
                ParcelAvailableAt(context, roadMask, blockedMask, tileX, tileY) &&
                RoadAt(context, roadMask, tileX + 1, tileY)) {
                frontageDepths.push_back(RoadFacingDepthInfoAtTile(tool, context, roadMask, blockedMask, tileX, tileY, -1, 0));
                ++tileY;
            }
            addedLots += AddRoadFacingParcelsForRun(tool, kRoadFacingEast, tileX, runStartY, frontageDepths, context, roadMask, blockedMask, plan);
        }
    }

    return addedLots;
}

bool FindAvailableRectangle(const RciPlanningContext& context, const std::vector<std::uint8_t>& roadMask, const std::vector<std::uint8_t>& blockedMask, int startX, int startY, RciRect& rect) {
    if (!ParcelAvailableAt(context, roadMask, blockedMask, startX, startY)) {
        return false;
    }

    int width = 0;
    while (startX + width <= context.bounds.maxTileX &&
        ParcelAvailableAt(context, roadMask, blockedMask, startX + width, startY)) {
        ++width;
    }

    int height = 0;
    bool rowAvailable = true;
    while (startY + height <= context.bounds.maxTileY && rowAvailable) {
        int offsetX = 0;
        for (; offsetX < width; ++offsetX) {
            if (!ParcelAvailableAt(context, roadMask, blockedMask, startX + offsetX, startY + height)) {
                rowAvailable = false;
                break;
            }
        }
        if (rowAvailable) {
            ++height;
        }
    }

    rect = RciRect(startX, startY, startX + width - 1, startY + height - 1);
    return rect.isValid();
}

std::size_t AddRemainingParcels(const RciTool& tool, const RciPlanningContext& context, const std::vector<std::uint8_t>& roadMask, std::vector<std::uint8_t>& blockedMask, RciPlan& plan) {
    std::size_t addedLots = 0u;
    int tileY = context.bounds.minTileY;
    for (; tileY <= context.bounds.maxTileY; ++tileY) {
        int tileX = context.bounds.minTileX;
        for (; tileX <= context.bounds.maxTileX; ++tileX) {
            RciRect availableRect;
            if (!FindAvailableRectangle(context, roadMask, blockedMask, tileX, tileY, availableRect) ||
                availableRect.width() < tool.minWidth() ||
                availableRect.height() < tool.minDepth()) {
                continue;
            }

            RciPlan localPlan;
            if (!tool.buildPlan(availableRect.minTileX, availableRect.minTileY, availableRect.maxTileX, availableRect.maxTileY, RciPlanMode::Lots, context.mapWidth, context.mapHeight, localPlan)) {
                continue;
            }

            std::size_t lotIndex = 0;
            for (; lotIndex < localPlan.lots.size(); ++lotIndex) {
                if (AddContextLot(tool, context, roadMask, blockedMask, localPlan.lots[lotIndex].rect, localPlan.lots[lotIndex].frontDirection, plan)) {
                    ++addedLots;
                }
            }
        }
    }

    return addedLots;
}

void AddPaintRects(const RciPlanningContext& context, RciPlan& plan) {
    int tileY = context.bounds.minTileY;
    for (; tileY <= context.bounds.maxTileY; ++tileY) {
        int tileX = context.bounds.minTileX;
        while (tileX <= context.bounds.maxTileX) {
            while (tileX <= context.bounds.maxTileX && !PaintableAt(context, tileX, tileY)) {
                ++tileX;
            }
            if (tileX > context.bounds.maxTileX) {
                break;
            }

            const int runStartX = tileX;
            while (tileX <= context.bounds.maxTileX && PaintableAt(context, tileX, tileY)) {
                ++tileX;
            }
            plan.paintRects.push_back(RciRect(runStartX, tileY, tileX - 1, tileY));
        }
    }
}

struct PaintComponent {
    RciRect rect;
    int tileCount;

    PaintComponent()
        : tileCount(0) {
    }
};

std::vector<PaintComponent> FindPaintComponents(const RciPlanningContext& context, const std::vector<std::uint8_t>& roadMask) {
    const std::size_t totalTiles = static_cast<std::size_t>(context.mapWidth) * static_cast<std::size_t>(context.mapHeight);
    std::vector<std::uint8_t> visited(totalTiles, 0u);
    std::vector<PaintComponent> components;
    std::vector<int> queue;

    int startY = context.bounds.minTileY;
    for (; startY <= context.bounds.maxTileY; ++startY) {
        int startX = context.bounds.minTileX;
        for (; startX <= context.bounds.maxTileX; ++startX) {
            const int startIndex = TileLinearIndex(startX, startY, context.mapWidth);
            if (startIndex < 0 ||
                startIndex >= static_cast<int>(visited.size()) ||
                visited[static_cast<std::size_t>(startIndex)] != 0 ||
                !PaintableAt(context, startX, startY) ||
                RoadAt(context, roadMask, startX, startY)) {
                continue;
            }

            PaintComponent component;
            component.rect = RciRect(startX, startY, startX, startY);
            queue.clear();
            queue.push_back(startIndex);
            visited[static_cast<std::size_t>(startIndex)] = 1u;

            std::size_t readIndex = 0;
            for (; readIndex < queue.size(); ++readIndex) {
                const int currentIndex = queue[readIndex];
                const int currentY = currentIndex / context.mapWidth;
                const int currentX = currentIndex - (currentY * context.mapWidth);
                ++component.tileCount;
                component.rect.minTileX = std::min(component.rect.minTileX, currentX);
                component.rect.maxTileX = std::max(component.rect.maxTileX, currentX);
                component.rect.minTileY = std::min(component.rect.minTileY, currentY);
                component.rect.maxTileY = std::max(component.rect.maxTileY, currentY);

                const int neighborOffsets[4][2] = {
                    {1, 0},
                    {-1, 0},
                    {0, 1},
                    {0, -1}
                };
                int neighborIndex = 0;
                for (; neighborIndex < 4; ++neighborIndex) {
                    const int neighborX = currentX + neighborOffsets[neighborIndex][0];
                    const int neighborY = currentY + neighborOffsets[neighborIndex][1];
                    if (neighborX < context.bounds.minTileX ||
                        neighborX > context.bounds.maxTileX ||
                        neighborY < context.bounds.minTileY ||
                        neighborY > context.bounds.maxTileY ||
                        !PaintableAt(context, neighborX, neighborY) ||
                        RoadAt(context, roadMask, neighborX, neighborY)) {
                        continue;
                    }

                    const int neighborTileIndex = TileLinearIndex(neighborX, neighborY, context.mapWidth);
                    if (neighborTileIndex < 0 ||
                        neighborTileIndex >= static_cast<int>(visited.size()) ||
                        visited[static_cast<std::size_t>(neighborTileIndex)] != 0) {
                        continue;
                    }

                    visited[static_cast<std::size_t>(neighborTileIndex)] = 1u;
                    queue.push_back(neighborTileIndex);
                }
            }

            components.push_back(component);
        }
    }

    return components;
}

bool HasLineRoadableSpan(const RciPlanningContext& context, const RciRect& rect, bool vertical, int origin) {
    if (vertical) {
        int tileY = rect.minTileY;
        int runLength = 0;
        for (; tileY <= rect.maxTileY; ++tileY) {
            const bool roadable = RoadableAt(context, origin, tileY) && RoadableAt(context, origin + 1, tileY);
            runLength = roadable ? runLength + 1 : 0;
            if (runLength >= kPlannedLocalRoadFootprint) {
                return true;
            }
        }
        return false;
    }

    int tileX = rect.minTileX;
    int runLength = 0;
    for (; tileX <= rect.maxTileX; ++tileX) {
        const bool roadable = RoadableAt(context, tileX, origin) && RoadableAt(context, tileX, origin + 1);
        runLength = roadable ? runLength + 1 : 0;
        if (runLength >= kPlannedLocalRoadFootprint) {
            return true;
        }
    }
    return false;
}

bool HasNearbyParallelRoad(const RciPlanningContext& context, const RciRect& rect, bool vertical, int origin) {
    const RoadAxis axis = vertical ? RoadAxis::Vertical : RoadAxis::Horizontal;
    int offset = -kGridSnapTolerance;
    for (; offset <= kGridSnapTolerance; ++offset) {
        const int checkOrigin = origin + offset;
        if (vertical) {
            int tileY = rect.minTileY;
            for (; tileY <= rect.maxTileY; ++tileY) {
                if (AxisMaskAt(context, checkOrigin, tileY, axis)) {
                    return true;
                }
            }
        } else {
            int tileX = rect.minTileX;
            for (; tileX <= rect.maxTileX; ++tileX) {
                if (AxisMaskAt(context, tileX, checkOrigin, axis)) {
                    return true;
                }
            }
        }
    }

    return false;
}

std::vector<int> CollectParallelRoadOrigins(const RciPlanningContext& context, const RciRect& rect, bool vertical) {
    std::vector<int> origins;
    const RoadAxis axis = vertical ? RoadAxis::Vertical : RoadAxis::Horizontal;
    if (vertical) {
        int tileX = std::max(0, rect.minTileX - kGridSnapTolerance);
        for (; tileX <= std::min(context.mapWidth - 1, rect.maxTileX + kGridSnapTolerance); ++tileX) {
            int tileY = std::max(0, rect.minTileY - 1);
            bool found = false;
            for (; tileY <= std::min(context.mapHeight - 1, rect.maxTileY + 1); ++tileY) {
                if (AxisMaskAt(context, tileX, tileY, axis)) {
                    found = true;
                    break;
                }
            }
            if (found) {
                origins.push_back(tileX);
            }
        }
    } else {
        int tileY = std::max(0, rect.minTileY - kGridSnapTolerance);
        for (; tileY <= std::min(context.mapHeight - 1, rect.maxTileY + kGridSnapTolerance); ++tileY) {
            int tileX = std::max(0, rect.minTileX - 1);
            bool found = false;
            for (; tileX <= std::min(context.mapWidth - 1, rect.maxTileX + 1); ++tileX) {
                if (AxisMaskAt(context, tileX, tileY, axis)) {
                    found = true;
                    break;
                }
            }
            if (found) {
                origins.push_back(tileY);
            }
        }
    }

    std::sort(origins.begin(), origins.end());
    origins.erase(std::unique(origins.begin(), origins.end()), origins.end());
    return origins;
}

int ChooseRoadOrigin(const RciPlanningContext& context, const RciRect& rect, bool vertical, int minimumFirstBlock, int minimumSecondBlock, int maximumBlock) {
    const int minCoordinate = vertical ? rect.minTileX : rect.minTileY;
    const int maxCoordinate = vertical ? rect.maxTileX : rect.maxTileY;
    const int length = maxCoordinate - minCoordinate + 1;
    if (length <= maximumBlock) {
        return -1;
    }

    const int minimumOrigin = minCoordinate + minimumFirstBlock;
    const int maximumOrigin = maxCoordinate - minimumSecondBlock - kPlannedLocalRoadFootprint + 1;
    if (maximumOrigin < minimumOrigin) {
        return -1;
    }

    const int idealOrigin = ClampInt(minCoordinate + (length / 2) - (kPlannedLocalRoadFootprint / 2), minimumOrigin, maximumOrigin);
    std::vector<int> snappedCandidates;
    const std::vector<int> parallelOrigins = CollectParallelRoadOrigins(context, rect, vertical);
    std::size_t originIndex = 0;
    for (; originIndex < parallelOrigins.size(); ++originIndex) {
        if (std::abs(parallelOrigins[originIndex] - idealOrigin) <= kGridSnapTolerance) {
            snappedCandidates.push_back(ClampInt(parallelOrigins[originIndex], minimumOrigin, maximumOrigin));
        }
    }

    std::sort(snappedCandidates.begin(), snappedCandidates.end(), [idealOrigin](int left, int right) {
        const int leftScore = std::abs(left - idealOrigin);
        const int rightScore = std::abs(right - idealOrigin);
        if (leftScore != rightScore) {
            return leftScore < rightScore;
        }
        return left < right;
    });
    snappedCandidates.erase(std::unique(snappedCandidates.begin(), snappedCandidates.end()), snappedCandidates.end());

    std::size_t candidateIndex = 0;
    for (; candidateIndex < snappedCandidates.size(); ++candidateIndex) {
        const int candidate = snappedCandidates[candidateIndex];
        if (HasNearbyParallelRoad(context, rect, vertical, candidate) ||
            !HasLineRoadableSpan(context, rect, vertical, candidate)) {
            continue;
        }

        return candidate;
    }

    std::vector<int> candidates;
    int delta = 0;
    for (; delta <= std::max(idealOrigin - minimumOrigin, maximumOrigin - idealOrigin); ++delta) {
        const int left = idealOrigin - delta;
        const int right = idealOrigin + delta;
        if (left >= minimumOrigin) {
            candidates.push_back(left);
        }
        if (right <= maximumOrigin && right != left) {
            candidates.push_back(right);
        }
    }

    std::sort(candidates.begin(), candidates.end(), [idealOrigin](int left, int right) {
        const int leftScore = std::abs(left - idealOrigin);
        const int rightScore = std::abs(right - idealOrigin);
        if (leftScore != rightScore) {
            return leftScore < rightScore;
        }
        return left < right;
    });
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    for (candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex) {
        const int candidate = candidates[candidateIndex];
        if (HasNearbyParallelRoad(context, rect, vertical, candidate) ||
            !HasLineRoadableSpan(context, rect, vertical, candidate)) {
            continue;
        }

        return candidate;
    }

    return -1;
}

void AddRoadSegmentsForLine(const RciPlanningContext& context, bool vertical, int origin, const RciRect& rect, RciPlan& plan, std::vector<std::uint8_t>& roadMask) {
    if (vertical) {
        int tileY = rect.minTileY;
        while (tileY <= rect.maxTileY) {
            while (tileY <= rect.maxTileY && !(RoadableAt(context, roadMask, origin, tileY) && RoadableAt(context, roadMask, origin + 1, tileY))) {
                ++tileY;
            }
            if (tileY > rect.maxTileY) {
                break;
            }

            const int runStartY = tileY;
            while (tileY <= rect.maxTileY && RoadableAt(context, roadMask, origin, tileY) && RoadableAt(context, roadMask, origin + 1, tileY)) {
                ++tileY;
            }
            if (tileY - runStartY >= kPlannedLocalRoadFootprint) {
                int extendedStartY = runStartY;
                while (extendedStartY > 0 && PerpendicularRoadAtLinePosition(context, plan, true, origin, extendedStartY - 1)) {
                    --extendedStartY;
                }
                int extendedEndY = tileY - 1;
                while (extendedEndY + 1 < context.mapHeight && PerpendicularRoadAtLinePosition(context, plan, true, origin, extendedEndY + 1)) {
                    ++extendedEndY;
                }
                const RciRoadPlan roadPlan(origin, extendedStartY, origin, extendedEndY);
                AddRoadPlan(plan, roadPlan);
                MarkRoadFootprint(roadPlan, context, roadMask);
            }
        }
        return;
    }

    int tileX = rect.minTileX;
    while (tileX <= rect.maxTileX) {
        while (tileX <= rect.maxTileX && !(RoadableAt(context, roadMask, tileX, origin) && RoadableAt(context, roadMask, tileX, origin + 1))) {
            ++tileX;
        }
        if (tileX > rect.maxTileX) {
            break;
        }

        const int runStartX = tileX;
        while (tileX <= rect.maxTileX && RoadableAt(context, roadMask, tileX, origin) && RoadableAt(context, roadMask, tileX, origin + 1)) {
            ++tileX;
        }
        if (tileX - runStartX >= kPlannedLocalRoadFootprint) {
            int extendedStartX = runStartX;
            while (extendedStartX > 0 && PerpendicularRoadAtLinePosition(context, plan, false, origin, extendedStartX - 1)) {
                --extendedStartX;
            }
            int extendedEndX = tileX - 1;
            while (extendedEndX + 1 < context.mapWidth && PerpendicularRoadAtLinePosition(context, plan, false, origin, extendedEndX + 1)) {
                ++extendedEndX;
            }
            const RciRoadPlan roadPlan(extendedStartX, origin, extendedEndX, origin);
            AddRoadPlan(plan, roadPlan);
            MarkRoadFootprint(roadPlan, context, roadMask);
        }
    }
}

int MinimumAdjacentRoadCoverage(const RciTool& tool, int spanLength) {
    return std::min(spanLength, std::max(kPlannedLocalRoadFootprint, tool.minWidth()));
}

int HorizontalAnyRoadCoverage(const RciPlanningContext& context, const std::vector<std::uint8_t>& roadMask, const RciRect& rect, int roadY) {
    int coverage = 0;
    int tileX = rect.minTileX;
    for (; tileX <= rect.maxTileX; ++tileX) {
        if (RoadAt(context, roadMask, tileX, roadY)) {
            ++coverage;
        }
    }
    return coverage;
}

std::vector<int> PartitionLengthEvenly(int totalLength, int segmentCount) {
    std::vector<int> segments;
    if (totalLength <= 0 || segmentCount <= 0) {
        return segments;
    }

    const int baseLength = totalLength / segmentCount;
    const int extra = totalLength % segmentCount;
    int segmentIndex = 0;
    for (; segmentIndex < segmentCount; ++segmentIndex) {
        segments.push_back(baseLength + (segmentIndex < extra ? 1 : 0));
    }
    return segments;
}

int OneSidedRoadFacingBlockCount(int totalLength, int maximumBlockLength) {
    int blockCount = 1;
    while (totalLength - (blockCount * kPlannedLocalRoadFootprint) > blockCount * maximumBlockLength) {
        blockCount *= 2;
    }
    return blockCount;
}

bool AddOneSidedRoadFacingSplitsForComponent(const RciTool& tool, const RciPlanningContext& context, const PaintComponent& component, RciPlan& plan, std::vector<std::uint8_t>& roadMask) {
    const RciRect& rect = component.rect;
    bool addedRoad = false;
    const int maximumRoadBoundBlockDepth = tool.maxDepth() * 2;
    if (rect.height() > tool.maxDepth()) {
        const int minimumCoverage = MinimumAdjacentRoadCoverage(tool, rect.width());
        const bool hasNorthRoad = HorizontalAnyRoadCoverage(context, roadMask, rect, rect.minTileY - 1) >= minimumCoverage;
        const bool hasSouthRoad = HorizontalAnyRoadCoverage(context, roadMask, rect, rect.maxTileY + 1) >= minimumCoverage;
        const bool splitFromNorth = hasNorthRoad && !hasSouthRoad;
        const bool splitFromSouth = hasSouthRoad && !hasNorthRoad;
        if (splitFromNorth || splitFromSouth) {
            const int blockCount = OneSidedRoadFacingBlockCount(rect.height(), maximumRoadBoundBlockDepth);
            const int blockOnlyLength = rect.height() - (blockCount * kPlannedLocalRoadFootprint);
            const std::vector<int> blockLengths = PartitionLengthEvenly(blockOnlyLength, blockCount);
            if (splitFromNorth) {
                int cursorY = rect.minTileY;
                std::size_t blockIndex = 0;
                for (; blockIndex < blockLengths.size(); ++blockIndex) {
                    cursorY += blockLengths[blockIndex];
                    if (cursorY >= rect.minTileY &&
                        cursorY + kPlannedLocalRoadFootprint - 1 <= rect.maxTileY &&
                        !HasNearbyParallelRoad(context, rect, false, cursorY) &&
                        HasLineRoadableSpan(context, rect, false, cursorY)) {
                        const std::size_t oldRoadCount = plan.roadPlans.size();
                        AddRoadSegmentsForLine(context, false, cursorY, rect, plan, roadMask);
                        addedRoad = addedRoad || plan.roadPlans.size() != oldRoadCount;
                    }
                    cursorY += kPlannedLocalRoadFootprint;
                }
            } else {
                int cursorY = rect.maxTileY;
                int blockIndex = static_cast<int>(blockLengths.size()) - 1;
                for (; blockIndex >= 0; --blockIndex) {
                    cursorY -= blockLengths[static_cast<std::size_t>(blockIndex)];
                    const int roadY = cursorY - kPlannedLocalRoadFootprint + 1;
                    if (roadY >= rect.minTileY &&
                        roadY + kPlannedLocalRoadFootprint - 1 <= rect.maxTileY &&
                        !HasNearbyParallelRoad(context, rect, false, roadY) &&
                        HasLineRoadableSpan(context, rect, false, roadY)) {
                        const std::size_t oldRoadCount = plan.roadPlans.size();
                        AddRoadSegmentsForLine(context, false, roadY, rect, plan, roadMask);
                        addedRoad = addedRoad || plan.roadPlans.size() != oldRoadCount;
                    }
                    cursorY -= kPlannedLocalRoadFootprint;
                }
            }
        }
    }

    return addedRoad;
}

void AddSmartRoads(const RciTool& tool, const RciPlanningContext& context, RciPlan& plan, std::vector<std::uint8_t>& roadMask) {
    const int maximumBlockWidth = tool.maxWidth();
    const int maximumBlockDepth = tool.maxDepth() * 2;
    const int minimumBlockDepth = tool.minDepth() * 2;

    int iteration = 0;
    for (; iteration < 128; ++iteration) {
        const std::vector<PaintComponent> components = FindPaintComponents(context, roadMask);
        bool addedRoad = false;
        std::size_t componentIndex = 0;
        for (; componentIndex < components.size(); ++componentIndex) {
            const RciRect& rect = components[componentIndex].rect;
            if (rect.width() > maximumBlockWidth) {
                const int roadX = ChooseRoadOrigin(context, rect, true, tool.minWidth(), tool.minWidth(), maximumBlockWidth);
                if (roadX >= 0) {
                    AddRoadSegmentsForLine(context, true, roadX, rect, plan, roadMask);
                    addedRoad = true;
                    break;
                }
            }

            if (AddOneSidedRoadFacingSplitsForComponent(tool, context, components[componentIndex], plan, roadMask)) {
                addedRoad = true;
                break;
            }

            if (rect.height() > maximumBlockDepth) {
                const int roadY = ChooseRoadOrigin(context, rect, false, minimumBlockDepth, minimumBlockDepth, maximumBlockDepth);
                if (roadY >= 0) {
                    AddRoadSegmentsForLine(context, false, roadY, rect, plan, roadMask);
                    addedRoad = true;
                    break;
                }
            }
        }

        if (!addedRoad) {
            break;
        }
    }
}
}

RciColor::RciColor()
    : r(0.0f),
      g(0.0f),
      b(0.0f),
      a(0.0f) {
}

RciColor::RciColor(float red, float green, float blue, float alpha)
    : r(red),
      g(green),
      b(blue),
      a(alpha) {
}

RciRect::RciRect()
    : minTileX(0),
      minTileY(0),
      maxTileX(-1),
      maxTileY(-1) {
}

RciRect::RciRect(int minX, int minY, int maxX, int maxY)
    : minTileX(minX),
      minTileY(minY),
      maxTileX(maxX),
      maxTileY(maxY) {
}

int RciRect::width() const {
    return isValid() ? maxTileX - minTileX + 1 : 0;
}

int RciRect::height() const {
    return isValid() ? maxTileY - minTileY + 1 : 0;
}

bool RciRect::isValid() const {
    return maxTileX >= minTileX && maxTileY >= minTileY;
}

bool RciRect::intersects(const RciRect& other) const {
    if (!isValid() || !other.isValid()) {
        return false;
    }

    return minTileX <= other.maxTileX &&
        maxTileX >= other.minTileX &&
        minTileY <= other.maxTileY &&
        maxTileY >= other.minTileY;
}

RciRoadPlan::RciRoadPlan()
    : startTileX(0),
      startTileY(0),
      endTileX(0),
      endTileY(0) {
}

RciRoadPlan::RciRoadPlan(int startX, int startY, int endX, int endY)
    : startTileX(startX),
      startTileY(startY),
      endTileX(endX),
      endTileY(endY) {
}

RciPlanningContext::RciPlanningContext()
    : mapWidth(0),
      mapHeight(0),
      mode(RciPlanMode::Area) {
}

RciLot::RciLot()
    : zoningType(TileZoningNone),
      frontDirection(kRoadDirectionNorth),
      availableAfterTick(0) {
}

RciPlan::RciPlan()
    : zoningType(TileZoningNone),
      mode(RciPlanMode::Area) {
}

RciTool::RciTool()
    : color_(0.18f, 0.86f, 0.32f, 0.50f),
      zoningType_(TileZoningResidential),
      minDepth_(2),
      preferredDepth_(4),
      maxDepth_(8),
      minWidth_(2),
      preferredWidth_(16),
      maxWidth_(24) {
}

const std::string& RciTool::id() const {
    return id_;
}

const std::string& RciTool::name() const {
    return name_;
}

const std::string& RciTool::labelStringId() const {
    return labelStringId_;
}

const std::string& RciTool::desirabilityOverlayStringId() const {
    return desirabilityOverlayStringId_;
}

const RciColor& RciTool::color() const {
    return color_;
}

std::uint16_t RciTool::zoningType() const {
    return zoningType_;
}

int RciTool::minDepth() const {
    return minDepth_;
}

int RciTool::preferredDepth() const {
    return preferredDepth_;
}

int RciTool::maxDepth() const {
    return maxDepth_;
}

int RciTool::minWidth() const {
    return minWidth_;
}

int RciTool::preferredWidth() const {
    return preferredWidth_;
}

int RciTool::maxWidth() const {
    return maxWidth_;
}

void RciTool::setDefinition(
    const std::string& id,
    const std::string& name,
    const RciColor& color,
    std::uint16_t zoningType,
    int minDepth,
    int preferredDepth,
    int maxDepth,
    int minWidth,
    int preferredWidth,
    int maxWidth,
    const std::string& labelStringId,
    const std::string& desirabilityOverlayStringId) {
    id_ = id;
    name_ = name.empty() ? id : name;
    labelStringId_ = labelStringId;
    desirabilityOverlayStringId_ = desirabilityOverlayStringId;
    color_ = color;
    zoningType_ = zoningType;

    minDepth_ = std::max(1, minDepth);
    maxDepth_ = std::max(minDepth_, maxDepth);
    preferredDepth_ = ClampInt(preferredDepth, minDepth_, maxDepth_);

    minWidth_ = std::max(1, minWidth);
    maxWidth_ = std::max(minWidth_, maxWidth);
    preferredWidth_ = ClampInt(preferredWidth, minWidth_, maxWidth_);
}

bool RciTool::buildPlan(
    int startTileX,
    int startTileY,
    int endTileX,
    int endTileY,
    RciPlanMode mode,
    int mapWidth,
    int mapHeight,
    RciPlan& plan) const {
    plan = RciPlan();
    const RciRect bounds = NormalizeBounds(startTileX, startTileY, endTileX, endTileY, mapWidth, mapHeight);
    if (!bounds.isValid() || zoningType_ == TileZoningNone) {
        return false;
    }

    plan.toolId = id_;
    plan.name = name_;
    plan.zoningType = zoningType_;
    plan.color = color_;
    plan.mode = mode;
    plan.bounds = bounds;

    if (mode == RciPlanMode::Area) {
        return buildAreaPlan(bounds, plan);
    }

    if (mode == RciPlanMode::Lots) {
        return buildLotsPlan(bounds, plan);
    }

    return buildLotsAndRoadsPlan(bounds, plan);
}

bool RciTool::buildPlan(const RciPlanningContext& context, RciPlan& plan) const {
    plan = RciPlan();
    const std::size_t totalTiles = static_cast<std::size_t>(context.mapWidth) * static_cast<std::size_t>(context.mapHeight);
    if (context.mapWidth <= 0 ||
        context.mapHeight <= 0 ||
        !context.bounds.isValid() ||
        context.bounds.minTileX < 0 ||
        context.bounds.minTileY < 0 ||
        context.bounds.maxTileX >= context.mapWidth ||
        context.bounds.maxTileY >= context.mapHeight ||
        context.paintableTiles.size() < totalTiles ||
        zoningType_ == TileZoningNone) {
        return false;
    }

    plan.toolId = id_;
    plan.name = name_;
    plan.zoningType = zoningType_;
    plan.color = color_;
    plan.mode = context.mode;
    plan.bounds = context.bounds;
    AddPaintRects(context, plan);
    if (plan.paintRects.empty()) {
        return false;
    }

    if (context.mode == RciPlanMode::Area) {
        plan.zoneRects = plan.paintRects;
        return true;
    }

    std::vector<std::uint8_t> roadMask(totalTiles, 0u);
    if (context.groundRoadTiles.size() >= totalTiles) {
        std::copy(context.groundRoadTiles.begin(), context.groundRoadTiles.begin() + totalTiles, roadMask.begin());
    }

    if (context.mode == RciPlanMode::LotsAndRoads) {
        AddSmartRoads(*this, context, plan, roadMask);
    }

    std::vector<std::uint8_t> blockedMask(totalTiles, 0u);
    AddRoadFacingParcels(*this, context, roadMask, blockedMask, plan);
    AddRemainingParcels(*this, context, roadMask, blockedMask, plan);
    if (plan.lots.empty()) {
        plan.zoneRects = plan.paintRects;
    }

    return !plan.lots.empty() || !plan.zoneRects.empty();
}

bool RciTool::buildAreaPlan(const RciRect& bounds, RciPlan& plan) const {
    plan.zoneRects.push_back(bounds);
    return true;
}

bool RciTool::buildLotsPlan(const RciRect& bounds, RciPlan& plan) const {
    std::vector<Segment> widthSegments;
    std::vector<Segment> depthSegments;
    if (!PartitionSegments(bounds.width(), minWidth_, 2, maxWidth_, widthSegments) ||
        !PartitionSegments(bounds.height(), minDepth_, preferredDepth_, maxDepth_, depthSegments)) {
        return buildAreaPlan(bounds, plan);
    }

    std::size_t depthIndex = 0;
    for (; depthIndex < depthSegments.size(); ++depthIndex) {
        std::size_t widthIndex = 0;
        for (; widthIndex < widthSegments.size(); ++widthIndex) {
            const RciRect lotRect(
                bounds.minTileX + widthSegments[widthIndex].startOffset,
                bounds.minTileY + depthSegments[depthIndex].startOffset,
                bounds.minTileX + widthSegments[widthIndex].startOffset + widthSegments[widthIndex].length - 1,
                bounds.minTileY + depthSegments[depthIndex].startOffset + depthSegments[depthIndex].length - 1);
            plan.lots.push_back(constructLot(lotRect));
            plan.zoneRects.push_back(lotRect);
        }
    }

    return !plan.lots.empty();
}

bool RciTool::buildLotsAndRoadsPlan(const RciRect& bounds, RciPlan& plan) const {
    std::vector<Segment> blockColumns;
    std::vector<Segment> blockRows;
    std::vector<int> roadColumnOffsets;
    std::vector<int> roadRowOffsets;

    const int minimumBlockDepth = minDepth_ * 2;
    const int preferredBlockDepth = preferredDepth_ * 2;
    const int maximumBlockDepth = maxDepth_ * 2;
    if (!PartitionBlocksWithRoads(bounds.width(), minWidth_, preferredWidth_, maxWidth_, blockColumns, roadColumnOffsets) ||
        !PartitionBlocksWithRoads(bounds.height(), minimumBlockDepth, preferredBlockDepth, maximumBlockDepth, blockRows, roadRowOffsets)) {
        return buildLotsPlan(bounds, plan);
    }

    std::size_t roadIndex = 0;
    for (; roadIndex < roadRowOffsets.size(); ++roadIndex) {
        const int roadY = bounds.minTileY + roadRowOffsets[roadIndex];
        plan.roadPlans.push_back(RciRoadPlan(bounds.minTileX, roadY, bounds.maxTileX, roadY));
    }

    for (roadIndex = 0; roadIndex < roadColumnOffsets.size(); ++roadIndex) {
        const int roadX = bounds.minTileX + roadColumnOffsets[roadIndex];
        plan.roadPlans.push_back(RciRoadPlan(roadX, bounds.minTileY, roadX, bounds.maxTileY));
    }

    std::size_t rowIndex = 0;
    for (; rowIndex < blockRows.size(); ++rowIndex) {
        const int blockY = bounds.minTileY + blockRows[rowIndex].startOffset;
        int firstDepth = 0;
        int secondDepth = 0;
        if (!SplitBlockIntoTwoDepths(blockRows[rowIndex].length, minDepth_, preferredDepth_, maxDepth_, firstDepth, secondDepth)) {
            continue;
        }

        std::size_t columnIndex = 0;
        for (; columnIndex < blockColumns.size(); ++columnIndex) {
            const int blockX = bounds.minTileX + blockColumns[columnIndex].startOffset;
            std::vector<Segment> lotWidthSegments;
            if (!PartitionSegments(blockColumns[columnIndex].length, minWidth_, 2, maxWidth_, lotWidthSegments)) {
                continue;
            }

            const int rowDepths[2] = {firstDepth, secondDepth};
            int rowStartOffsets[2] = {0, firstDepth};
            int lotRow = 0;
            for (; lotRow < 2; ++lotRow) {
                std::size_t lotColumnIndex = 0;
                for (; lotColumnIndex < lotWidthSegments.size(); ++lotColumnIndex) {
                    const RciRect lotRect(
                        blockX + lotWidthSegments[lotColumnIndex].startOffset,
                        blockY + rowStartOffsets[lotRow],
                        blockX + lotWidthSegments[lotColumnIndex].startOffset + lotWidthSegments[lotColumnIndex].length - 1,
                        blockY + rowStartOffsets[lotRow] + rowDepths[lotRow] - 1);
                    plan.lots.push_back(constructLot(lotRect));
                    plan.zoneRects.push_back(lotRect);
                }
            }
        }
    }

    if (plan.lots.empty()) {
        return buildLotsPlan(bounds, plan);
    }

    return true;
}

RciLot RciTool::constructLot(const RciRect& rect) const {
    RciLot lot;
    lot.toolId = id_;
    lot.name = name_;
    lot.zoningType = zoningType_;
    lot.color = color_;
    lot.rect = rect;
    return lot;
}

RciType::RciType()
    : color_(0.18f, 0.86f, 0.32f, 0.50f) {
}

const std::string& RciType::id() const {
    return id_;
}

const std::string& RciType::name() const {
    return name_;
}

const std::string& RciType::desirabilityOverlayStringId() const {
    return desirabilityOverlayStringId_;
}

const std::string& RciType::demandParameterId() const {
    return demandParameterId_;
}

const RciColor& RciType::color() const {
    return color_;
}

const std::vector<std::uint16_t>& RciType::allowedZoningTypes() const {
    return allowedZoningTypes_;
}

bool RciType::allowsZoningType(std::uint16_t zoningType) const {
    return std::find(allowedZoningTypes_.begin(), allowedZoningTypes_.end(), zoningType) != allowedZoningTypes_.end();
}

void RciType::setDefinition(
    const std::string& id,
    const std::string& name,
    const std::string& desirabilityOverlayStringId,
    const std::string& demandParameterId,
    const RciColor& color,
    const std::vector<std::uint16_t>& allowedZoningTypes) {
    id_ = id;
    name_ = name.empty() ? id : name;
    desirabilityOverlayStringId_ = desirabilityOverlayStringId;
    demandParameterId_ = demandParameterId;
    color_ = color;
    allowedZoningTypes_ = allowedZoningTypes;
}

RciToolCatalog::RciToolCatalog() {
}

bool RciToolCatalog::loadFromXmlFile(const std::string& filePath) {
    const std::string xml = XmlReadFileToString(filePath);
    if (xml.empty()) {
        tools_.clear();
        rciTypes_.clear();
        return false;
    }

    std::vector<RciTool> loadedTools;
    std::vector<RciType> loadedRciTypes;

    std::string::size_type zoneSearchStart = 0u;
    while (true) {
        const std::string::size_type zoneStart = xml.find("<zone", zoneSearchStart);
        if (zoneStart == std::string::npos) {
            break;
        }

        const std::string::size_type zoneEnd = xml.find('>', zoneStart);
        if (zoneEnd == std::string::npos) {
            break;
        }

        const std::string zoneTag = xml.substr(zoneStart, zoneEnd - zoneStart + 1u);
        const std::string id = XmlAttributeValue(zoneTag, "id", std::string());
        const std::uint16_t zoningType = ZoningTypeFromToolTag(zoneTag, id);
        if (!id.empty() && zoningType != TileZoningNone) {
            RciTool tool;
            const RciColor fallbackColor = DefaultZoneColor(zoningType);
            tool.setDefinition(
                id,
                XmlAttributeValue(zoneTag, "name", id),
                AttributeColorValue(zoneTag, fallbackColor),
                zoningType,
                XmlAttributeIntValue(zoneTag, "minDepth", 2),
                XmlAttributeIntValueAny(zoneTag, "preferredDepth", "preferedDepth", IsResidentialZoningType(zoningType) ? 4 : 8),
                XmlAttributeIntValue(zoneTag, "maxDepth", 8),
                XmlAttributeIntValue(zoneTag, "minWidth", 2),
                XmlAttributeIntValueAny(zoneTag, "preferredWidth", "preferedWidth", 16),
                XmlAttributeIntValue(zoneTag, "maxWidth", 24),
                XmlAttributeValue(zoneTag, "labelStringId", std::string()),
                std::string());
            loadedTools.push_back(tool);
        }

        zoneSearchStart = zoneEnd + 1u;
    }

    std::string::size_type searchStart = 0u;
    while (true) {
        const std::string::size_type toolStart = xml.find("<tool", searchStart);
        if (toolStart == std::string::npos) {
            break;
        }

        const std::string::size_type toolEnd = xml.find('>', toolStart);
        if (toolEnd == std::string::npos) {
            break;
        }

        const std::string toolTag = xml.substr(toolStart, toolEnd - toolStart + 1u);
        const std::string id = XmlAttributeValue(toolTag, "id", std::string());
        const std::uint16_t zoningType = ZoningTypeFromToolTag(toolTag, id);
        if (!id.empty() && zoningType != TileZoningNone) {
            RciTool tool;
            const RciColor fallbackColor = DefaultZoneColor(zoningType);
            tool.setDefinition(
                id,
                XmlAttributeValue(toolTag, "name", id),
                AttributeColorValue(toolTag, fallbackColor),
                zoningType,
                XmlAttributeIntValue(toolTag, "minDepth", 2),
                XmlAttributeIntValueAny(toolTag, "preferredDepth", "preferedDepth", IsResidentialZoningType(zoningType) ? 4 : 8),
                XmlAttributeIntValue(toolTag, "maxDepth", 8),
                XmlAttributeIntValue(toolTag, "minWidth", 2),
                XmlAttributeIntValueAny(toolTag, "preferredWidth", "preferedWidth", 16),
                XmlAttributeIntValue(toolTag, "maxWidth", 24),
                XmlAttributeValue(toolTag, "labelStringId", std::string()),
                XmlAttributeValue(toolTag, "desirabilityOverlayStringId", std::string()));
            loadedTools.push_back(tool);
        }

        searchStart = toolEnd + 1u;
    }

    std::string::size_type rciTypeSearchStart = 0u;
    while (true) {
        const std::string::size_type rciTypeStart = xml.find("<rciType", rciTypeSearchStart);
        if (rciTypeStart == std::string::npos) {
            break;
        }

        const std::string::size_type rciTypeEnd = xml.find('>', rciTypeStart);
        if (rciTypeEnd == std::string::npos) {
            break;
        }

        const std::string rciTypeTag = xml.substr(rciTypeStart, rciTypeEnd - rciTypeStart + 1u);
        const std::string id = XmlAttributeValue(rciTypeTag, "id", std::string());
        const std::vector<std::uint16_t> allowedZoningTypes = ZoningTypesFromRciTypeTag(rciTypeTag);
        if (!id.empty() && !allowedZoningTypes.empty()) {
            RciType rciType;
            const RciColor fallbackColor = IsResidentialZoningType(allowedZoningTypes.front()) ?
                RciColor(0.18f, 0.86f, 0.32f, 0.50f) :
                RciColor(0.92f, 0.76f, 0.15f, 0.50f);
            rciType.setDefinition(
                id,
                XmlAttributeValue(rciTypeTag, "name", id),
                XmlAttributeValue(rciTypeTag, "desirabilityOverlayStringId", std::string()),
                XmlAttributeValue(rciTypeTag, "demandParameterId", std::string()),
                AttributeColorValue(rciTypeTag, fallbackColor),
                allowedZoningTypes);
            loadedRciTypes.push_back(rciType);
        }

        rciTypeSearchStart = rciTypeEnd + 1u;
    }

    if (loadedTools.empty()) {
        tools_.clear();
        rciTypes_.clear();
        return false;
    }
    if (loadedRciTypes.empty()) {
        tools_.clear();
        rciTypes_.clear();
        return false;
    }

    tools_ = loadedTools;
    rciTypes_ = loadedRciTypes;
    return true;
}

const std::vector<RciTool>& RciToolCatalog::tools() const {
    return tools_;
}

const RciTool* RciToolCatalog::findTool(const std::string& id) const {
    std::size_t toolIndex = 0;
    for (; toolIndex < tools_.size(); ++toolIndex) {
        if (tools_[toolIndex].id() == id) {
            return &tools_[toolIndex];
        }
    }

    return 0;
}

const std::vector<RciType>& RciToolCatalog::rciTypes() const {
    return rciTypes_;
}

const RciType* RciToolCatalog::findRciType(const std::string& id) const {
    std::size_t typeIndex = 0;
    for (; typeIndex < rciTypes_.size(); ++typeIndex) {
        if (rciTypes_[typeIndex].id() == id) {
            return &rciTypes_[typeIndex];
        }
    }

    return 0;
}
