#include "GameSession.h"

#include <algorithm>
#include <cstdint>
#include <direct.h>
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "CrashLogger.h"

namespace {
const std::uint32_t kRegionSaveMagic = 0x52424743u; // CBGR
const std::uint32_t kRegionSaveVersion = 3u;
const std::uint32_t kCitySaveMagic = 0x59544243u; // CBTY
const std::uint32_t kCitySaveVersion = 7u;
const int kMaximumPreviewBuildsInFlight = 2;

std::string GetExecutableDirectory() {
    char modulePath[MAX_PATH];
    const DWORD pathLength = GetModuleFileNameA(0, modulePath, MAX_PATH);
    std::string fullPath(modulePath, modulePath + pathLength);
    const std::string::size_type separatorIndex = fullPath.find_last_of("\\/");
    if (separatorIndex == std::string::npos) {
        return ".";
    }

    return fullPath.substr(0, separatorIndex);
}

template <typename T>
void WriteValue(std::ostream& stream, const T& value) {
    stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
    if (!stream) {
        throw std::runtime_error("Failed to write save data.");
    }
}

template <typename T>
void ReadValue(std::istream& stream, T& value) {
    stream.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!stream) {
        throw std::runtime_error("Failed to read save data.");
    }
}

void WriteString(std::ostream& stream, const std::string& value) {
    const std::uint32_t length = static_cast<std::uint32_t>(value.size());
    WriteValue(stream, length);
    if (length > 0u) {
        stream.write(value.data(), length);
        if (!stream) {
            throw std::runtime_error("Failed to write save string.");
        }
    }
}

std::string ReadString(std::istream& stream) {
    std::uint32_t length = 0u;
    ReadValue(stream, length);
    std::string value;
    value.resize(length);
    if (length > 0u) {
        stream.read(&value[0], length);
        if (!stream) {
            throw std::runtime_error("Failed to read save string.");
        }
    }
    return value;
}

void WriteInt2(std::ostream& stream, const Int2& value) {
    WriteValue(stream, value.x);
    WriteValue(stream, value.y);
}

Int2 ReadInt2(std::istream& stream) {
    Int2 value;
    ReadValue(stream, value.x);
    ReadValue(stream, value.y);
    return value;
}

void WriteRciLot(std::ostream& stream, const RciLot& lot) {
    WriteString(stream, lot.toolId);
    WriteString(stream, lot.name);
    WriteValue(stream, lot.zoningType);
    WriteValue(stream, lot.color.r);
    WriteValue(stream, lot.color.g);
    WriteValue(stream, lot.color.b);
    WriteValue(stream, lot.color.a);
    WriteValue(stream, lot.rect.minTileX);
    WriteValue(stream, lot.rect.minTileY);
    WriteValue(stream, lot.rect.maxTileX);
    WriteValue(stream, lot.rect.maxTileY);
    WriteValue(stream, lot.availableAfterTick);
}

RciLot ReadRciLot(std::istream& stream) {
    RciLot lot;
    lot.toolId = ReadString(stream);
    lot.name = ReadString(stream);
    ReadValue(stream, lot.zoningType);
    ReadValue(stream, lot.color.r);
    ReadValue(stream, lot.color.g);
    ReadValue(stream, lot.color.b);
    ReadValue(stream, lot.color.a);
    ReadValue(stream, lot.rect.minTileX);
    ReadValue(stream, lot.rect.minTileY);
    ReadValue(stream, lot.rect.maxTileX);
    ReadValue(stream, lot.rect.maxTileY);
    ReadValue(stream, lot.availableAfterTick);
    return lot;
}

void WriteTile(std::ostream& stream, const Tile& tile) {
    WriteValue(stream, tile.landValue);
    WriteValue(stream, tile.airPollution);
    const std::uint8_t isVacant = tile.isVacant ? 1u : 0u;
    WriteValue(stream, isVacant);
    WriteValue(stream, tile.zoningType);
}

Tile ReadTile(std::istream& stream) {
    Tile tile;
    std::uint8_t isVacant = 0u;
    ReadValue(stream, tile.landValue);
    ReadValue(stream, tile.airPollution);
    ReadValue(stream, isVacant);
    ReadValue(stream, tile.zoningType);
    tile.isVacant = isVacant != 0u;
    return tile;
}

