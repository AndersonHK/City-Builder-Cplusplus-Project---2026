#pragma once

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

    bool enterCity(int regionX, int regionY);
    void exitToRegion();
    bool saveAutoslot();
    bool loadAutoslot();

private:
    bool loadRegionFromDisk();
    bool saveRegionToDisk() const;
    void exportActiveCity();
    std::string saveDirectory() const;
    std::string saveFilePath() const;
    void ensureSaveDirectory() const;

    RuntimeOptions runtimeOptions_;
    Region region_;
    std::unique_ptr<SimulationRuntime> runtime_;
    GameMode mode_;
    City* activeCity_;
};
