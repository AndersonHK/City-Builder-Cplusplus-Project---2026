#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "City.h"
#include "SimulationRuntime.h"

enum class GameMode {
    Region,
    City
};

struct LoadingStatus {
    bool active;
    std::string label;
    float progress;

    LoadingStatus()
        : active(false),
          label(),
          progress(0.0f) {
    }
};

class GameSession {
public:
    typedef std::function<void(const LoadingStatus&)> LoadingPresenter;

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
    void setGameSpeed(GameSpeed gameSpeed);
    GameSpeed gameSpeed() const;
    City* activeCity();
    const City* activeCity() const;
    bool activeCityHasUnsavedChanges() const;
    bool isLoading() const;
    const LoadingStatus& loadingStatus() const;
    void setLoadingPresenter(const LoadingPresenter& loadingPresenter);
    void clearLoadingPresenter();
    void setSaveDirectoryOverride(const std::string& saveDirectoryOverride);
    std::uint64_t renderStateRevision() const;

    void setActiveCityCamera(int cameraX, int cameraY, int visibleTiles);
    bool enterCity(int regionX, int regionY);
    void exitToRegion();
    bool discardActiveCityChanges();
    bool saveAutoslot();
    bool loadAutoslot();
    bool requestCityPreviewBuild(City& city);
    bool takeReadyCityPreviewState(City& city, CitySaveState& saveState);
    bool loadCityPreviewPixels(const City& city, std::vector<std::uint8_t>& previewPixels) const;
    bool saveCityPreviewPixels(const City& city, const std::vector<std::uint8_t>& previewPixels) const;

private:
    bool loadRegionFromDisk();
    bool saveRegionToDisk(float progressStart, float progressEnd);
    CitySaveState loadCitySaveState(City& city);
    CitySaveState loadCitySaveStateFromDiskOrDefault(City& city);
    bool saveCityStateToDisk(City& city, const CitySaveState& saveState);
    void exportActiveCity();
    void beginLoadingStage(const std::string& label, float progress);
    void updateLoadingStage(const std::string& label, float progress);
    void finishLoadingStage(bool invalidatesRenderState);
    void presentLoadingStage() const;
    std::string saveDirectory() const;
    std::string saveFilePath() const;
    std::string citySaveFilePath(const City& city) const;
    std::string cityPreviewFilePath(const City& city) const;
    void ensureSaveDirectory() const;

    RuntimeOptions runtimeOptions_;
    Region region_;
    std::unique_ptr<SimulationRuntime> runtime_;
    GameMode mode_;
    City* activeCity_;
    LoadingStatus loadingStatus_;
    LoadingPresenter loadingPresenter_;
    std::string saveDirectoryOverride_;
    std::uint64_t renderStateRevision_;
};