void WriteRoadLanePlacement(std::ostream& stream, const RoadLanePlacement& lane) {
    WriteValue(stream, lane.tileX);
    WriteValue(stream, lane.tileY);
    WriteValue(stream, lane.tileIndex);
    WriteValue(stream, static_cast<std::uint8_t>(lane.family));
    WriteValue(stream, static_cast<std::uint8_t>(lane.layer));
    WriteValue(stream, lane.templateId);
    WriteValue(stream, lane.strokeId);
    WriteValue(stream, lane.laneIndex);
    WriteValue(stream, static_cast<std::uint8_t>(lane.axis));
    WriteValue(stream, lane.crossSectionMask);
    WriteValue(stream, static_cast<std::uint8_t>(lane.laneType));
    WriteValue(stream, static_cast<std::uint8_t>(lane.surface));
    WriteValue(stream, static_cast<std::uint8_t>(lane.role));
    WriteValue(stream, static_cast<std::uint8_t>(lane.separatorStyle));
    WriteValue(stream, lane.laneTravelMask);
    WriteValue(stream, lane.arrowTravelMask);
    WriteValue(stream, lane.sideMin);
    WriteValue(stream, lane.sideMax);
    WriteValue(stream, lane.sidewalkEdgeMask);
    WriteValue(stream, lane.sameDirectionDividerMask);
    WriteValue(stream, lane.opposingDirectionDividerMask);
    const std::uint8_t active = lane.active ? 1u : 0u;
    WriteValue(stream, active);
}

RoadLanePlacement ReadRoadLanePlacement(std::istream& stream) {
    RoadLanePlacement lane;
    std::uint8_t value = 0u;
    ReadValue(stream, lane.tileX);
    ReadValue(stream, lane.tileY);
    ReadValue(stream, lane.tileIndex);
    ReadValue(stream, value);
    lane.family = static_cast<RoadFamily>(value);
    ReadValue(stream, value);
    lane.layer = static_cast<TransportLayerId>(value);
    ReadValue(stream, lane.templateId);
    ReadValue(stream, lane.strokeId);
    ReadValue(stream, lane.laneIndex);
    ReadValue(stream, value);
    lane.axis = static_cast<RoadAxis>(value);
    ReadValue(stream, lane.crossSectionMask);
    ReadValue(stream, value);
    lane.laneType = static_cast<RoadLaneTypeId>(value);
    ReadValue(stream, value);
    lane.surface = static_cast<RoadLaneSurface>(value);
    ReadValue(stream, value);
    lane.role = static_cast<RoadLaneRole>(value);
    ReadValue(stream, value);
    lane.separatorStyle = static_cast<RoadSeparatorStyle>(value);
    ReadValue(stream, lane.laneTravelMask);
    ReadValue(stream, lane.arrowTravelMask);
    ReadValue(stream, lane.sideMin);
    ReadValue(stream, lane.sideMax);
    ReadValue(stream, lane.sidewalkEdgeMask);
    ReadValue(stream, lane.sameDirectionDividerMask);
    ReadValue(stream, lane.opposingDirectionDividerMask);
    ReadValue(stream, value);
    lane.active = value != 0u;
    return lane;
}

void WriteTransportStrokeSaveState(std::ostream& stream, const TransportStrokeSaveState& stroke) {
    WriteValue(stream, stroke.strokeId);
    WriteInt2(stream, stroke.startTile);
    WriteInt2(stream, stroke.cornerTile);
    WriteInt2(stream, stroke.endTile);
    WriteValue(stream, static_cast<std::uint8_t>(stroke.family));
    WriteValue(stream, static_cast<std::uint8_t>(stroke.layer));
    WriteValue(stream, stroke.laneCount);
    WriteValue(stream, static_cast<std::uint8_t>(stroke.trafficSide));
    WriteValue(stream, static_cast<std::uint8_t>(stroke.directionMode));
}

