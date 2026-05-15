#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Lot.h"
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
    std::vector<CitySaveLotModuleState> modules;

    CitySaveLotState()
        : lotId(-1),
          anchorTileX(0),
          anchorTileY(0),
          rotationSteps(0) {
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
    std::uint32_t nextRoadStrokeId;
    std::vector<TransportTileSaveState> tiles;

    TransportNetworkSaveState()
        : nextRoadStrokeId(1u) {
    }
};

struct CitySaveState {
    int width;
    int height;
    int nextLotId;
    std::vector<Tile> tiles;
    std::vector<CitySaveLotState> lots;
    std::vector<LotRenderInstance> previewLots;
    std::vector<float> cityParameters;
    TransportNetworkSaveState transport;

    CitySaveState()
        : width(1024),
          height(1024),
          nextLotId(1) {
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
    const CitySaveState& saveState() const;
    const std::vector<std::uint8_t>& previewPixels() const;
    int previewWidth() const;
    int previewHeight() const;
    std::uint64_t previewRevision() const;
    bool hasPreviewPixels() const;

    void setSaveState(const CitySaveState& saveState);
    void generateDefaultState(std::uint32_t seed);
    void setPreviewPixels(const std::vector<std::uint8_t>& previewPixels);
    void clearPreviewPixels();

    static const int kDefaultWidth = 1024;
    static const int kDefaultHeight = 1024;
    static const int kPreviewWidth = 4096;
    static const int kPreviewHeight = 4096;

private:
    std::string name_;
    int regionX_;
    int regionY_;
    CitySaveState saveState_;
    std::vector<std::uint8_t> previewPixels_;
    std::uint64_t previewRevision_;
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
    std::uint64_t revision() const;

    void addCity(std::unique_ptr<City> city);
    void recalculateRegionParameters();

private:
    std::vector<std::unique_ptr<City> > cities_;
    std::vector<float> regionParameters_;
    std::uint64_t revision_;
};
