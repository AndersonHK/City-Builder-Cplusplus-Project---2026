#pragma once

#include "LotModule.h"

#include <algorithm>

struct LotModulePlacementGeometry {
    Int2 localOrigin;
    int footprintWidth;
    int footprintHeight;
    float renderOffsetX;
    float renderOffsetY;
    float renderWidth;
    float renderHeight;

    LotModulePlacementGeometry()
        : localOrigin(0, 0),
          footprintWidth(1),
          footprintHeight(1),
          renderOffsetX(0.0f),
          renderOffsetY(0.0f),
          renderWidth(1.0f),
          renderHeight(1.0f) {
    }
};

inline int NormalizeLotModulePlacementRotation(int rotationSteps) {
    return ((rotationSteps % 4) + 4) % 4;
}

inline Int2 RotateLotModulePlacementTile(const Int2& localTile, int rotationSteps) {
    switch (NormalizeLotModulePlacementRotation(rotationSteps)) {
        case 1:
            return Int2(-localTile.y, localTile.x);
        case 2:
            return Int2(-localTile.x, -localTile.y);
        case 3:
            return Int2(localTile.y, -localTile.x);
        default:
            return localTile;
    }
}

inline Int2 RotatedLotModulePlacementMinimum(const Int2& localOrigin, int width, int height, int rotationSteps) {
    Int2 minimum(0, 0);
    bool hasTile = false;

    int tileY = 0;
    for (; tileY < height; ++tileY) {
        int tileX = 0;
        for (; tileX < width; ++tileX) {
            const Int2 rotatedTile = RotateLotModulePlacementTile(Int2(localOrigin.x + tileX, localOrigin.y + tileY), rotationSteps);
            if (!hasTile) {
                minimum = rotatedTile;
                hasTile = true;
            } else {
                minimum.x = std::min(minimum.x, rotatedTile.x);
                minimum.y = std::min(minimum.y, rotatedTile.y);
            }
        }
    }

    return minimum;
}

inline float ResolveLotModulePlacementAlignedOffset(int footprintSize, float renderSize, std::uint8_t alignment) {
    const float availableSpace = static_cast<float>(footprintSize) - renderSize;
    if (alignment == kLotModulePlacementAlignCenter) {
        return availableSpace * 0.5f;
    }
    if (alignment == kLotModulePlacementAlignEnd) {
        return availableSpace;
    }
    return 0.0f;
}

inline LotModulePlacementGeometry ResolveLotModulePlacementGeometry(
    const LotModulePlacementDefinition& placement,
    const LotModule& module) {
    LotModulePlacementGeometry geometry;
    geometry.localOrigin = placement.localOrigin;
    geometry.footprintWidth = placement.footprintWidth > 0 ? placement.footprintWidth : module.width;
    geometry.footprintHeight = placement.footprintHeight > 0 ? placement.footprintHeight : module.height;
    geometry.renderWidth = placement.hasRenderWidth ? placement.renderWidth : static_cast<float>(module.width);
    geometry.renderHeight = placement.hasRenderHeight ? placement.renderHeight : static_cast<float>(module.height);
    geometry.renderOffsetX = placement.hasRenderOffsetX
        ? placement.renderOffsetX
        : ResolveLotModulePlacementAlignedOffset(geometry.footprintWidth, geometry.renderWidth, placement.renderAlignX);
    geometry.renderOffsetY = placement.hasRenderOffsetY
        ? placement.renderOffsetY
        : ResolveLotModulePlacementAlignedOffset(geometry.footprintHeight, geometry.renderHeight, placement.renderAlignY);
    return geometry;
}

inline LotModulePlacementGeometry RotateLotModulePlacementGeometry(const LotModulePlacementGeometry& geometry, int rotationSteps) {
    const int normalizedRotation = NormalizeLotModulePlacementRotation(rotationSteps);
    LotModulePlacementGeometry rotated;
    rotated.localOrigin = RotatedLotModulePlacementMinimum(
        geometry.localOrigin,
        geometry.footprintWidth,
        geometry.footprintHeight,
        normalizedRotation);

    switch (normalizedRotation) {
        case 1:
            rotated.footprintWidth = geometry.footprintHeight;
            rotated.footprintHeight = geometry.footprintWidth;
            rotated.renderOffsetX = static_cast<float>(geometry.footprintHeight) - geometry.renderOffsetY - geometry.renderHeight;
            rotated.renderOffsetY = geometry.renderOffsetX;
            rotated.renderWidth = geometry.renderHeight;
            rotated.renderHeight = geometry.renderWidth;
            break;
        case 2:
            rotated.footprintWidth = geometry.footprintWidth;
            rotated.footprintHeight = geometry.footprintHeight;
            rotated.renderOffsetX = static_cast<float>(geometry.footprintWidth) - geometry.renderOffsetX - geometry.renderWidth;
            rotated.renderOffsetY = static_cast<float>(geometry.footprintHeight) - geometry.renderOffsetY - geometry.renderHeight;
            rotated.renderWidth = geometry.renderWidth;
            rotated.renderHeight = geometry.renderHeight;
            break;
        case 3:
            rotated.footprintWidth = geometry.footprintHeight;
            rotated.footprintHeight = geometry.footprintWidth;
            rotated.renderOffsetX = geometry.renderOffsetY;
            rotated.renderOffsetY = static_cast<float>(geometry.footprintWidth) - geometry.renderOffsetX - geometry.renderWidth;
            rotated.renderWidth = geometry.renderHeight;
            rotated.renderHeight = geometry.renderWidth;
            break;
        default:
            rotated = geometry;
            break;
    }

    return rotated;
}

inline bool LotModulePlacementGeometryVisualFits(const LotModulePlacementGeometry& geometry) {
    const float epsilon = 0.0001f;
    return geometry.footprintWidth > 0 &&
        geometry.footprintHeight > 0 &&
        geometry.renderWidth > 0.0f &&
        geometry.renderHeight > 0.0f &&
        geometry.renderOffsetX >= -epsilon &&
        geometry.renderOffsetY >= -epsilon &&
        geometry.renderOffsetX + geometry.renderWidth <= static_cast<float>(geometry.footprintWidth) + epsilon &&
        geometry.renderOffsetY + geometry.renderHeight <= static_cast<float>(geometry.footprintHeight) + epsilon;
}