TransportStrokeSaveState ReadTransportStrokeSaveState(std::istream& stream) {
    TransportStrokeSaveState stroke;
    std::uint8_t value = 0u;
    ReadValue(stream, stroke.strokeId);
    stroke.startTile = ReadInt2(stream);
    stroke.cornerTile = ReadInt2(stream);
    stroke.endTile = ReadInt2(stream);
    ReadValue(stream, value);
    stroke.family = static_cast<RoadFamily>(value);
    ReadValue(stream, value);
    stroke.layer = static_cast<TransportLayerId>(value);
    ReadValue(stream, stroke.laneCount);
    ReadValue(stream, value);
    stroke.trafficSide = static_cast<RoadTrafficSide>(value);
    ReadValue(stream, value);
    stroke.directionMode = static_cast<RoadDirectionMode>(value);
    return stroke;
}

void WriteTransportTileEraseSaveState(std::ostream& stream, const TransportTileEraseSaveState& erasure) {
    WriteValue(stream, static_cast<std::uint8_t>(erasure.layer));
    WriteValue(stream, erasure.tileIndex);
}

TransportTileEraseSaveState ReadTransportTileEraseSaveState(std::istream& stream) {
    TransportTileEraseSaveState erasure;
    std::uint8_t value = 0u;
    ReadValue(stream, value);
    erasure.layer = static_cast<TransportLayerId>(value);
    ReadValue(stream, erasure.tileIndex);
    return erasure;
}

void WriteLotRenderInstance(std::ostream& stream, const LotRenderInstance& lot) {
    WriteValue(stream, lot.lotId);
    WriteValue(stream, lot.originX);
    WriteValue(stream, lot.originY);
    WriteValue(stream, lot.width);
    WriteValue(stream, lot.height);
    WriteValue(stream, lot.renderHeight);
    WriteValue(stream, lot.colorR);
    WriteValue(stream, lot.colorG);
    WriteValue(stream, lot.colorB);
    WriteValue(stream, lot.surfacePattern);
    WriteValue(stream, lot.surfaceDirection);
}

LotRenderInstance ReadLotRenderInstance(std::istream& stream) {
    LotRenderInstance lot;
    ReadValue(stream, lot.lotId);
    ReadValue(stream, lot.originX);
    ReadValue(stream, lot.originY);
    ReadValue(stream, lot.width);
    ReadValue(stream, lot.height);
    ReadValue(stream, lot.renderHeight);
    ReadValue(stream, lot.colorR);
    ReadValue(stream, lot.colorG);
    ReadValue(stream, lot.colorB);
    ReadValue(stream, lot.surfacePattern);
    ReadValue(stream, lot.surfaceDirection);
    return lot;
}

