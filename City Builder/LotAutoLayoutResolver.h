#pragma once

#include "Lot.h"
#include "LotModulePlacementGeometry.h"

inline bool LotAutoSizeConditionMatches(const LotAutoSizeCondition& condition, int lotWidth, int lotHeight) {
    return lotWidth >= condition.minWidth && lotWidth <= condition.maxWidth &&
        lotHeight >= condition.minDepth && lotHeight <= condition.maxDepth;
}

struct LotAutoPrimaryGeometry {
    bool hasPrimary;
    std::string moduleId;
    Int2 localOrigin;
    int footprintWidth;
    int footprintHeight;

    LotAutoPrimaryGeometry()
        : hasPrimary(false),
          localOrigin(0, 0),
          footprintWidth(1),
          footprintHeight(1) {
    }
};

inline int ResolveLotAutoCoordinateReference(std::uint8_t reference, int lotSize, int itemSize, int primaryStart, int primarySize, bool hasPrimary) {
    switch (reference) {
        case kLotAutoReferenceLotCenter:
            return (lotSize - itemSize) / 2;
        case kLotAutoReferenceLotEnd:
            return lotSize - itemSize;
        case kLotAutoReferencePrimaryStart:
            return hasPrimary ? primaryStart : 0;
        case kLotAutoReferencePrimaryCenter:
            return hasPrimary ? primaryStart + ((primarySize - itemSize) / 2) : (lotSize - itemSize) / 2;
        case kLotAutoReferencePrimaryEnd:
            return hasPrimary ? primaryStart + primarySize - itemSize : lotSize - itemSize;
        default:
            return 0;
    }
}

inline bool LotAutoPrimaryRequirementMatches(const std::vector<std::string>& primaryModuleIds, const LotAutoPrimaryGeometry& primary) {
    if (primaryModuleIds.empty()) {
        return true;
    }
    if (!primary.hasPrimary) {
        return false;
    }

    std::size_t primaryIndex = 0;
    for (; primaryIndex < primaryModuleIds.size(); ++primaryIndex) {
        if (primaryModuleIds[primaryIndex] == primary.moduleId) {
            return true;
        }
    }

    return false;
}

inline Int2 ResolveLotAutoOrigin(
    std::uint8_t xReference,
    std::uint8_t yReference,
    int xOffset,
    int yOffset,
    int itemWidth,
    int itemHeight,
    int lotWidth,
    int lotHeight,
    const LotAutoPrimaryGeometry& primary) {
    return Int2(
        ResolveLotAutoCoordinateReference(xReference, lotWidth, itemWidth, primary.localOrigin.x, primary.footprintWidth, primary.hasPrimary) + xOffset,
        ResolveLotAutoCoordinateReference(yReference, lotHeight, itemHeight, primary.localOrigin.y, primary.footprintHeight, primary.hasPrimary) + yOffset);
}

inline LotModulePlacementDefinition BuildLotAutoModulePlacementDefinition(
    const LotAutoModuleRule& rule,
    const LotModule& module,
    int lotWidth,
    int lotHeight,
    const LotAutoPrimaryGeometry& primary) {
    LotModulePlacementDefinition placement;
    placement.moduleId = rule.moduleId;
    placement.footprintWidth = rule.footprintWidth;
    placement.footprintHeight = rule.footprintHeight;
    placement.renderOffsetX = rule.renderOffsetX;
    placement.renderOffsetY = rule.renderOffsetY;
    placement.renderWidth = rule.renderWidth;
    placement.renderHeight = rule.renderHeight;
    placement.hasRenderOffsetX = rule.hasRenderOffsetX;
    placement.hasRenderOffsetY = rule.hasRenderOffsetY;
    placement.hasRenderWidth = rule.hasRenderWidth;
    placement.hasRenderHeight = rule.hasRenderHeight;
    placement.renderAlignX = rule.renderAlignX;
    placement.renderAlignY = rule.renderAlignY;
    placement.affectsSimulation = rule.affectsSimulation;
    placement.claimsFootprint = rule.claimsFootprint;
    placement.alternatives = rule.alternatives;

    const LotModulePlacementGeometry geometry = ResolveLotModulePlacementGeometry(placement, module);
    placement.localOrigin = ResolveLotAutoOrigin(
        rule.xReference,
        rule.yReference,
        rule.xOffset,
        rule.yOffset,
        geometry.footprintWidth,
        geometry.footprintHeight,
        lotWidth,
        lotHeight,
        primary);
    return placement;
}

inline Int2 ResolveLotAutoAccessTile(
    const LotAccessDefinition& access,
    int lotWidth,
    int lotHeight,
    const LotAutoPrimaryGeometry& primary) {
    return ResolveLotAutoOrigin(
        access.xReference,
        access.yReference,
        access.xOffset,
        access.yOffset,
        1,
        1,
        lotWidth,
        lotHeight,
        primary);
}
