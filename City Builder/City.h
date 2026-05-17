#pragma once

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "Lot.h"
#include "RciTool.h"
#include "RoadLane.h"
#include "Tile.h"

struct CitySaveLotModuleState {
    std::string moduleAssetId;
    Int2 localOrigin;

    CitySaveLotModuleState()
        : localOrigin(0, 0) {
    }
};

struct CitySaveLotState {
    int lotId;
    std::string assetId;
    int anchorTileX;
    int anchorTileY;
    int rotationSteps;
    int constructionTotalTicks;
    int constructionRemainingTicks;
    std::vector<CitySaveLotModuleState> modules;

    CitySaveLotState()
        : lotId(-1),
          anchorTileX(0),
          anchorTileY(0),
          rotationSteps(0),
          constructionTotalTicks(0),
          constructionRemainingTicks(0) {
    }
};

struct TransportTileSaveState {
    TransportLayerId layer;
    int tileIndex;
    std::vector<RoadLanePlacement> lanes;

    TransportTileSaveState()
        : layer(TransportLayerId::Ground),
          tileIndex(0) {
    }
};

struct TransportNetworkSaveState {
    std::vector<TransportTileSaveState> tiles;
};

struct CitySaveState {
    int width;
    int height;
    int nextLotId;
    int cameraX;
    int cameraY;
    int visibleTiles;
    std::uint64_t simulationTick;
    std::vector<Tile> tiles;
    std::vector<RciLot> zoningLots;
    std::vector<CitySaveLotState> lots;
    std::vector<LotRenderInstance> previewLots;
    std::vector<float> cityParameters;
    TransportNetworkSaveState transport;

    CitySaveState()
        : width(1024),
          height(1024),
          nextLotId(1),
          cameraX(384),
          cameraY(384),
          visibleTiles(256),
          simulationTick(0) {
    }
};

class City {
public:
    City();
    City(const std::string& name, int regionX, int regionY, int width, int height);

    const std::string& name() const;
    int regionX() const;
    int regionY() const;
    int width() const;
    int height() const;
    int cameraX() const;
    int cameraY() const;
    int visibleTiles() const;
    int population() const;
    const std::vector<float>& cityParameters() const;
    bool hasSaveState() const;
    bool isSaveStateDirty() const;
    const CitySaveState& saveState() const;
    int previewWidth() const;
    int previewHeight() const;
    std::uint64_t previewRevision() const;
    bool hasPreviewBuildInFlight() const;
    bool isPreviewBuildReady() const;

    void setSaveState(const CitySaveState& saveState, bool isDirty = true);
    void setMetadataFromSaveState(const CitySaveState& saveState);
    void setCamera(int cameraX, int cameraY, int visibleTiles);
    void markSaveStateClean();
    void unloadCleanSaveState();
    void unloadSaveState();
    void clearPreview();
    void startPreviewBuild(std::future<CitySaveState>&& previewBuildFuture);
    CitySaveState takePreviewBuildState();

    static const int kDefaultWidth = 1024;
    static const int kDefaultHeight = 1024;
    static const int kPreviewWidth = 4096;
    static const int kPreviewHeight = 4096;
    static const int kDefaultVisibleTiles = 256;

    static std::uint32_t seedForRegionCoordinate(int regionX, int regionY);
    static CitySaveState createDefaultSaveState(std::uint32_t seed, int width = kDefaultWidth, int height = kDefaultHeight);
    static int centeredCameraCoordinate(int mapSize, int visibleTiles);

private:
    void applySaveStateMetadata(const CitySaveState& saveState);
    void bumpPreviewRevision();

    std::string name_;
    int regionX_;
    int regionY_;
    int width_;
    int height_;
    int cameraX_;
    int cameraY_;
    int visibleTiles_;
    std::vector<float> cityParameters_;
    std::unique_ptr<CitySaveState> saveState_;
    bool saveStateDirty_;
    std::uint64_t previewRevision_;
    bool previewBuildInFlight_;
    std::future<CitySaveState> previewBuildFuture_;
};

class Region {
public:
    Region();

    void clear();
    void createDefault();
    City* cityAt(int regionX, int regionY);
    const City* cityAt(int regionX, int regionY) const;
    std::vector<std::unique_ptr<City> >& cities();
    const std::vector<std::unique_ptr<City> >& cities() const;
    const std::vector<float>& regionParameters() const;
    int population() const;
    std::uint64_t revision() const;

    void addCity(std::unique_ptr<City> city);
    void recalculateRegionParameters();

private:
    std::vector<std::unique_ptr<City> > cities_;
    std::vector<float> regionParameters_;
    std::uint64_t revision_;
};