void WriteCityState(std::ostream& stream, const CitySaveState& state) {
    WriteValue(stream, state.width);
    WriteValue(stream, state.height);
    WriteValue(stream, state.nextLotId);
    WriteValue(stream, state.cameraX);
    WriteValue(stream, state.cameraY);
    WriteValue(stream, state.visibleTiles);
    WriteValue(stream, state.simulationTick);

    std::uint32_t count = static_cast<std::uint32_t>(state.tiles.size());
    WriteValue(stream, count);
    std::size_t tileIndex = 0;
    for (; tileIndex < state.tiles.size(); ++tileIndex) {
        WriteTile(stream, state.tiles[tileIndex]);
    }

    count = static_cast<std::uint32_t>(state.zoningLots.size());
    WriteValue(stream, count);
    std::size_t zoningLotIndex = 0;
    for (; zoningLotIndex < state.zoningLots.size(); ++zoningLotIndex) {
        WriteRciLot(stream, state.zoningLots[zoningLotIndex]);
    }

    count = static_cast<std::uint32_t>(state.lots.size());
    WriteValue(stream, count);
    std::size_t lotIndex = 0;
    for (; lotIndex < state.lots.size(); ++lotIndex) {
        const CitySaveLotState& lot = state.lots[lotIndex];
        WriteValue(stream, lot.lotId);
        WriteString(stream, lot.assetId);
        WriteValue(stream, lot.anchorTileX);
        WriteValue(stream, lot.anchorTileY);
        WriteValue(stream, lot.rotationSteps);
        WriteValue(stream, lot.constructionTotalTicks);
        WriteValue(stream, lot.constructionRemainingTicks);
        const std::uint32_t moduleCount = static_cast<std::uint32_t>(lot.modules.size());
        WriteValue(stream, moduleCount);
        std::size_t moduleIndex = 0;
        for (; moduleIndex < lot.modules.size(); ++moduleIndex) {
            WriteString(stream, lot.modules[moduleIndex].moduleAssetId);
            WriteInt2(stream, lot.modules[moduleIndex].localOrigin);
        }
    }

    count = static_cast<std::uint32_t>(state.previewLots.size());
    WriteValue(stream, count);
    for (lotIndex = 0; lotIndex < state.previewLots.size(); ++lotIndex) {
        WriteLotRenderInstance(stream, state.previewLots[lotIndex]);
    }

    count = static_cast<std::uint32_t>(state.cityParameters.size());
    WriteValue(stream, count);
    std::size_t parameterIndex = 0;
    for (; parameterIndex < state.cityParameters.size(); ++parameterIndex) {
        WriteValue(stream, state.cityParameters[parameterIndex]);
    }

    WriteValue(stream, state.transport.nextRoadStrokeId);
    count = static_cast<std::uint32_t>(state.transport.strokes.size());
    WriteValue(stream, count);
    std::size_t roadStrokeIndex = 0;
    for (; roadStrokeIndex < state.transport.strokes.size(); ++roadStrokeIndex) {
        WriteTransportStrokeSaveState(stream, state.transport.strokes[roadStrokeIndex]);
    }

    count = static_cast<std::uint32_t>(state.transport.erasures.size());
    WriteValue(stream, count);
    std::size_t erasureIndex = 0;
    for (; erasureIndex < state.transport.erasures.size(); ++erasureIndex) {
        WriteTransportTileEraseSaveState(stream, state.transport.erasures[erasureIndex]);
    }
}

CitySaveState ReadCityState(std::istream& stream) {
    CitySaveState state;
    ReadValue(stream, state.width);
    ReadValue(stream, state.height);
    ReadValue(stream, state.nextLotId);
    ReadValue(stream, state.cameraX);
    ReadValue(stream, state.cameraY);
    ReadValue(stream, state.visibleTiles);
    ReadValue(stream, state.simulationTick);

    std::uint32_t count = 0u;
    ReadValue(stream, count);
    state.tiles.resize(count);
    std::size_t tileIndex = 0;
    for (; tileIndex < state.tiles.size(); ++tileIndex) {
        state.tiles[tileIndex] = ReadTile(stream);
    }

    ReadValue(stream, count);
    state.zoningLots.resize(count);
    std::size_t zoningLotIndex = 0;
    for (; zoningLotIndex < state.zoningLots.size(); ++zoningLotIndex) {
        state.zoningLots[zoningLotIndex] = ReadRciLot(stream);
    }

    ReadValue(stream, count);
    state.lots.resize(count);
    std::size_t lotIndex = 0;
    for (; lotIndex < state.lots.size(); ++lotIndex) {
        CitySaveLotState& lot = state.lots[lotIndex];
        ReadValue(stream, lot.lotId);
        lot.assetId = ReadString(stream);
        ReadValue(stream, lot.anchorTileX);
        ReadValue(stream, lot.anchorTileY);
        ReadValue(stream, lot.rotationSteps);
        ReadValue(stream, lot.constructionTotalTicks);
        ReadValue(stream, lot.constructionRemainingTicks);
        std::uint32_t moduleCount = 0u;
        ReadValue(stream, moduleCount);
        lot.modules.resize(moduleCount);
        std::size_t moduleIndex = 0;
        for (; moduleIndex < lot.modules.size(); ++moduleIndex) {
            lot.modules[moduleIndex].moduleAssetId = ReadString(stream);
            lot.modules[moduleIndex].localOrigin = ReadInt2(stream);
        }
    }

    ReadValue(stream, count);
    state.previewLots.resize(count);
    for (lotIndex = 0; lotIndex < state.previewLots.size(); ++lotIndex) {
        state.previewLots[lotIndex] = ReadLotRenderInstance(stream);
    }

    ReadValue(stream, count);
    state.cityParameters.resize(count);
    std::size_t parameterIndex = 0;
    for (; parameterIndex < state.cityParameters.size(); ++parameterIndex) {
        ReadValue(stream, state.cityParameters[parameterIndex]);
    }

    ReadValue(stream, state.transport.nextRoadStrokeId);
    ReadValue(stream, count);
    state.transport.strokes.resize(count);
    std::size_t roadStrokeIndex = 0;
    for (; roadStrokeIndex < state.transport.strokes.size(); ++roadStrokeIndex) {
        state.transport.strokes[roadStrokeIndex] = ReadTransportStrokeSaveState(stream);
    }

    ReadValue(stream, count);
    state.transport.erasures.resize(count);
    std::size_t erasureIndex = 0;
    for (; erasureIndex < state.transport.erasures.size(); ++erasureIndex) {
        state.transport.erasures[erasureIndex] = ReadTransportTileEraseSaveState(stream);
    }

    return state;
}
}

