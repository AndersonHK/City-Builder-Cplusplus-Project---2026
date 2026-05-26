#include "GameSession.h"

#include "RuntimePaths.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <direct.h>
#include <fstream>
#include <future>
#include <iostream>
#include <limits>
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
const std::uint32_t kMinimumReadableCitySaveVersion = 11u;
const std::uint32_t kCitySaveVersion = 12u;
const std::uint32_t kCityPreviewSaveMagic = 0x56504243u; // CBPV
const std::uint32_t kCityPreviewSaveVersion = 1u;
const int kMaximumPreviewBuildsInFlight = 2;

struct SaveFileStamp {
    bool exists;
    std::uint64_t byteSize;
    std::uint32_t lastWriteLow;
    std::uint32_t lastWriteHigh;

    SaveFileStamp()
        : exists(false),
          byteSize(0u),
          lastWriteLow(0u),
          lastWriteHigh(0u) {
    }
};

void EnsureDirectoryExists(const std::string& directoryPath) {
    if (directoryPath.empty()) {
        return;
    }

    errno = 0;
    const int result = _mkdir(directoryPath.c_str());
    if (result == 0) {
        return;
    }

    if (errno == EEXIST) {
        const DWORD attributes = GetFileAttributesA(directoryPath.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
            return;
        }
    }

    std::ostringstream message;
    message << "Unable to create directory '" << directoryPath << "' (errno " << errno << ").";
    LogWarning("GameSession::ensureSaveDirectory", message.str());
}

SaveFileStamp GetSaveFileStamp(const std::string& path) {
    SaveFileStamp stamp;
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &attributes)) {
        return stamp;
    }

    if ((attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
        return stamp;
    }

    stamp.exists = true;
    stamp.byteSize =
        (static_cast<std::uint64_t>(attributes.nFileSizeHigh) << 32) |
        static_cast<std::uint64_t>(attributes.nFileSizeLow);
    stamp.lastWriteLow = attributes.ftLastWriteTime.dwLowDateTime;
    stamp.lastWriteHigh = attributes.ftLastWriteTime.dwHighDateTime;
    return stamp;
}

bool SaveFileStampsMatch(const SaveFileStamp& left, const SaveFileStamp& right) {
    return left.exists == right.exists &&
        left.byteSize == right.byteSize &&
        left.lastWriteLow == right.lastWriteLow &&
        left.lastWriteHigh == right.lastWriteHigh;
}

bool IsReadableCitySaveVersion(std::uint32_t version) {
    return version >= kMinimumReadableCitySaveVersion && version <= kCitySaveVersion;
}

std::size_t ExpectedPreviewPixelCount(const City& city) {
    return static_cast<std::size_t>(city.previewWidth()) *
        static_cast<std::size_t>(city.previewHeight()) *
        4u;
}

void BuildFallbackCityPreviewPixels(const City& city, std::vector<std::uint8_t>& previewPixels) {
    const std::size_t expectedPixelByteCount = ExpectedPreviewPixelCount(city);
    previewPixels.assign(expectedPixelByteCount, 0u);
    std::size_t pixelIndex = 0u;
    for (; pixelIndex + 3u < previewPixels.size(); pixelIndex += 4u) {
        previewPixels[pixelIndex + 0u] = 20u;
        previewPixels[pixelIndex + 1u] = 28u;
        previewPixels[pixelIndex + 2u] = 38u;
        previewPixels[pixelIndex + 3u] = 255u;
    }
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
    WriteValue(stream, lot.frontDirection);
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
    ReadValue(stream, lot.frontDirection);
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
    WriteValue(stream, tile.parkEffect);
    const std::uint8_t isVacant = tile.isVacant ? 1u : 0u;
    WriteValue(stream, isVacant);
    WriteValue(stream, tile.zoningType);
}

