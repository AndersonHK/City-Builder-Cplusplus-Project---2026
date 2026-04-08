#pragma once

#include <vector>

#include "LotModule.h"
#include "Tile.h"

struct Int2 {
    int x;
    int y;

    Int2()
        : x(0),
          y(0) {
    }

    Int2(int xValue, int yValue)
        : x(xValue),
          y(yValue) {
    }
};

struct LotRenderInstance {
    int originX;
    int originY;
    int width;
    int height;

    LotRenderInstance()
        : originX(0),
          originY(0),
          width(1),
          height(1) {
    }
};

struct LotDefinition {
    int renderOriginOffsetX;
    int renderOriginOffsetY;
    int footprintWidth;
    int footprintHeight;
    std::vector<Int2> occupiedOffsets;
    std::vector<const LotModule*> modules;

    LotDefinition()
        : renderOriginOffsetX(0),
          renderOriginOffsetY(0),
          footprintWidth(1),
          footprintHeight(1) {
    }
};

class Lot {
public:
    Lot();
    Lot(int clickedTileX, int clickedTileY, const LotDefinition& definition, int mapWidth);

    void applyEffects(std::vector<Tile>& tiles) const;
    const std::vector<int>& occupiedTileIndices() const;
    LotRenderInstance buildRenderInstance() const;

private:
    std::vector<int> occupiedTileIndices_;
    int renderOriginX_;
    int renderOriginY_;
    int footprintWidth_;
    int footprintHeight_;
    int airPollutionEmit_;
    int landValueEmit_;
};