GameSession::GameSession(const RuntimeOptions& runtimeOptions)
    : runtimeOptions_(runtimeOptions),
      runtime_(new SimulationRuntime(runtimeOptions)),
      mode_(GameMode::Region),
      activeCity_(0),
      isLoading_(false),
      renderStateRevision_(0) {
}

GameSession::~GameSession() {
    shutdown();
}

void GameSession::loadOrCreateRegion() {
    CrashScope crashScope("GameSession::loadOrCreateRegion");

    if (!loadRegionFromDisk()) {
        region_.createDefault();
        std::cout << "Created default 3x3 region." << std::endl;
    }
}

void GameSession::shutdown() {
    if (runtime_) {
        runtime_->stop();
    }
}

bool GameSession::isRegionMode() const {
    return mode_ == GameMode::Region;
}

bool GameSession::isCityMode() const {
    return mode_ == GameMode::City;
}

GameMode GameSession::mode() const {
    return mode_;
}

Region& GameSession::region() {
    return region_;
}

const Region& GameSession::region() const {
    return region_;
}

SimulationRuntime& GameSession::runtime() {
    return *runtime_;
}

const SimulationRuntime& GameSession::runtime() const {
    return *runtime_;
}

City* GameSession::activeCity() {
    return activeCity_;
}

const City* GameSession::activeCity() const {
    return activeCity_;
}

bool GameSession::isLoading() const {
    return isLoading_;
}

std::uint64_t GameSession::renderStateRevision() const {
    return renderStateRevision_;
}

void GameSession::setActiveCityCamera(int cameraX, int cameraY, int visibleTiles) {
    if (activeCity_ != 0) {
        activeCity_->setCamera(cameraX, cameraY, visibleTiles);
    }
}

bool GameSession::enterCity(int regionX, int regionY) {
    CrashScope crashScope("GameSession::enterCity");

    City* city = region_.cityAt(regionX, regionY);
    if (city == 0) {
        return false;
    }

    beginLoadingStage();
    try {
        if (mode_ == GameMode::City) {
            runtime_->stop();
            exportActiveCity();
        }

        runtime_->stop();
        CitySaveState saveState = loadCitySaveState(*city);
        runtime_->importCitySaveState(saveState);
        city->setMetadataFromSaveState(saveState);
        city->unloadCleanSaveState();
        activeCity_ = city;
        mode_ = GameMode::City;
        runtime_->start();
        finishLoadingStage(true);
        std::cout << "Entered city at region " << regionX << ", " << regionY << ": " << city->name() << std::endl;
        return true;
    } catch (const std::exception& error) {
        LogException("GameSession::enterCity", error);
        finishLoadingStage(false);
        throw;
    } catch (...) {
        LogError("GameSession::enterCity", "unknown exception.");
        finishLoadingStage(false);
        throw;
    }
}

void GameSession::exitToRegion() {
    if (mode_ != GameMode::City) {
        return;
    }

    runtime_->stop();
    exportActiveCity();
    activeCity_ = 0;
    mode_ = GameMode::Region;
    std::cout << "Returned to region." << std::endl;
}

