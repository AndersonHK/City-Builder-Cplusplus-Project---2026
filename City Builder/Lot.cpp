#include "Lot.h"

Lot::Lot()
    : renderOriginX_(0),
      renderOriginY_(0),
      footprintWidth_(1),
      footprintHeight_(1),
      airPollutionEmit_(0),
      landValueEmit_(0),
      renderHeight_(0.5f),
      colorR_(0.4f),
      colorG_(0.4f),
      colorB_(0.4f) {
}

Lot::Lot(int clickedTileX, int clickedTileY, const LotDefinition& definition, int mapWidth)
    : renderOriginX_(clickedTileX + definition.renderOriginOffsetX),
      renderOriginY_(clickedTileY + definition.renderOriginOffsetY),
      footprintWidth_(definition.footprintWidth),
      footprintHeight_(definition.footprintHeight),
      airPollutionEmit_(0),
      landValueEmit_(0),
      renderHeight_(0.5f),
      colorR_(0.4f),
      colorG_(0.4f),
      colorB_(0.4f) {
    std::size_t offsetIndex = 0;
    for (; offsetIndex < definition.occupiedOffsets.size(); ++offsetIndex) {
        const Int2& occupiedOffset = definition.occupiedOffsets[offsetIndex];
        const int tileX = clickedTileX + occupiedOffset.x;
        const int tileY = clickedTileY + occupiedOffset.y;
        occupiedTileIndices_.push_back((tileY * mapWidth) + tileX);
    }

    for (offsetIndex = 0; offsetIndex < definition.modules.size(); ++offsetIndex) {
        const LotModule* module = definition.modules[offsetIndex];
        if (module == 0) {
            continue;
        }

        airPollutionEmit_ += module->airPollutionEmit;
        landValueEmit_ += module->landValueEmit;
    }

    if (airPollutionEmit_ > 0 && landValueEmit_ < 0) {
        renderHeight_ = 1.75f;
        colorR_ = 0.34f;
        colorG_ = 0.32f;
        colorB_ = 0.29f;
    } else if (landValueEmit_ > 0) {
        renderHeight_ = 0.35f;
        colorR_ = 0.18f;
        colorG_ = 0.54f;
        colorB_ = 0.23f;
    }
}

void Lot::applyEffects(std::vector<Tile>& tiles) const {
    if (occupiedTileIndices_.empty()) {
        return;
    }

    const int tileCount = static_cast<int>(occupiedTileIndices_.size());
    const int pollutionPerTile = airPollutionEmit_ / tileCount;
    const int landValuePerTile = landValueEmit_ / tileCount;

    std::size_t tileIndex = 0;
    for (; tileIndex < occupiedTileIndices_.size(); ++tileIndex) {
        Tile& tile = tiles[occupiedTileIndices_[tileIndex]];
        tile.airPollution += pollutionPerTile;
        tile.landValue += landValuePerTile;
    }
}

const std::vector<int>& Lot::occupiedTileIndices() const {
    return occupiedTileIndices_;
}

LotRenderInstance Lot::buildRenderInstance() const {
    LotRenderInstance renderInstance;
    renderInstance.originX = renderOriginX_;
    renderInstance.originY = renderOriginY_;
    renderInstance.width = footprintWidth_;
    renderInstance.height = footprintHeight_;
    renderInstance.renderHeight = renderHeight_;
    renderInstance.colorR = colorR_;
    renderInstance.colorG = colorG_;
    renderInstance.colorB = colorB_;
    return renderInstance;
}
