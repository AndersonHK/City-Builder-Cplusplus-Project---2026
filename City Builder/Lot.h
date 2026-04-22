#pragma once

#include <string>
#include <vector>

#include "LotModule.h"
#include "Tile.h"

struct LotRenderInstance {
    int lotId;
    int originX;
    int originY;
    int width;
    int height;
    float renderHeight;
    float colorR;
    float colorG;
    float colorB;

    LotRenderInstance()
        : lotId(-1),
          originX(0),
          originY(0),
          width(1),
          height(1),
          renderHeight(0.5f),
          colorR(0.4f),
          colorG(0.4f),
          colorB(0.4f) {
    }
};

struct LotAsset {
    std::string id;
    Int2 anchor;
    Int2 renderOrigin;
    std::vector<LotModulePlacementDefinition> initialModules;

    LotAsset()
        : anchor(0, 0),
          renderOrigin(0, 0) {
    }
};

class Lot {
public:
    Lot();
    Lot(int lotId, const std::string& assetId, int anchorTileX, int anchorTileY);

    int id() const;
    const std::string& assetId() const;
    int anchorTileX() const;
    int anchorTileY() const;
    const std::vector<LotModulePlacement>& modules() const;
    const std::vector<Int2>& occupiedOffsets() const;
    const std::vector<int>& occupiedTileIndices() const;

    int addModule(const LotModule& module, const Int2& localOrigin, int mapWidth);
    bool removeModule(int moduleInstanceId, int mapWidth);
    int moduleInstanceIdAtLocalTile(const Int2& localTile) const;
    bool occupiesLocalTile(const Int2& localTile) const;
    void rebaseAnchorToMinimumTile(int mapWidth);
    void applyEffects(std::vector<Tile>& tiles) const;
    LotRenderInstance buildRenderInstance() const;
    std::string moduleSummary() const;

private:
    void rebuildCachedState(int mapWidth);

    int lotId_;
    std::string assetId_;
    int anchorTileX_;
    int anchorTileY_;
    int nextModuleInstanceId_;
    std::vector<LotModulePlacement> modules_;
    std::vector<Int2> occupiedOffsets_;
    std::vector<int> occupiedTileIndices_;
    int airPollutionEmit_;
    int landValueEmit_;
    Int2 minimumOccupiedOffset_;
    Int2 maximumOccupiedOffset_;
    float renderHeight_;
    float colorR_;
    float colorG_;
    float colorB_;
};