bool GameSession::saveAutoslot() {
    if (mode_ == GameMode::City) {
        const bool wasRunning = true;
        runtime_->stop();
        exportActiveCity();
        const bool saved = saveRegionToDisk();
        if (wasRunning) {
            runtime_->start();
        }
        return saved;
    }

    return saveRegionToDisk();
}

bool GameSession::loadAutoslot() {
    CrashScope crashScope("GameSession::loadAutoslot");

    beginLoadingStage();
    const bool reloadActiveCity = mode_ == GameMode::City && activeCity_ != 0;
    int activeRegionX = 0;
    int activeRegionY = 0;
    if (reloadActiveCity) {
        activeRegionX = activeCity_->regionX();
        activeRegionY = activeCity_->regionY();
    }

    try {
        runtime_->stop();
        activeCity_ = 0;
        mode_ = GameMode::Region;

        const bool loaded = loadRegionFromDisk();
        if (!loaded) {
            region_.createDefault();
            std::cout << "Load failed; created a new default region." << std::endl;
        }

        bool importedCity = false;
        if (reloadActiveCity) {
            City* city = region_.cityAt(activeRegionX, activeRegionY);
            if (city != 0) {
                CitySaveState saveState = loadCitySaveState(*city);
                runtime_->importCitySaveState(saveState);
                city->setMetadataFromSaveState(saveState);
                city->unloadCleanSaveState();
                activeCity_ = city;
                mode_ = GameMode::City;
                importedCity = true;
                runtime_->start();
                finishLoadingStage(true);
                std::cout << "Reloaded city at region " << activeRegionX << ", " << activeRegionY << ": " << city->name() << std::endl;
            } else {
                std::cout << "Loaded region, but active city coordinates were missing; returned to region." << std::endl;
            }
        }

        if (!importedCity) {
            finishLoadingStage(false);
        }
        return loaded;
    } catch (const std::exception& error) {
        LogException("GameSession::loadAutoslot", error);
        finishLoadingStage(false);
        throw;
    } catch (...) {
        LogError("GameSession::loadAutoslot", "unknown exception.");
        finishLoadingStage(false);
        throw;
    }
}

bool GameSession::loadRegionFromDisk() {
    std::ifstream stream(saveFilePath().c_str(), std::ios::in | std::ios::binary);
    if (!stream) {
        return false;
    }

    try {
        std::uint32_t magic = 0u;
        std::uint32_t version = 0u;
        ReadValue(stream, magic);
        ReadValue(stream, version);
        if (magic != kRegionSaveMagic || version != kRegionSaveVersion) {
            std::cout << "Region save version mismatch." << std::endl;
            return false;
        }

        std::uint32_t cityCount = 0u;
        ReadValue(stream, cityCount);
        region_.clear();
        std::uint32_t cityIndex = 0u;
        for (; cityIndex < cityCount; ++cityIndex) {
            const std::string cityName = ReadString(stream);
            int regionX = 0;
            int regionY = 0;
            int width = 0;
            int height = 0;
            ReadValue(stream, regionX);
            ReadValue(stream, regionY);
            ReadValue(stream, width);
            ReadValue(stream, height);
            int cameraX = City::centeredCameraCoordinate(width, City::kDefaultVisibleTiles);
            int cameraY = City::centeredCameraCoordinate(height, City::kDefaultVisibleTiles);
            int visibleTiles = City::kDefaultVisibleTiles;
            ReadValue(stream, cameraX);
            ReadValue(stream, cameraY);
            ReadValue(stream, visibleTiles);

            std::uint32_t parameterCount = 0u;
            ReadValue(stream, parameterCount);
            std::vector<float> cityParameters(parameterCount, 0.0f);
            std::size_t parameterIndex = 0;
            for (; parameterIndex < cityParameters.size(); ++parameterIndex) {
                ReadValue(stream, cityParameters[parameterIndex]);
            }

            std::unique_ptr<City> city(new City(cityName, regionX, regionY, width, height));
            CitySaveState summaryState;
            summaryState.width = width;
            summaryState.height = height;
            summaryState.cameraX = cameraX;
            summaryState.cameraY = cameraY;
            summaryState.visibleTiles = visibleTiles;
            summaryState.cityParameters = cityParameters;
            city->setMetadataFromSaveState(summaryState);
            region_.addCity(std::move(city));
        }

        region_.recalculateRegionParameters();
        std::cout << "Loaded region save: " << cityCount << " cities." << std::endl;
        return true;
    } catch (const std::exception& error) {
        std::cout << "Could not load region save: " << error.what() << std::endl;
        LogException("GameSession::loadRegionFromDisk", error);
    }

    return false;
}