Tile ReadTile(std::istream& stream) {
    Tile tile;
    std::uint8_t isVacant = 0u;
    ReadValue(stream, tile.landValue);
    ReadValue(stream, tile.airPollution);
    ReadValue(stream, tile.parkEffect);
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

void WriteTransportTileSaveState(std::ostream& stream, const TransportTileSaveState& tile) {
    WriteValue(stream, static_cast<std::uint8_t>(tile.layer));
    WriteValue(stream, tile.tileIndex);
    const std::uint32_t laneCount = static_cast<std::uint32_t>(tile.lanes.size());
    WriteValue(stream, laneCount);
    std::size_t laneIndex = 0;
    for (; laneIndex < tile.lanes.size(); ++laneIndex) {
        WriteRoadLanePlacement(stream, tile.lanes[laneIndex]);
    }
}

TransportTileSaveState ReadTransportTileSaveState(std::istream& stream) {
    TransportTileSaveState tile;
    std::uint8_t value = 0u;
    ReadValue(stream, value);
    tile.layer = static_cast<TransportLayerId>(value);
    ReadValue(stream, tile.tileIndex);
    std::uint32_t laneCount = 0u;
    ReadValue(stream, laneCount);
    tile.lanes.resize(laneCount);
    std::size_t laneIndex = 0;
    for (; laneIndex < tile.lanes.size(); ++laneIndex) {
        tile.lanes[laneIndex] = ReadRoadLanePlacement(stream);
    }
    return tile;
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
            WriteValue(stream, lot.modules[moduleIndex].footprintWidth);
            WriteValue(stream, lot.modules[moduleIndex].footprintHeight);
            WriteValue(stream, lot.modules[moduleIndex].renderOffsetX);
            WriteValue(stream, lot.modules[moduleIndex].renderOffsetY);
            WriteValue(stream, lot.modules[moduleIndex].renderWidth);
            WriteValue(stream, lot.modules[moduleIndex].renderHeight);
            const std::uint8_t affectsSimulation = lot.modules[moduleIndex].affectsSimulation ? 1u : 0u;
            const std::uint8_t claimsFootprint = lot.modules[moduleIndex].claimsFootprint ? 1u : 0u;
            WriteValue(stream, affectsSimulation);
            WriteValue(stream, claimsFootprint);
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

    count = static_cast<std::uint32_t>(state.transport.tiles.size());
    WriteValue(stream, count);
    std::size_t transportTileIndex = 0;
    for (; transportTileIndex < state.transport.tiles.size(); ++transportTileIndex) {
        WriteTransportTileSaveState(stream, state.transport.tiles[transportTileIndex]);
    }
}

CitySaveState ReadCityState(std::istream& stream, std::uint32_t version) {
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
            if (version >= 12u) {
                ReadValue(stream, lot.modules[moduleIndex].footprintWidth);
                ReadValue(stream, lot.modules[moduleIndex].footprintHeight);
                ReadValue(stream, lot.modules[moduleIndex].renderOffsetX);
                ReadValue(stream, lot.modules[moduleIndex].renderOffsetY);
                ReadValue(stream, lot.modules[moduleIndex].renderWidth);
                ReadValue(stream, lot.modules[moduleIndex].renderHeight);
                std::uint8_t affectsSimulation = 1u;
                std::uint8_t claimsFootprint = 1u;
                ReadValue(stream, affectsSimulation);
                ReadValue(stream, claimsFootprint);
                lot.modules[moduleIndex].affectsSimulation = affectsSimulation != 0u;
                lot.modules[moduleIndex].claimsFootprint = claimsFootprint != 0u;
            }
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

    ReadValue(stream, count);
    state.transport.tiles.resize(count);
    std::size_t transportTileIndex = 0;
    for (; transportTileIndex < state.transport.tiles.size(); ++transportTileIndex) {
        state.transport.tiles[transportTileIndex] = ReadTransportTileSaveState(stream);
    }

    return state;
}
}

ApplicationWarning::ApplicationWarning()
    : title(),
      message() {
}

ApplicationWarning::ApplicationWarning(const std::string& warningTitle, const std::string& warningMessage)
    : title(warningTitle),
      message(warningMessage) {
}

