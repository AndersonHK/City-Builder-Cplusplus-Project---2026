#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "City.h"
#include "SimulationRuntime.h"

enum class GameMode {
    Region,
    City
};

class GameSession {
public:
    explicit GameSession(const RuntimeOptions& runtimeOptions);
    ~GameSession();

    void loadOrCreateRegion();
    void shutdown();

    bool isRegionMode() const;
    bool isCityMode() const;
    GameMode mode() const;
    Region& region();
    const Region& region() const;
    SimulationRuntime& runtime();
    const SimulationRuntime& runtime() const;
    City* activeCity();
    const City* activeCity() const;
    bool isLoading() const;
    std::uint64_t renderStateRevision() const;

    void setActiveCityCamera(int cameraX, int cameraY, int visibleTiles);
    bool enterCity(int regionX, int regionY);
    void exitToRegion();
    bool saveAutoslot();
    bool loadAutoslot();
    bool requestCityPreviewBuild(City& city);
    bool takeReadyCityPreviewState(City& city, CitySaveState& saveState);

private:
    bool loadRegionFromDisk();
    bool saveRegionToDisk();
    CitySaveState loadCitySaveState(City& city);
    bool saveCityStateToDisk(City& city, const CitySaveState& saveState);
    void exportActiveCity();
    void beginLoadingStage();
    void finishLoadingStage(bool invalidatesRenderState);
    std::string saveDirectory() const;
    std::string saveFilePath() const;
    std::string citySaveFilePath(const City& city) const;
    void ensureSaveDirectory() const;

    RuntimeOptions runtimeOptions_;
    Region region_;
    std::unique_ptr<SimulationRuntime> runtime_;
    GameMode mode_;
    City* activeCity_;
    bool isLoading_;
    std::uint64_t renderStateRevision_;
};