bool GameSession::saveRegionToDisk() {
    try {
        ensureSaveDirectory();
        std::size_t cityIndex = 0;
        for (; cityIndex < region_.cities().size(); ++cityIndex) {
            City& city = *region_.cities()[cityIndex];
            CitySaveState cityState = loadCitySaveState(city);
            if (!saveCityStateToDisk(city, cityState)) {
                return false;
            }
            city.setMetadataFromSaveState(cityState);
            city.markSaveStateClean();
            city.unloadCleanSaveState();
        }

        std::ofstream stream(saveFilePath().c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!stream) {
            std::cout << "Could not open region save for writing." << std::endl;
            return false;
        }

        WriteValue(stream, kRegionSaveMagic);
        WriteValue(stream, kRegionSaveVersion);
        const std::uint32_t cityCount = static_cast<std::uint32_t>(region_.cities().size());
        WriteValue(stream, cityCount);
        for (cityIndex = 0; cityIndex < region_.cities().size(); ++cityIndex) {
            const City& city = *region_.cities()[cityIndex];
            WriteString(stream, city.name());
            WriteValue(stream, city.regionX());
            WriteValue(stream, city.regionY());
            WriteValue(stream, city.width());
            WriteValue(stream, city.height());
            WriteValue(stream, city.cameraX());
            WriteValue(stream, city.cameraY());
            WriteValue(stream, city.visibleTiles());
            const std::vector<float>& cityParameters = city.cityParameters();
            const std::uint32_t parameterCount = static_cast<std::uint32_t>(cityParameters.size());
            WriteValue(stream, parameterCount);
            std::size_t parameterIndex = 0;
            for (; parameterIndex < cityParameters.size(); ++parameterIndex) {
                WriteValue(stream, cityParameters[parameterIndex]);
            }
        }

        region_.recalculateRegionParameters();
        std::cout << "Saved region autoslot: " << saveFilePath() << std::endl;
        return true;
    } catch (const std::exception& error) {
        std::cout << "Could not save region: " << error.what() << std::endl;
        LogException("GameSession::saveRegionToDisk", error);
    }

    return false;
}

CitySaveState GameSession::loadCitySaveState(City& city) {
    if (city.hasPreviewBuildInFlight()) {
        CitySaveState saveState = city.takePreviewBuildState();
        city.setMetadataFromSaveState(saveState);
        return saveState;
    }

    if (city.hasSaveState()) {
        return city.saveState();
    }

    std::ifstream stream(citySaveFilePath(city).c_str(), std::ios::in | std::ios::binary);
    if (stream) {
        try {
            std::uint32_t magic = 0u;
            std::uint32_t version = 0u;
            ReadValue(stream, magic);
            ReadValue(stream, version);
            if (magic != kCitySaveMagic || version != kCitySaveVersion) {
                std::cout << "City save version mismatch for " << city.name() << "." << std::endl;
            } else {
                CitySaveState saveState = ReadCityState(stream);
                city.setMetadataFromSaveState(saveState);
                return saveState;
            }
        } catch (const std::exception& error) {
            std::cout << "Could not load city save for " << city.name() << ": " << error.what() << std::endl;
            LogException("GameSession::loadCitySaveState", error);
        }
    }

    CitySaveState defaultState = City::createDefaultSaveState(
        City::seedForRegionCoordinate(city.regionX(), city.regionY()),
        city.width(),
        city.height());
    city.setMetadataFromSaveState(defaultState);
    return defaultState;
}