GameSession::GameSession(const RuntimeOptions& runtimeOptions)
    : runtimeOptions_(runtimeOptions),
      runtime_(),
      mode_(GameMode::Region),
      activeCity_(0),
      loadingStatus_(),
      loadingPresenter_(),
      cityPreviewRenderer_(),
      saveDirectoryOverride_(),
      renderStateRevision_(0),
      applicationWarnings_() {
    runtimeOptions_.nonFatalAssetWarningHandler = [this] (const std::string& title, const std::string& message) {
        queueApplicationWarning(title, message);
    };
}

GameSession::~GameSession() {
    shutdown();
}

void GameSession::loadOrCreateRegion() {
    CrashScope crashScope("GameSession::loadOrCreateRegion");

    beginLoadingStage("Loading region", 0.10f);
    if (!loadRegionFromDisk()) {
        updateLoadingStage("Creating region", 0.72f);
        region_.createDefault();
        updateLoadingStage("Saving new region", 0.76f);
        if (!saveRegionToDisk(0.78f, 0.96f)) {
            std::cout << "Created default region, but could not write its initial autoslot." << std::endl;
        }
        std::cout << "Created default 3x3 region." << std::endl;
    } else {
        updateLoadingStage("Preparing region", 0.82f);
    }
    finishLoadingStage(false);
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
    ensureRuntime();
    return *runtime_;
}

const SimulationRuntime& GameSession::runtime() const {
    if (!runtime_) {
        throw std::runtime_error("Simulation runtime has not been initialized.");
    }
    return *runtime_;
}

void GameSession::setGameSpeed(GameSpeed gameSpeed) {
    ensureRuntime();
    runtime_->setGameSpeed(gameSpeed);
}

GameSpeed GameSession::gameSpeed() const {
    if (!runtime_) {
        return runtimeOptions_.fastForward ? GameSpeed::FastForward : GameSpeed::Fast;
    }
    return runtime_->gameSpeed();
}

City* GameSession::activeCity() {
    return activeCity_;
}

const City* GameSession::activeCity() const {
    return activeCity_;
}

bool GameSession::activeCityHasUnsavedChanges() const {
    return activeCity_ != 0 &&
        activeCity_->hasSaveState() &&
        activeCity_->isSaveStateDirty();
}

bool GameSession::isLoading() const {
    return loadingStatus_.active;
}

const LoadingStatus& GameSession::loadingStatus() const {
    return loadingStatus_;
}

void GameSession::setLoadingPresenter(const LoadingPresenter& loadingPresenter) {
    loadingPresenter_ = loadingPresenter;
}

void GameSession::clearLoadingPresenter() {
    loadingPresenter_ = LoadingPresenter();
}

void GameSession::setCityPreviewRenderer(const CityPreviewRenderer& cityPreviewRenderer) {
    cityPreviewRenderer_ = cityPreviewRenderer;
}

void GameSession::clearCityPreviewRenderer() {
    cityPreviewRenderer_ = CityPreviewRenderer();
}

void GameSession::setSaveDirectoryOverride(const std::string& saveDirectoryOverride) {
    saveDirectoryOverride_ = saveDirectoryOverride;
}

std::uint64_t GameSession::renderStateRevision() const {
    return renderStateRevision_;
}

void GameSession::queueApplicationWarning(const std::string& title, const std::string& message) {
    applicationWarnings_.push_back(ApplicationWarning(title, message));
    ++renderStateRevision_;
}

bool GameSession::hasApplicationWarning() const {
    return !applicationWarnings_.empty();
}

const ApplicationWarning* GameSession::currentApplicationWarning() const {
    return applicationWarnings_.empty() ? 0 : &applicationWarnings_.front();
}

void GameSession::dismissCurrentApplicationWarning() {
    if (applicationWarnings_.empty()) {
        return;
    }

    applicationWarnings_.erase(applicationWarnings_.begin());
    ++renderStateRevision_;
}