bool GameSession::saveCityStateToDisk(City& city, const CitySaveState& saveState) {
    try {
        std::ofstream stream(citySaveFilePath(city).c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!stream) {
            std::cout << "Could not open city save for writing: " << citySaveFilePath(city) << std::endl;
            return false;
        }

        WriteValue(stream, kCitySaveMagic);
        WriteValue(stream, kCitySaveVersion);
        WriteCityState(stream, saveState);
        return true;
    } catch (const std::exception& error) {
        std::cout << "Could not save city " << city.name() << ": " << error.what() << std::endl;
        LogException("GameSession::saveCityStateToDisk", error);
    }

    return false;
}

bool GameSession::requestCityPreviewBuild(City& city) {
    if (city.hasSaveState() || city.hasPreviewBuildInFlight()) {
        return false;
    }

    int buildsInFlight = 0;
    std::size_t cityIndex = 0;
    for (; cityIndex < region_.cities().size(); ++cityIndex) {
        if (region_.cities()[cityIndex]->hasPreviewBuildInFlight()) {
            ++buildsInFlight;
        }
    }

    if (buildsInFlight >= kMaximumPreviewBuildsInFlight) {
        return false;
    }

    const std::string path = citySaveFilePath(city);
    const std::string cityName = city.name();
    const int regionX = city.regionX();
    const int regionY = city.regionY();
    const int width = city.width();
    const int height = city.height();
    std::future<CitySaveState> previewBuildFuture = std::async(std::launch::async, [path, cityName, regionX, regionY, width, height]() {
        std::ifstream stream(path.c_str(), std::ios::in | std::ios::binary);
        if (stream) {
            try {
                std::uint32_t magic = 0u;
                std::uint32_t version = 0u;
                ReadValue(stream, magic);
                ReadValue(stream, version);
                if (magic == kCitySaveMagic && version == kCitySaveVersion) {
                    return ReadCityState(stream);
                }
                std::cout << "City save version mismatch for preview " << cityName << "." << std::endl;
            } catch (const std::exception& error) {
                std::cout << "Could not load city preview state for " << cityName << ": " << error.what() << std::endl;
                LogException("GameSession::requestCityPreviewBuild", error);
            }
        }

        return City::createDefaultSaveState(City::seedForRegionCoordinate(regionX, regionY), width, height);
    });

    city.startPreviewBuild(std::move(previewBuildFuture));
    return true;
}

bool GameSession::takeReadyCityPreviewState(City& city, CitySaveState& saveState) {
    if (!city.hasPreviewBuildInFlight() || !city.isPreviewBuildReady()) {
        return false;
    }

    saveState = city.takePreviewBuildState();
    city.setMetadataFromSaveState(saveState);
    return true;
}

void GameSession::exportActiveCity() {
    if (activeCity_ == 0) {
        return;
    }

    CitySaveState saveState = runtime_->exportCitySaveState();
    saveState.cameraX = activeCity_->cameraX();
    saveState.cameraY = activeCity_->cameraY();
    saveState.visibleTiles = activeCity_->visibleTiles();
    activeCity_->setSaveState(saveState, true);
    region_.recalculateRegionParameters();
}

void GameSession::beginLoadingStage() {
    isLoading_ = true;
}

void GameSession::finishLoadingStage(bool invalidatesRenderState) {
    if (invalidatesRenderState) {
        ++renderStateRevision_;
        if (renderStateRevision_ == 0) {
            renderStateRevision_ = 1;
        }
    }
    isLoading_ = false;
}

std::string GameSession::saveDirectory() const {
    return GetExecutableDirectory() + "\\Data\\Saves";
}

std::string GameSession::saveFilePath() const {
    return saveDirectory() + "\\region.bin";
}

std::string GameSession::citySaveFilePath(const City& city) const {
    std::ostringstream fileName;
    fileName << saveDirectory() << "\\city_" << city.regionX() << "_" << city.regionY() << ".bin";
    return fileName.str();
}

void GameSession::ensureSaveDirectory() const {
    const std::string dataDirectory = GetExecutableDirectory() + "\\Data";
    _mkdir(dataDirectory.c_str());
    _mkdir(saveDirectory().c_str());
}