void GameSession::ensureRuntime() {
    if (runtime_) {
        return;
    }

    const bool hadLoadingStage = loadingStatus_.active;
    if (!hadLoadingStage) {
        beginLoadingStage("Loading simulation assets", 0.12f);
    } else {
        updateLoadingStage("Loading simulation assets", std::max(loadingStatus_.progress, 0.12f));
    }

    runtime_.reset(new SimulationRuntime(runtimeOptions_));

    if (!hadLoadingStage && !loadingPresenter_) {
        finishLoadingStage(false);
    } else {
        updateLoadingStage("Simulation assets ready", std::max(loadingStatus_.progress, 0.20f));
    }
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

    if (mode_ == GameMode::Region && activeCity_ != 0 && activeCity_ != city &&
        activeCity_->hasSaveState() && activeCity_->isSaveStateDirty()) {
        std::cout << "Active city has unsaved changes; enterCity needs an explicit save or discard decision." << std::endl;
        return false;
    }

    beginLoadingStage("Loading city", 0.08f);
    ensureRuntime();
    try {
        if (mode_ == GameMode::City) {
            updateLoadingStage("Packing active city", 0.20f);
            runtime_->stop();
            exportActiveCity();
        }

        if (mode_ == GameMode::Region && activeCity_ != 0 && activeCity_ != city) {
            updateLoadingStage("Unloading previous city", 0.30f);
            activeCity_->unloadCleanSaveState();
        }

        updateLoadingStage("Reading city save", 0.46f);
        runtime_->stop();
        CitySaveState saveState = loadCitySaveState(*city);
        updateLoadingStage("Preparing simulation", 0.70f);
        runtime_->importCitySaveState(saveState);
        city->setMetadataFromSaveState(saveState);
        city->unloadCleanSaveState();
        activeCity_ = city;
        mode_ = GameMode::City;
        runtime_->setGameSpeed(GameSpeed::Paused);
        updateLoadingStage("Starting city", 0.92f);
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

    beginLoadingStage("Loading region", 0.08f);
    ensureRuntime();
    try {
        updateLoadingStage("Packing active city", 0.24f);
        runtime_->stop();
        exportActiveCity();
        updateLoadingStage("Preparing region", 0.82f);
        mode_ = GameMode::Region;
        finishLoadingStage(false);
        std::cout << "Returned to region." << std::endl;
    } catch (const std::exception& error) {
        LogException("GameSession::exitToRegion", error);
        finishLoadingStage(false);
        throw;
    } catch (...) {
        LogError("GameSession::exitToRegion", "unknown exception.");
        finishLoadingStage(false);
        throw;
    }
}

bool GameSession::quitCityToRegion(bool saveBeforeQuit) {
    if (mode_ != GameMode::City) {
        return true;
    }

    City* quittingCity = activeCity_;
    activeCity_ = 0;
    mode_ = GameMode::Region;

    beginLoadingStage(saveBeforeQuit ? "Saving city" : "Loading region", 0.08f);
    ensureRuntime();
    try {
        updateLoadingStage(saveBeforeQuit ? "Packing active city" : "Stopping city", 0.20f);
        runtime_->stop();

        if (saveBeforeQuit) {
            if (quittingCity != 0) {
                CitySaveState saveState = runtime_->exportCitySaveState();
                saveState.cameraX = quittingCity->cameraX();
                saveState.cameraY = quittingCity->cameraY();
                saveState.visibleTiles = quittingCity->visibleTiles();
                quittingCity->setSaveState(saveState, true);
                region_.recalculateRegionParameters();
            }

            updateLoadingStage("Writing autoslot", 0.34f);
            if (!saveRegionToDisk(0.36f, 0.82f)) {
                if (quittingCity != 0) {
                    quittingCity->unloadSaveState();
                }
                updateLoadingStage("Unloading city", 0.88f);
                CitySaveState emptyRuntimeState = City::createDefaultSaveState(0u, runtime_->mapWidth(), runtime_->mapHeight());
                runtime_->importCitySaveState(emptyRuntimeState, false);
                finishLoadingStage(true);
                return false;
            }
        } else if (quittingCity != 0) {
            updateLoadingStage("Discarding city changes", 0.42f);
            CitySaveState saveState = loadCitySaveStateFromDiskOrDefault(*quittingCity);
            quittingCity->setMetadataFromSaveState(saveState);
            quittingCity->unloadSaveState();
            quittingCity->clearPreview();
            region_.recalculateRegionParameters();
        }

        updateLoadingStage("Unloading city", 0.88f);
        CitySaveState emptyRuntimeState = City::createDefaultSaveState(0u, runtime_->mapWidth(), runtime_->mapHeight());
        runtime_->importCitySaveState(emptyRuntimeState, false);
        if (quittingCity != 0) {
            quittingCity->unloadCleanSaveState();
        }
        finishLoadingStage(true);
        std::cout << "Quit city to region." << std::endl;
        return true;
    } catch (const std::exception& error) {
        LogException("GameSession::quitCityToRegion", error);
        finishLoadingStage(false);
        throw;
    } catch (...) {
        LogError("GameSession::quitCityToRegion", "unknown exception.");
        finishLoadingStage(false);
        throw;
    }
}

bool GameSession::discardActiveCityChanges() {
    if (activeCity_ == 0) {
        return true;
    }

    beginLoadingStage("Reloading city save", 0.12f);
    ensureRuntime();
    try {
        runtime_->stop();
        updateLoadingStage("Reading city save", 0.45f);
        CitySaveState saveState = loadCitySaveStateFromDiskOrDefault(*activeCity_);
        updateLoadingStage("Restoring city metadata", 0.82f);
        activeCity_->setMetadataFromSaveState(saveState);
        activeCity_->unloadSaveState();
        activeCity_->clearPreview();
        region_.recalculateRegionParameters();
        finishLoadingStage(false);
        std::cout << "Discarded unsaved city changes for " << activeCity_->name() << "." << std::endl;
        return true;
    } catch (const std::exception& error) {
        std::cout << "Could not discard city changes: " << error.what() << std::endl;
        LogException("GameSession::discardActiveCityChanges", error);
    } catch (...) {
        std::cout << "Could not discard city changes: unknown error." << std::endl;
        LogError("GameSession::discardActiveCityChanges", "unknown exception.");
    }

    finishLoadingStage(false);
    return false;
}

bool GameSession::saveAutoslot() {
    beginLoadingStage(mode_ == GameMode::City ? "Saving city" : "Saving region", 0.08f);
    ensureRuntime();
    bool shouldRestartRuntime = false;
    if (mode_ == GameMode::City) {
        CitySaveState restartState;
        bool hasRestartState = false;
        try {
            updateLoadingStage("Packing active city", 0.18f);
            runtime_->stop();
            shouldRestartRuntime = true;
            exportActiveCity();
            if (activeCity_ != 0 && activeCity_->hasSaveState()) {
                restartState = activeCity_->saveState();
                hasRestartState = true;
            }
            updateLoadingStage("Writing autoslot", 0.32f);
            const bool saved = saveRegionToDisk(0.34f, 0.94f);
            if (shouldRestartRuntime) {
                if (hasRestartState) {
                    runtime_->importCitySaveState(restartState);
                }
                runtime_->start();
            }
            finishLoadingStage(false);
            return saved;
        } catch (...) {
            if (shouldRestartRuntime) {
                if (hasRestartState) {
                    runtime_->importCitySaveState(restartState);
                }
                runtime_->start();
            }
            finishLoadingStage(false);
            throw;
        }
    }

    try {
        updateLoadingStage("Writing autoslot", 0.20f);
        const bool saved = saveRegionToDisk(0.22f, 0.94f);
        finishLoadingStage(false);
        return saved;
    } catch (...) {
        if (shouldRestartRuntime) {
            runtime_->start();
        }
        finishLoadingStage(false);
        throw;
    }
}

bool GameSession::loadAutoslot() {
    CrashScope crashScope("GameSession::loadAutoslot");

    beginLoadingStage("Loading autoslot", 0.08f);
    ensureRuntime();
    const bool reloadActiveCity = mode_ == GameMode::City && activeCity_ != 0;
    int activeRegionX = 0;
    int activeRegionY = 0;
    if (reloadActiveCity) {
        activeRegionX = activeCity_->regionX();
        activeRegionY = activeCity_->regionY();
    }

    try {
        updateLoadingStage("Reading region save", 0.22f);
        runtime_->stop();
        activeCity_ = 0;
        mode_ = GameMode::Region;

        const bool loaded = loadRegionFromDisk();
        if (!loaded) {
            updateLoadingStage("Creating region", 0.52f);
            region_.createDefault();
            std::cout << "Load failed; created a new default region." << std::endl;
        }

        bool importedCity = false;
        if (reloadActiveCity) {
            City* city = region_.cityAt(activeRegionX, activeRegionY);
            if (city != 0) {
                updateLoadingStage("Reading city save", 0.56f);
                CitySaveState saveState = loadCitySaveState(*city);
                updateLoadingStage("Preparing simulation", 0.78f);
                runtime_->importCitySaveState(saveState);
                city->setMetadataFromSaveState(saveState);
                city->unloadCleanSaveState();
                activeCity_ = city;
                mode_ = GameMode::City;
                importedCity = true;
                runtime_->setGameSpeed(GameSpeed::Paused);
                updateLoadingStage("Starting city", 0.92f);
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

bool GameSession::saveRegionToDisk(float progressStart, float progressEnd) {
    try {
        ensureSaveDirectory();
        const float progressSpan = std::max(0.0f, progressEnd - progressStart);
        const std::size_t progressCityCount = std::max<std::size_t>(region_.cities().size(), 1u);
        std::size_t cityIndex = 0;
        for (; cityIndex < region_.cities().size(); ++cityIndex) {
            City& city = *region_.cities()[cityIndex];
            const float cityProgress = progressStart + progressSpan * (static_cast<float>(cityIndex) / static_cast<float>(progressCityCount + 1u));
            updateLoadingStage("Saving city data", cityProgress);
            CitySaveState cityState = loadCitySaveState(city);
            if (!saveCityStateToDisk(city, cityState)) {
                return false;
            }
            city.setMetadataFromSaveState(cityState);
            city.markSaveStateClean();
            if (!writeCityPreviewCache(city, cityState)) {
                return false;
            }
            city.unloadCleanSaveState();
        }

        updateLoadingStage("Saving region index", progressStart + progressSpan * (static_cast<float>(progressCityCount) / static_cast<float>(progressCityCount + 1u)));
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

    return loadCitySaveStateFromDiskOrDefault(city);
}

CitySaveState GameSession::loadCitySaveStateFromDiskOrDefault(City& city) {
    std::ifstream stream(citySaveFilePath(city).c_str(), std::ios::in | std::ios::binary);
    if (stream) {
        try {
            std::uint32_t magic = 0u;
            std::uint32_t version = 0u;
            ReadValue(stream, magic);
            ReadValue(stream, version);
            if (magic != kCitySaveMagic || !IsReadableCitySaveVersion(version)) {
                std::cout << "City save version mismatch for " << city.name() << "." << std::endl;
            } else {
                CitySaveState saveState = ReadCityState(stream, version);
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

bool GameSession::writeCityPreviewCache(City& city, const CitySaveState& saveState) const {
    std::vector<std::uint8_t> previewPixels;
    bool renderedPreview = false;
    if (cityPreviewRenderer_) {
        renderedPreview = cityPreviewRenderer_(saveState, previewPixels);
    }

    if (!renderedPreview || previewPixels.size() != ExpectedPreviewPixelCount(city)) {
        if (!renderedPreview) {
            LogWarning("GameSession::writeCityPreviewCache", "City preview renderer was unavailable; writing a fallback preview for " + city.name() + ".");
        } else {
            LogWarning("GameSession::writeCityPreviewCache", "City preview renderer returned an invalid pixel buffer for " + city.name() + "; writing a fallback preview.");
        }
        BuildFallbackCityPreviewPixels(city, previewPixels);
    }

    if (!saveCityPreviewPixels(city, previewPixels)) {
        std::cout << "Could not save mandatory region preview cache for " << city.name() << "." << std::endl;
        return false;
    }

    return true;
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
                if (magic == kCitySaveMagic && IsReadableCitySaveVersion(version)) {
                    return ReadCityState(stream, version);
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

bool GameSession::loadCityPreviewPixels(const City& city, std::vector<std::uint8_t>& previewPixels) const {
    std::ifstream stream(cityPreviewFilePath(city).c_str(), std::ios::in | std::ios::binary);
    if (!stream) {
        return false;
    }

    try {
        std::uint32_t magic = 0u;
        std::uint32_t version = 0u;
        ReadValue(stream, magic);
        ReadValue(stream, version);
        if (magic != kCityPreviewSaveMagic || version != kCityPreviewSaveVersion) {
            return false;
        }

        int regionX = 0;
        int regionY = 0;
        int cityWidth = 0;
        int cityHeight = 0;
        int previewWidth = 0;
        int previewHeight = 0;
        ReadValue(stream, regionX);
        ReadValue(stream, regionY);
        ReadValue(stream, cityWidth);
        ReadValue(stream, cityHeight);
        ReadValue(stream, previewWidth);
        ReadValue(stream, previewHeight);
        if (regionX != city.regionX() ||
            regionY != city.regionY() ||
            cityWidth != city.width() ||
            cityHeight != city.height() ||
            previewWidth != city.previewWidth() ||
            previewHeight != city.previewHeight()) {
            return false;
        }

        std::uint8_t cachedHasSourceFile = 0u;
        SaveFileStamp cachedStamp;
        ReadValue(stream, cachedHasSourceFile);
        cachedStamp.exists = cachedHasSourceFile != 0u;
        ReadValue(stream, cachedStamp.byteSize);
        ReadValue(stream, cachedStamp.lastWriteLow);
        ReadValue(stream, cachedStamp.lastWriteHigh);
        const SaveFileStamp currentStamp = GetSaveFileStamp(citySaveFilePath(city));
        if (!SaveFileStampsMatch(cachedStamp, currentStamp)) {
            return false;
        }

        std::uint32_t pixelByteCount = 0u;
        ReadValue(stream, pixelByteCount);
        const std::size_t expectedPixelByteCount = ExpectedPreviewPixelCount(city);
        if (static_cast<std::size_t>(pixelByteCount) != expectedPixelByteCount) {
            return false;
        }

        previewPixels.resize(expectedPixelByteCount);
        if (!previewPixels.empty()) {
            stream.read(reinterpret_cast<char*>(&previewPixels[0]), static_cast<std::streamsize>(previewPixels.size()));
            if (!stream) {
                return false;
            }
        }
        return true;
    } catch (const std::exception& error) {
        std::cout << "Could not load region preview cache for " << city.name() << ": " << error.what() << std::endl;
        LogException("GameSession::loadCityPreviewPixels", error);
    }

    return false;
}

bool GameSession::saveCityPreviewPixels(const City& city, const std::vector<std::uint8_t>& previewPixels) const {
    if (city.hasSaveState() && city.isSaveStateDirty()) {
        return false;
    }

    const std::size_t expectedPixelByteCount = ExpectedPreviewPixelCount(city);
    if (previewPixels.size() != expectedPixelByteCount ||
        expectedPixelByteCount > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }

    const SaveFileStamp currentStamp = GetSaveFileStamp(citySaveFilePath(city));
    if (city.hasSaveState() && !currentStamp.exists) {
        return false;
    }

    try {
        ensureSaveDirectory();
        std::ofstream stream(cityPreviewFilePath(city).c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!stream) {
            return false;
        }

        WriteValue(stream, kCityPreviewSaveMagic);
        WriteValue(stream, kCityPreviewSaveVersion);
        WriteValue(stream, city.regionX());
        WriteValue(stream, city.regionY());
        WriteValue(stream, city.width());
        WriteValue(stream, city.height());
        WriteValue(stream, city.previewWidth());
        WriteValue(stream, city.previewHeight());
        const std::uint8_t hasSourceFile = currentStamp.exists ? 1u : 0u;
        WriteValue(stream, hasSourceFile);
        WriteValue(stream, currentStamp.byteSize);
        WriteValue(stream, currentStamp.lastWriteLow);
        WriteValue(stream, currentStamp.lastWriteHigh);
        const std::uint32_t pixelByteCount = static_cast<std::uint32_t>(previewPixels.size());
        WriteValue(stream, pixelByteCount);
        if (!previewPixels.empty()) {
            stream.write(reinterpret_cast<const char*>(&previewPixels[0]), static_cast<std::streamsize>(previewPixels.size()));
            if (!stream) {
                throw std::runtime_error("Failed to write region preview cache pixels.");
            }
        }
        return true;
    } catch (const std::exception& error) {
        std::cout << "Could not save region preview cache for " << city.name() << ": " << error.what() << std::endl;
        LogException("GameSession::saveCityPreviewPixels", error);
    }

    return false;
}

void GameSession::exportActiveCity() {
    if (activeCity_ == 0) {
        return;
    }
    ensureRuntime();

    CitySaveState saveState = runtime_->exportCitySaveState();
    saveState.cameraX = activeCity_->cameraX();
    saveState.cameraY = activeCity_->cameraY();
    saveState.visibleTiles = activeCity_->visibleTiles();
    activeCity_->setSaveState(saveState, true);
    region_.recalculateRegionParameters();
}

void GameSession::beginLoadingStage(const std::string& label, float progress) {
    loadingStatus_.active = true;
    loadingStatus_.label = label;
    loadingStatus_.progress = std::max(0.0f, std::min(progress, 1.0f));
    presentLoadingStage();
}

void GameSession::updateLoadingStage(const std::string& label, float progress) {
    if (!loadingStatus_.active) {
        return;
    }

    loadingStatus_.label = label;
    loadingStatus_.progress = std::max(0.0f, std::min(progress, 1.0f));
    presentLoadingStage();
}

void GameSession::finishLoadingStage(bool invalidatesRenderState) {
    if (loadingStatus_.active) {
        loadingStatus_.progress = 1.0f;
        presentLoadingStage();
    }

    if (invalidatesRenderState) {
        ++renderStateRevision_;
        if (renderStateRevision_ == 0) {
            renderStateRevision_ = 1;
        }
    }
    loadingStatus_.active = false;
}

void GameSession::presentLoadingStage() const {
    if (loadingPresenter_) {
        loadingPresenter_(loadingStatus_);
    }
}

std::string GameSession::saveDirectory() const {
    if (!saveDirectoryOverride_.empty()) {
        return saveDirectoryOverride_;
    }

    return RuntimeDataPath("Saves");
}

std::string GameSession::saveFilePath() const {
    return saveDirectory() + "\\region.bin";
}

std::string GameSession::citySaveFilePath(const City& city) const {
    std::ostringstream fileName;
    fileName << saveDirectory() << "\\city_" << city.regionX() << "_" << city.regionY() << ".bin";
    return fileName.str();
}

std::string GameSession::cityPreviewFilePath(const City& city) const {
    std::ostringstream fileName;
    fileName << saveDirectory() << "\\city_" << city.regionX() << "_" << city.regionY() << ".preview.bin";
    return fileName.str();
}

void GameSession::ensureSaveDirectory() const {
    if (!saveDirectoryOverride_.empty()) {
        EnsureDirectoryExists(saveDirectoryOverride_);
        return;
    }

    const std::string dataDirectory = RuntimeDataDirectory();
    EnsureDirectoryExists(dataDirectory);
    EnsureDirectoryExists(saveDirectory());
}
