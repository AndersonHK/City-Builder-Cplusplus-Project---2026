#include "SimulationRuntime.h"

#include "AssetLoader.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iostream>
#include <random>
#include <stdexcept>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {
// Converts a steady-clock span to microseconds for lightweight profiling.
long long DurationMicros(const std::chrono::steady_clock::time_point& startTime, const std::chrono::steady_clock::time_point& endTime) {
    return std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
}

// Finds the executable directory so data assets can be loaded beside the binary.
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

int NormalizeRotationSteps(int rotationSteps) {
    return ((rotationSteps % 4) + 4) % 4;
}

Int2 RotateLocalTile(const Int2& localTile, int rotationSteps) {
    switch (NormalizeRotationSteps(rotationSteps)) {
        case 1:
            return Int2(-localTile.y, localTile.x);
        case 2:
            return Int2(-localTile.x, -localTile.y);
        case 3:
            return Int2(localTile.y, -localTile.x);
        default:
            return localTile;
    }
}

std::uint8_t RotateRoadDirection(std::uint8_t roadDirection, int rotationSteps) {
    std::uint8_t direction = roadDirection;
    int step = 0;
    for (; step < NormalizeRotationSteps(rotationSteps); ++step) {
        if (direction == kRoadDirectionNorth) {
            direction = kRoadDirectionEast;
        } else if (direction == kRoadDirectionEast) {
            direction = kRoadDirectionSouth;
        } else if (direction == kRoadDirectionSouth) {
            direction = kRoadDirectionWest;
        } else if (direction == kRoadDirectionWest) {
            direction = kRoadDirectionNorth;
        }
    }

    return direction;
}

Int2 RotatedRectangleMinimum(const Int2& localOrigin, int width, int height, int rotationSteps) {
    Int2 minimum(0, 0);
    bool hasTile = false;

    int tileY = 0;
    for (; tileY < height; ++tileY) {
        int tileX = 0;
        for (; tileX < width; ++tileX) {
            const Int2 rotatedTile = RotateLocalTile(Int2(localOrigin.x + tileX, localOrigin.y + tileY), rotationSteps);
            if (!hasTile) {
                minimum = rotatedTile;
                hasTile = true;
            } else {
                minimum.x = std::min(minimum.x, rotatedTile.x);
                minimum.y = std::min(minimum.y, rotatedTile.y);
            }
        }
    }

    return minimum;
}

void RotatedRectangleBounds(const Int2& localOrigin, int width, int height, int rotationSteps, Int2& minimum, Int2& maximum) {
    bool hasTile = false;

    int tileY = 0;
    for (; tileY < height; ++tileY) {
        int tileX = 0;
        for (; tileX < width; ++tileX) {
            const Int2 rotatedTile = RotateLocalTile(Int2(localOrigin.x + tileX, localOrigin.y + tileY), rotationSteps);
            if (!hasTile) {
                minimum = rotatedTile;
                maximum = rotatedTile;
                hasTile = true;
            } else {
                minimum.x = std::min(minimum.x, rotatedTile.x);
                minimum.y = std::min(minimum.y, rotatedTile.y);
                maximum.x = std::max(maximum.x, rotatedTile.x);
                maximum.y = std::max(maximum.y, rotatedTile.y);
            }
        }
    }

    if (!hasTile) {
        minimum = Int2(0, 0);
        maximum = Int2(0, 0);
    }
}
}

// Allocates the triple-buffered world and derives chunk/work scheduling config.
SimulationRuntime::SimulationRuntime(const RuntimeOptions& runtimeOptions)
    : simulationReadBufferIndex_(0),
      simulationWriteBufferIndex_(1),
      publishedBufferIndex_(0),
      publishedGeneration_(0),
      nextLotId_(1),
      lotsRevision_(0),
      commuteRevision_(0),
      commutesDirty_(true),
      keepRunning_(false),
      nextChunkIndex_(0),
      workersRemaining_(0),
      chunkTaskReady_(false),
      stopWorkerThreads_(false),
      chunkTaskGeneration_(0),
      currentWorkerTask_(WorkerTaskType::None),
      currentReadTiles_(0),
      currentWriteTiles_(0),
      updatesPerSecond_(0),
      neighborPassMicros_(0),
      commandPassMicros_(0),
      lotEffectsMicros_(0),
      localPassMicros_(0),
      publishMicros_(0),
      writeBufferWaitMicros_(0) {
    runtimeOptions_ = runtimeOptions;

    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    const int workerThreadCount = hardwareThreads > 2u ? static_cast<int>(hardwareThreads) - 2 : 1;
    chunkConfig_ = CalculateChunkConfig(kMapWidth, kMapHeight, sizeof(Tile), workerThreadCount, runtimeOptions_.manualL2BytesPerLogicalThread, runtimeOptions_.detectL2CacheSize, runtimeOptions_.usableL2Fraction, kMinimumJobsPerWorkerMultiplier);
    chunkLayout_ = BuildChunkLayout(kMapWidth, kMapHeight, chunkConfig_.chunkWidth, chunkConfig_.chunkHeight);
    transportNetwork_.initialize(kMapWidth, kMapHeight, chunkLayout_);

    loadAssets();

    tileBuffers_.resize(3);
    const std::size_t totalTileCount = static_cast<std::size_t>(kMapWidth) * static_cast<std::size_t>(kMapHeight);
    const std::size_t chunkCount = chunkLayout_.size();
    lotOccupancy_.assign(totalTileCount, kInvalidLotId);
    oldCityParameters_.assign(cityParameterRegistry_.count(), 0.0f);
    nextCityParameters_.assign(cityParameterRegistry_.count(), 0.0f);
    cityParameterDeltaBuffers_.assign(static_cast<std::size_t>(chunkConfig_.workerThreadCount + 1), std::vector<float>(cityParameterRegistry_.count(), 0.0f));

    int bufferIndex = 0;
    for (; bufferIndex < 3; ++bufferIndex) {
        tileBuffers_[bufferIndex].tiles.resize(totalTileCount);
        tileBuffers_[bufferIndex].chunkRevisions.assign(chunkCount, 1);
        tileBuffers_[bufferIndex].publishedLots.clear();
        tileBuffers_[bufferIndex].publishedLotInfos.clear();
        tileBuffers_[bufferIndex].publishedLotOccupancy.assign(totalTileCount, kInvalidLotId);
        tileBuffers_[bufferIndex].publishedRoads.assign(totalTileCount * TransportNetwork::layerCount(), ResolvedRoadCell());
        tileBuffers_[bufferIndex].publishedGroundRoadRenderState.assign(totalTileCount * kGroundRoadRenderChannelsPerTile, 0);
        tileBuffers_[bufferIndex].publishedTileOverlayState.assign(totalTileCount * 4u, 0);
        tileBuffers_[bufferIndex].publishedGroundRoadChunkRevisions.assign(chunkCount, 1);
        tileBuffers_[bufferIndex].publishedElevatedRoadChunkRevisions.assign(chunkCount, 1);
        tileBuffers_[bufferIndex].publishedTileOverlayChunkRevisions.assign(chunkCount, 1);
        tileBuffers_[bufferIndex].lotRenderRevision = 0;
        tileBuffers_[bufferIndex].roadRenderRevision = 0;
        tileBuffers_[bufferIndex].overlayRenderRevision = 0;
        tileBuffers_[bufferIndex].commuteRenderRevision = 0;
        bufferUseCounts_[bufferIndex].store(0);
    }

    lastRenderedGeneration_.store(0);
    initializeWorld();

    std::cout << "Loaded module assets: " << moduleAssets_.size() << std::endl;
    std::cout << "Loaded lot assets: " << lotAssets_.size() << std::endl;
    std::cout << "L2 bytes per logical thread: " << chunkConfig_.chosenL2BytesPerLogicalThread << std::endl;
    std::cout << "Detected L2 bytes per logical thread: " << chunkConfig_.detectedL2BytesPerLogicalThread << std::endl;
    std::cout << "Manual L2 override bytes per logical thread: " << chunkConfig_.manualOverrideBytesPerLogicalThread << std::endl;
    std::cout << "Usable cache fraction: " << chunkConfig_.usableCacheFraction << std::endl;
    std::cout << "Usable cache bytes per chunk: " << chunkConfig_.usableCacheBytesPerChunk << std::endl;
    std::cout << "Chunk size: " << chunkConfig_.chunkWidth << "x" << chunkConfig_.chunkHeight << std::endl;
    std::cout << "Estimated chunk working set bytes: " << chunkConfig_.chunkWorkingSetBytes << std::endl;
    std::cout << "Worker threads: " << chunkConfig_.workerThreadCount << std::endl;
    std::cout << "Chunk count: " << chunkConfig_.chunkCount << std::endl;
}

// Stops worker activity before runtime storage is destroyed.
SimulationRuntime::~SimulationRuntime() {
    stop();
}

// Starts the simulation thread and worker pool.
void SimulationRuntime::start() {
    if (keepRunning_.exchange(true)) {
        return;
    }

    startWorkers();
    simulationThread_ = std::thread(&SimulationRuntime::simulationLoop, this);
}

// Requests shutdown and joins all simulation-owned threads.
void SimulationRuntime::stop() {
    if (!keepRunning_.exchange(false)) {
        return;
    }

    {
        std::lock_guard<std::mutex> renderLock(renderMutex_);
        renderCv_.notify_all();
    }

    {
        std::lock_guard<std::mutex> workerLock(workerMutex_);
        workerCv_.notify_all();
        workerFinishedCv_.notify_all();
    }

    if (simulationThread_.joinable()) {
        simulationThread_.join();
    }

    stopWorkers();
}

// Queues a pollution brush write for the next command boundary.
void SimulationRuntime::queuePaintPollution(int tileX, int tileY, int amount) {
    if (!isTileInsideMap(tileX, tileY)) {
        return;
    }

    PlayerCommand playerCommand;
    playerCommand.type = PlayerCommandType::PaintPollution;
    playerCommand.tileX = tileX;
    playerCommand.tileY = tileY;
    playerCommand.amount = amount;
    enqueueCommand(playerCommand);
}

// Queues a lot placement by archetype id.
void SimulationRuntime::queuePlaceLot(const std::string& lotAssetId, int tileX, int tileY, int rotationSteps) {
    if (!isTileInsideMap(tileX, tileY)) {
        return;
    }

    PlayerCommand playerCommand;
    playerCommand.type = PlayerCommandType::PlaceLot;
    playerCommand.tileX = tileX;
    playerCommand.tileY = tileY;
    playerCommand.rotationSteps = NormalizeRotationSteps(rotationSteps);
    playerCommand.assetId = lotAssetId;
    enqueueCommand(playerCommand);
}

// Queues a module expansion against the lot adjacent to a clicked tile.
void SimulationRuntime::queueAddModuleAtTile(const std::string& moduleAssetId, int tileX, int tileY) {
    if (!isTileInsideMap(tileX, tileY)) {
        return;
    }

    PlayerCommand playerCommand;
    playerCommand.type = PlayerCommandType::AddModuleAtTile;
    playerCommand.tileX = tileX;
    playerCommand.tileY = tileY;
    playerCommand.assetId = moduleAssetId;
    enqueueCommand(playerCommand);
}

// Queues removal of the module under a clicked occupied tile.
void SimulationRuntime::queueRemoveModuleAtTile(int tileX, int tileY) {
    if (!isTileInsideMap(tileX, tileY)) {
        return;
    }

    PlayerCommand playerCommand;
    playerCommand.type = PlayerCommandType::RemoveModuleAtTile;
    playerCommand.tileX = tileX;
    playerCommand.tileY = tileY;
    enqueueCommand(playerCommand);
}

// Queues a multi-tile road stroke for transport-layer application.
void SimulationRuntime::queuePlaceRoadStroke(const RoadStrokeCommand& roadStrokeCommand) {
    if (!isTileInsideMap(roadStrokeCommand.startTile.x, roadStrokeCommand.startTile.y) ||
        !isTileInsideMap(roadStrokeCommand.cornerTile.x, roadStrokeCommand.cornerTile.y) ||
        !isTileInsideMap(roadStrokeCommand.endTile.x, roadStrokeCommand.endTile.y)) {
        return;
    }

    PlayerCommand playerCommand;
    playerCommand.type = PlayerCommandType::PlaceRoadStroke;
    playerCommand.tileX = roadStrokeCommand.endTile.x;
    playerCommand.tileY = roadStrokeCommand.endTile.y;
    playerCommand.roadStroke = roadStrokeCommand;
    enqueueCommand(playerCommand);
}

// Queues the default smokestack lot archetype.
void SimulationRuntime::queuePlaceSmokestack(int tileX, int tileY, int rotationSteps) {
    queuePlaceLot("smokestack_lot", tileX, tileY, rotationSteps);
}

// Queues the default park lot archetype.
void SimulationRuntime::queuePlacePark(int tileX, int tileY, int rotationSteps) {
    queuePlaceLot("park_lot", tileX, tileY, rotationSteps);
}

// Queues the default factory lot archetype.
void SimulationRuntime::queuePlaceFactory(int tileX, int tileY, int rotationSteps) {
    queuePlaceLot("factory_lot", tileX, tileY, rotationSteps);
}

// Queues the default house lot archetype.
void SimulationRuntime::queuePlaceHouse(int tileX, int tileY, int rotationSteps) {
    queuePlaceLot("house_lot", tileX, tileY, rotationSteps);
}

// Queues a smokestack module expansion.
void SimulationRuntime::queueAddSmokestackModule(int tileX, int tileY) {
    queueAddModuleAtTile("smokestack_module", tileX, tileY);
}

// Queues a park module expansion.
void SimulationRuntime::queueAddParkModule(int tileX, int tileY) {
    queueAddModuleAtTile("park_module", tileX, tileY);
}

// Queues a ground local-street stroke.
void SimulationRuntime::queuePlaceStreetRoad(const Int2& startTile, const Int2& cornerTile, const Int2& endTile) {
    RoadStrokeCommand roadStrokeCommand;
    roadStrokeCommand.startTile = startTile;
    roadStrokeCommand.cornerTile = cornerTile;
    roadStrokeCommand.endTile = endTile;
    roadStrokeCommand.family = RoadFamily::LocalStreet;
    roadStrokeCommand.layer = TransportLayerId::Ground;
    roadStrokeCommand.roadTemplate = TransportNetwork::makeRoadTemplate(roadStrokeCommand.family, roadStrokeCommand.layer, 1, RoadTrafficSide::RightHand, RoadDirectionMode::TwoWay);
    queuePlaceRoadStroke(roadStrokeCommand);
}

// Queues an elevated highway stroke.
void SimulationRuntime::queuePlaceHighwayRoad(const Int2& startTile, const Int2& cornerTile, const Int2& endTile) {
    RoadStrokeCommand roadStrokeCommand;
    roadStrokeCommand.startTile = startTile;
    roadStrokeCommand.cornerTile = cornerTile;
    roadStrokeCommand.endTile = endTile;
    roadStrokeCommand.family = RoadFamily::Highway;
    roadStrokeCommand.layer = TransportLayerId::Elevated;
    roadStrokeCommand.roadTemplate = TransportNetwork::makeRoadTemplate(roadStrokeCommand.family, roadStrokeCommand.layer, 1, RoadTrafficSide::RightHand, RoadDirectionMode::TwoWay);
    queuePlaceRoadStroke(roadStrokeCommand);
}

// Builds a renderer-facing lot preview without mutating simulation state.
bool SimulationRuntime::buildLotPreviewInstance(const std::string& lotAssetId, int tileX, int tileY, int rotationSteps, LotRenderInstance& renderInstance) const {
    const LotAsset* lotAsset = findLotAssetById(lotAssetId);
    if (lotAsset == 0) {
        return false;
    }

    Lot candidateLot;
    if (!buildLotCandidate(*lotAsset, tileX, tileY, rotationSteps, -1, candidateLot)) {
        return false;
    }

    const int minimumX = candidateLot.minimumTileX();
    const int minimumY = candidateLot.minimumTileY();
    const int maximumX = minimumX + candidateLot.footprintWidth() - 1;
    const int maximumY = minimumY + candidateLot.footprintHeight() - 1;
    if (!isTileInsideMap(minimumX, minimumY) || !isTileInsideMap(maximumX, maximumY)) {
        return false;
    }

    renderInstance = candidateLot.buildRenderInstance();
    return true;
}

// Reads the currently published snapshot for debug tile inspection.
TileQueryResult SimulationRuntime::queryTile(int tileX, int tileY) const {
    TileQueryResult queryResult;
    if (!isTileInsideMap(tileX, tileY)) {
        return queryResult;
    }

    std::lock_guard<std::mutex> publishedLock(publishedMutex_);
    const TileBuffer& publishedBuffer = tileBuffers_[publishedBufferIndex_];
    queryResult.isValid = true;
    queryResult.tile = publishedBuffer.tiles[tileIndex(tileX, tileY)];
    queryResult.generation = publishedGeneration_;

    if (!publishedBuffer.publishedLotOccupancy.empty()) {
        const int lotId = publishedBuffer.publishedLotOccupancy[tileIndex(tileX, tileY)];
        if (lotId != kInvalidLotId) {
            queryResult.hasLot = true;
            queryResult.lotId = lotId;

            const PublishedLotInfo* publishedLotInfo = findPublishedLotInfoById(publishedBuffer.publishedLotInfos, lotId);
            if (publishedLotInfo != 0) {
                queryResult.lotAssetId = publishedLotInfo->assetId;
                queryResult.moduleSummary = publishedLotInfo->moduleSummary;
                queryResult.parameterSummary = publishedLotInfo->parameterSummary;
                queryResult.commuteDemand = publishedLotInfo->commuteDemand;
                queryResult.commuteSatisfied = publishedLotInfo->commuteSatisfied;
                queryResult.commuteRouteSegments = publishedLotInfo->commuteRouteSegments;
            }
        }
    }

    if (!publishedBuffer.publishedRoads.empty()) {
        const int tileIndexValue = tileIndex(tileX, tileY);
        std::size_t layerIndex = 0;
        for (; layerIndex < TransportNetwork::layerCount(); ++layerIndex) {
            const std::size_t slot = TransportNetwork::slotIndex(static_cast<TransportLayerId>(layerIndex), tileIndexValue, static_cast<std::size_t>(kMapWidth) * static_cast<std::size_t>(kMapHeight));
            const ResolvedRoadCell& roadCell = publishedBuffer.publishedRoads[slot];
            if (roadCell.family == static_cast<std::uint8_t>(RoadFamily::None)) {
                continue;
            }

            queryResult.roadLayers.push_back(static_cast<TransportLayerId>(layerIndex));
            queryResult.roads.push_back(roadCell);
        }
    }

    return queryResult;
}

CitySaveState SimulationRuntime::exportCitySaveState() const {
    CitySaveState saveState;
    saveState.width = kMapWidth;
    saveState.height = kMapHeight;
    saveState.nextLotId = nextLotId_;
    saveState.tiles = tileBuffers_[simulationReadBufferIndex_].tiles;
    saveState.cityParameters = oldCityParameters_;
    saveState.transport = transportNetwork_.exportSaveState();

    saveState.lots.reserve(lots_.size());
    saveState.previewLots.reserve(lots_.size());
    std::size_t lotIndex = 0;
    for (; lotIndex < lots_.size(); ++lotIndex) {
        const Lot& lot = lots_[lotIndex];
        CitySaveLotState lotSaveState;
        lotSaveState.lotId = lot.id();
        lotSaveState.assetId = lot.assetId();
        lotSaveState.anchorTileX = lot.anchorTileX();
        lotSaveState.anchorTileY = lot.anchorTileY();
        lotSaveState.rotationSteps = lot.rotationSteps();

        const std::vector<LotModulePlacement>& modules = lot.modules();
        std::size_t moduleIndex = 0;
        for (; moduleIndex < modules.size(); ++moduleIndex) {
            if (modules[moduleIndex].module == 0) {
                continue;
            }

            CitySaveLotModuleState moduleSaveState;
            moduleSaveState.moduleAssetId = modules[moduleIndex].module->id;
            moduleSaveState.localOrigin = modules[moduleIndex].localOrigin;
            lotSaveState.modules.push_back(moduleSaveState);
        }

        saveState.lots.push_back(lotSaveState);
        saveState.previewLots.push_back(lot.buildRenderInstance());
    }

    return saveState;
}

void SimulationRuntime::importCitySaveState(const CitySaveState& saveState) {
    const std::size_t totalTileCount = static_cast<std::size_t>(kMapWidth) * static_cast<std::size_t>(kMapHeight);
    if (saveState.width != kMapWidth || saveState.height != kMapHeight || saveState.tiles.size() != totalTileCount) {
        throw std::runtime_error("City save dimensions do not match the current runtime.");
    }

    {
        std::lock_guard<std::mutex> commandLock(commandMutex_);
        pendingCommands_.clear();
    }

    lotOccupancy_.assign(totalTileCount, kInvalidLotId);
    lots_.clear();
    nextLotId_ = std::max(1, saveState.nextLotId);

    std::size_t savedLotIndex = 0;
    for (; savedLotIndex < saveState.lots.size(); ++savedLotIndex) {
        const CitySaveLotState& lotSaveState = saveState.lots[savedLotIndex];
        const LotAsset* lotAsset = findLotAssetById(lotSaveState.assetId);
        if (lotAsset == 0) {
            continue;
        }

        Lot lot(lotSaveState.lotId, lotSaveState.assetId, lotSaveState.anchorTileX, lotSaveState.anchorTileY, lotSaveState.rotationSteps);
        if (lotAsset->footprintWidth > 0 && lotAsset->footprintHeight > 0) {
            Int2 footprintMinimum;
            Int2 footprintMaximum;
            RotatedRectangleBounds(lotAsset->footprintOrigin, lotAsset->footprintWidth, lotAsset->footprintHeight, lotSaveState.rotationSteps, footprintMinimum, footprintMaximum);
            lot.setExplicitFootprint(
                footprintMinimum,
                footprintMaximum.x - footprintMinimum.x + 1,
                footprintMaximum.y - footprintMinimum.y + 1,
                kMapWidth);
        }

        std::size_t moduleIndex = 0;
        for (; moduleIndex < lotSaveState.modules.size(); ++moduleIndex) {
            const LotModule* moduleAsset = findModuleAssetById(lotSaveState.modules[moduleIndex].moduleAssetId);
            if (moduleAsset != 0) {
                lot.addModule(*moduleAsset, lotSaveState.modules[moduleIndex].localOrigin, kMapWidth);
            }
        }

        bool isInsideMap = true;
        const std::vector<int>& occupiedTileIndices = lot.occupiedTileIndices();
        std::size_t occupiedIndex = 0;
        for (; occupiedIndex < occupiedTileIndices.size(); ++occupiedIndex) {
            const int tileLinearIndex = occupiedTileIndices[occupiedIndex];
            if (tileLinearIndex < 0 || tileLinearIndex >= static_cast<int>(totalTileCount)) {
                isInsideMap = false;
                break;
            }
        }

        if (!isInsideMap) {
            continue;
        }

        lots_.push_back(lot);
        setLotOccupancy(lot.id(), lots_.back().occupiedTileIndices());
        nextLotId_ = std::max(nextLotId_, lot.id() + 1);
    }

    oldCityParameters_.assign(cityParameterRegistry_.count(), 0.0f);
    nextCityParameters_.assign(cityParameterRegistry_.count(), 0.0f);
    const std::size_t copiedParameterCount = std::min(oldCityParameters_.size(), saveState.cityParameters.size());
    std::size_t parameterIndex = 0;
    for (; parameterIndex < copiedParameterCount; ++parameterIndex) {
        oldCityParameters_[parameterIndex] = saveState.cityParameters[parameterIndex];
        nextCityParameters_[parameterIndex] = saveState.cityParameters[parameterIndex];
    }

    transportNetwork_.importSaveState(saveState.transport);

    int bufferIndex = 0;
    for (; bufferIndex < 3; ++bufferIndex) {
        tileBuffers_[bufferIndex].tiles = saveState.tiles;
        tileBuffers_[bufferIndex].chunkRevisions.assign(chunkLayout_.size(), 2);
        tileBuffers_[bufferIndex].publishedLots.clear();
        tileBuffers_[bufferIndex].publishedLotInfos.clear();
        tileBuffers_[bufferIndex].publishedLotOccupancy.assign(totalTileCount, kInvalidLotId);
        tileBuffers_[bufferIndex].publishedRoads.assign(totalTileCount * TransportNetwork::layerCount(), ResolvedRoadCell());
        tileBuffers_[bufferIndex].publishedGroundRoadRenderState.assign(totalTileCount * kGroundRoadRenderChannelsPerTile, 0);
        tileBuffers_[bufferIndex].publishedTileOverlayState.assign(totalTileCount * 4u, 0);
        tileBuffers_[bufferIndex].publishedGroundRoadChunkRevisions.assign(chunkLayout_.size(), 1);
        tileBuffers_[bufferIndex].publishedElevatedRoadChunkRevisions.assign(chunkLayout_.size(), 1);
        tileBuffers_[bufferIndex].publishedTileOverlayChunkRevisions.assign(chunkLayout_.size(), 1);
        tileBuffers_[bufferIndex].lotRenderRevision = 0;
        tileBuffers_[bufferIndex].roadRenderRevision = 0;
        tileBuffers_[bufferIndex].overlayRenderRevision = 0;
        tileBuffers_[bufferIndex].commuteRenderRevision = 0;
        bufferUseCounts_[bufferIndex].store(0);
    }

    ++lotsRevision_;
    ++commuteRevision_;
    commutesDirty_ = true;
    simulationReadBufferIndex_ = 0;
    simulationWriteBufferIndex_ = 1;
    refreshPublishedLotSnapshot(tileBuffers_[simulationReadBufferIndex_]);
    refreshPublishedRoadSnapshot(tileBuffers_[simulationReadBufferIndex_]);

    {
        std::lock_guard<std::mutex> publishedLock(publishedMutex_);
        publishedBufferIndex_ = simulationReadBufferIndex_;
        ++publishedGeneration_;
        if (publishedGeneration_ == 0) {
            publishedGeneration_ = 1;
        }
    }

    lastRenderedGeneration_.store(0);
    updatesPerSecond_.store(0);
}

// Pins the current published buffer for renderer-side read-only access.
PublishedWorldSnapshot SimulationRuntime::acquirePublishedSnapshot() {
    PublishedWorldSnapshot snapshot;

    std::lock_guard<std::mutex> publishedLock(publishedMutex_);
    snapshot.bufferIndex = publishedBufferIndex_;
    snapshot.tiles = &tileBuffers_[publishedBufferIndex_].tiles;
    snapshot.lots = &tileBuffers_[publishedBufferIndex_].publishedLots;
    snapshot.chunkRevisions = &tileBuffers_[publishedBufferIndex_].chunkRevisions;
    snapshot.lotOccupancy = &tileBuffers_[publishedBufferIndex_].publishedLotOccupancy;
    snapshot.roads = &tileBuffers_[publishedBufferIndex_].publishedRoads;
    snapshot.groundRoadRenderState = &tileBuffers_[publishedBufferIndex_].publishedGroundRoadRenderState;
    snapshot.tileOverlayState = &tileBuffers_[publishedBufferIndex_].publishedTileOverlayState;
    snapshot.groundRoadChunkRevisions = &tileBuffers_[publishedBufferIndex_].publishedGroundRoadChunkRevisions;
    snapshot.elevatedRoadChunkRevisions = &tileBuffers_[publishedBufferIndex_].publishedElevatedRoadChunkRevisions;
    snapshot.tileOverlayChunkRevisions = &tileBuffers_[publishedBufferIndex_].publishedTileOverlayChunkRevisions;
    snapshot.width = kMapWidth;
    snapshot.height = kMapHeight;
    snapshot.generation = publishedGeneration_;
    snapshot.lotRevision = tileBuffers_[publishedBufferIndex_].lotRenderRevision;
    snapshot.roadRevision = tileBuffers_[publishedBufferIndex_].roadRenderRevision;
    snapshot.overlayRevision = tileBuffers_[publishedBufferIndex_].overlayRenderRevision;

    if (snapshot.bufferIndex >= 0) {
        bufferUseCounts_[snapshot.bufferIndex].fetch_add(1);
    }

    return snapshot;
}

// Releases a pinned published buffer and advances render-consumption tracking.
void SimulationRuntime::releasePublishedSnapshot(const PublishedWorldSnapshot& snapshot) {
    if (snapshot.bufferIndex < 0 || snapshot.bufferIndex >= 3) {
        return;
    }

    bufferUseCounts_[snapshot.bufferIndex].fetch_sub(1);

    const std::uint64_t currentRenderedGeneration = lastRenderedGeneration_.load();
    if (snapshot.generation > currentRenderedGeneration) {
        lastRenderedGeneration_.store(snapshot.generation);
    }

    std::lock_guard<std::mutex> renderLock(renderMutex_);
    renderCv_.notify_all();
}

// Returns the fixed map width for this milestone.
int SimulationRuntime::mapWidth() const {
    return kMapWidth;
}

// Returns the fixed map height for this milestone.
int SimulationRuntime::mapHeight() const {
    return kMapHeight;
}

// Returns the most recent simulation throughput counter.
int SimulationRuntime::updatesPerSecond() const {
    return updatesPerSecond_.load();
}

// Exposes the derived chunk sizing decisions for diagnostics.
const ChunkConfig& SimulationRuntime::chunkConfig() const {
    return chunkConfig_;
}

// Exposes the shared chunk layout used by simulation, transport, and rendering.
const std::vector<ChunkRect>& SimulationRuntime::chunkLayout() const {
    return chunkLayout_;
}

// Copies timing counters for the renderer's status print.
RuntimeTimingSnapshot SimulationRuntime::timingSnapshot() const {
    RuntimeTimingSnapshot snapshot;
    snapshot.neighborPassMicros = neighborPassMicros_.load();
    snapshot.commandPassMicros = commandPassMicros_.load();
    snapshot.lotEffectsMicros = lotEffectsMicros_.load();
    snapshot.localPassMicros = localPassMicros_.load();
    snapshot.publishMicros = publishMicros_.load();
    snapshot.writeBufferWaitMicros = writeBufferWaitMicros_.load();
    return snapshot;
}

// Loads XML lot/module archetypes from the executable data directory.
void SimulationRuntime::loadAssets() {
    LoadedGameAssets loadedAssets;
    std::string errorMessage;
    if (!LoadGameAssets(GetExecutableDirectory() + "\\Data", cityParameterRegistry_, loadedAssets, errorMessage)) {
        throw std::runtime_error(errorMessage);
    }

    moduleAssets_ = loadedAssets.modules;
    lotAssets_ = loadedAssets.lots;
    transportNetwork_.setCongestionCurve(loadedAssets.congestionCurve);

    moduleAssetIndexById_.clear();
    std::size_t moduleIndex = 0;
    for (; moduleIndex < moduleAssets_.size(); ++moduleIndex) {
        moduleAssetIndexById_[moduleAssets_[moduleIndex].id] = moduleIndex;
    }

    lotAssetIndexById_.clear();
    std::size_t lotIndex = 0;
    for (; lotIndex < lotAssets_.size(); ++lotIndex) {
        lotAssetIndexById_[lotAssets_[lotIndex].id] = lotIndex;
    }
}

// Seeds the three tile buffers and the initial published snapshot.
void SimulationRuntime::initializeWorld() {
    std::mt19937 randomEngine(static_cast<unsigned int>(std::time(0)));
    std::uniform_int_distribution<int> baseDistribution(0, 327670);

    std::vector<Tile>& bootstrapBuffer = tileBuffers_[simulationReadBufferIndex_].tiles;
    int tileY = 0;
    for (; tileY < kMapHeight; ++tileY) {
        int tileX = 0;
        for (; tileX < kMapWidth; ++tileX) {
            Tile& tile = bootstrapBuffer[tileIndex(tileX, tileY)];
            tile.landValue = baseDistribution(randomEngine);
            tile.airPollution = baseDistribution(randomEngine);
            tile.isVacant = true;
            tile.zoningType = 0;
        }
    }

    tileBuffers_[1].tiles = bootstrapBuffer;
    tileBuffers_[2].tiles = bootstrapBuffer;

    int bufferIndex = 0;
    for (; bufferIndex < 3; ++bufferIndex) {
        tileBuffers_[bufferIndex].chunkRevisions.assign(chunkLayout_.size(), 1);
        tileBuffers_[bufferIndex].publishedLots.clear();
        tileBuffers_[bufferIndex].publishedLotInfos.clear();
        tileBuffers_[bufferIndex].publishedLotOccupancy.assign(static_cast<std::size_t>(kMapWidth) * static_cast<std::size_t>(kMapHeight), kInvalidLotId);
        tileBuffers_[bufferIndex].publishedRoads.assign(static_cast<std::size_t>(kMapWidth) * static_cast<std::size_t>(kMapHeight) * TransportNetwork::layerCount(), ResolvedRoadCell());
        tileBuffers_[bufferIndex].publishedGroundRoadRenderState.assign(static_cast<std::size_t>(kMapWidth) * static_cast<std::size_t>(kMapHeight) * kGroundRoadRenderChannelsPerTile, 0);
        tileBuffers_[bufferIndex].publishedTileOverlayState.assign(static_cast<std::size_t>(kMapWidth) * static_cast<std::size_t>(kMapHeight) * 4u, 0);
        tileBuffers_[bufferIndex].publishedGroundRoadChunkRevisions.assign(chunkLayout_.size(), 1);
        tileBuffers_[bufferIndex].publishedElevatedRoadChunkRevisions.assign(chunkLayout_.size(), 1);
        tileBuffers_[bufferIndex].publishedTileOverlayChunkRevisions.assign(chunkLayout_.size(), 1);
        tileBuffers_[bufferIndex].lotRenderRevision = 0;
        tileBuffers_[bufferIndex].roadRenderRevision = 0;
        tileBuffers_[bufferIndex].overlayRenderRevision = 0;
        tileBuffers_[bufferIndex].commuteRenderRevision = 0;
    }

    lotOccupancy_.assign(static_cast<std::size_t>(kMapWidth) * static_cast<std::size_t>(kMapHeight), kInvalidLotId);
    lots_.clear();
    nextLotId_ = 1;
    lotsRevision_ = 0;
    commuteRevision_ = 0;
    commutesDirty_ = true;
    oldCityParameters_.assign(cityParameterRegistry_.count(), 0.0f);
    nextCityParameters_.assign(cityParameterRegistry_.count(), 0.0f);
    transportNetwork_.clear();

    std::lock_guard<std::mutex> publishedLock(publishedMutex_);
    publishedBufferIndex_ = simulationReadBufferIndex_;
    publishedGeneration_ = 0;
}

// Runs the ordered simulation passes and publishes completed write buffers.
void SimulationRuntime::simulationLoop() {
    int updatesThisSecond = 0;
    std::chrono::steady_clock::time_point secondWindowStart = std::chrono::steady_clock::now();

    while (keepRunning_.load()) {
        copyChunkRevisionsForWriteBuffer();

        const std::chrono::steady_clock::time_point neighborStart = std::chrono::steady_clock::now();
        runNeighborPass(tileBuffers_[simulationReadBufferIndex_].tiles, tileBuffers_[simulationWriteBufferIndex_].tiles);
        neighborPassMicros_.store(DurationMicros(neighborStart, std::chrono::steady_clock::now()));

        const std::chrono::steady_clock::time_point commandStart = std::chrono::steady_clock::now();
        applyQueuedCommands(tileBuffers_[simulationWriteBufferIndex_]);
        commandPassMicros_.store(DurationMicros(commandStart, std::chrono::steady_clock::now()));

        const std::chrono::steady_clock::time_point lotEffectsStart = std::chrono::steady_clock::now();
        applyLotEffects(tileBuffers_[simulationWriteBufferIndex_].tiles);
        runCommuteAssignment();
        lotEffectsMicros_.store(DurationMicros(lotEffectsStart, std::chrono::steady_clock::now()));

        const std::chrono::steady_clock::time_point localStart = std::chrono::steady_clock::now();
        runLocalTilePass(tileBuffers_[simulationWriteBufferIndex_].tiles);
        localPassMicros_.store(DurationMicros(localStart, std::chrono::steady_clock::now()));

        const std::chrono::steady_clock::time_point publishStart = std::chrono::steady_clock::now();
        publishCompletedBuffer();
        publishMicros_.store(DurationMicros(publishStart, std::chrono::steady_clock::now()));

        ++updatesThisSecond;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - secondWindowStart).count() >= 1) {
            updatesPerSecond_.store(updatesThisSecond);
            updatesThisSecond = 0;
            secondWindowStart = now;
        }

        if (!runtimeOptions_.fastForward) {
            std::unique_lock<std::mutex> renderLock(renderMutex_);
            const std::uint64_t generationToWaitFor = publishedGeneration_;
            renderCv_.wait(renderLock, [this, generationToWaitFor]() {
                return !keepRunning_.load() || lastRenderedGeneration_.load() >= generationToWaitFor;
            });
        }
    }
}

// Starts the chunk worker threads used by parallel tile passes.
void SimulationRuntime::startWorkers() {
    stopWorkerThreads_ = false;
    workerThreads_.clear();

    int workerIndex = 0;
    for (; workerIndex < chunkConfig_.workerThreadCount; ++workerIndex) {
        workerThreads_.push_back(std::thread(&SimulationRuntime::workerMain, this));
    }
}

// Wakes and joins chunk workers during shutdown.
void SimulationRuntime::stopWorkers() {
    {
        std::lock_guard<std::mutex> workerLock(workerMutex_);
        stopWorkerThreads_ = true;
        chunkTaskReady_ = true;
    }

    workerCv_.notify_all();
    workerFinishedCv_.notify_all();

    std::size_t workerIndex = 0;
    for (; workerIndex < workerThreads_.size(); ++workerIndex) {
        if (workerThreads_[workerIndex].joinable()) {
            workerThreads_[workerIndex].join();
        }
    }

    workerThreads_.clear();
    stopWorkerThreads_ = false;
    chunkTaskReady_ = false;
    currentWorkerTask_ = WorkerTaskType::None;
}

// Waits for chunk-task generations and participates until shutdown.
void SimulationRuntime::workerMain() {
    std::uint64_t observedGeneration = 0;

    for (;;) {
        {
            std::unique_lock<std::mutex> workerLock(workerMutex_);
            workerCv_.wait(workerLock, [this, observedGeneration]() {
                return stopWorkerThreads_ || (chunkTaskReady_ && chunkTaskGeneration_ != observedGeneration);
            });

            if (stopWorkerThreads_) {
                return;
            }

            observedGeneration = chunkTaskGeneration_;
        }

        runPendingChunkTasks();
        completeChunkTaskParticipant();
    }
}

// Dispatches one chunked tile pass across workers and the simulation thread.
void SimulationRuntime::parallelForEachChunk(WorkerTaskType workerTask, const std::vector<Tile>* readTiles, std::vector<Tile>* writeTiles) {
    if (chunkLayout_.empty()) {
        return;
    }

    if (workerThreads_.empty()) {
        currentWorkerTask_ = workerTask;
        currentReadTiles_ = readTiles;
        currentWriteTiles_ = writeTiles;
        nextChunkIndex_.store(0);
        runPendingChunkTasks();
        currentWorkerTask_ = WorkerTaskType::None;
        return;
    }

    {
        std::lock_guard<std::mutex> workerLock(workerMutex_);
        currentWorkerTask_ = workerTask;
        currentReadTiles_ = readTiles;
        currentWriteTiles_ = writeTiles;
        nextChunkIndex_.store(0);
        workersRemaining_.store(workerThreads_.size() + 1u);
        chunkTaskReady_ = true;
        ++chunkTaskGeneration_;
    }

    workerCv_.notify_all();

    runPendingChunkTasks();
    completeChunkTaskParticipant();

    std::unique_lock<std::mutex> workerLock(workerMutex_);
    workerFinishedCv_.wait(workerLock, [this]() {
        return !chunkTaskReady_;
    });
}

// Claims chunk indices atomically until the active pass is complete.
void SimulationRuntime::runPendingChunkTasks() {
    for (;;) {
        const std::size_t chunkIndex = nextChunkIndex_.fetch_add(1);
        if (chunkIndex >= chunkLayout_.size()) {
            break;
        }

        executeChunkTask(chunkLayout_[chunkIndex]);
    }
}

// Records one participant finishing the current chunk-task generation.
void SimulationRuntime::completeChunkTaskParticipant() {
    if (workersRemaining_.fetch_sub(1) == 1u) {
        std::lock_guard<std::mutex> workerLock(workerMutex_);
        chunkTaskReady_ = false;
        currentWorkerTask_ = WorkerTaskType::None;
        currentReadTiles_ = 0;
        currentWriteTiles_ = 0;
        workerFinishedCv_.notify_one();
    }
}

// Routes the active worker task enum to the concrete chunk pass.
void SimulationRuntime::executeChunkTask(const ChunkRect& chunkRect) {
    switch (currentWorkerTask_) {
        case WorkerTaskType::NeighborPass:
            runNeighborChunk(chunkRect);
            break;

        case WorkerTaskType::LocalPass:
            runLocalChunk(chunkRect);
            break;

        case WorkerTaskType::None:
            break;
    }
}

// Computes neighbor diffusion from the read buffer into the write buffer.
void SimulationRuntime::runNeighborChunk(const ChunkRect& chunkRect) {
    if (currentReadTiles_ == 0 || currentWriteTiles_ == 0) {
        return;
    }

    const std::vector<Tile>& readTiles = *currentReadTiles_;
    std::vector<Tile>& writeTiles = *currentWriteTiles_;

    int tileY = chunkRect.startY;
    for (; tileY < chunkRect.startY + chunkRect.height; ++tileY) {
        int tileX = chunkRect.startX;
        for (; tileX < chunkRect.startX + chunkRect.width; ++tileX) {
            const int currentIndex = tileIndex(tileX, tileY);
            const Tile& sourceTile = readTiles[currentIndex];

            int airPollutionDelta = 0;
            int landValueDelta = 0;

            if (tileX < kMapWidth - 1) {
                const Tile& neighborTile = readTiles[tileIndex(tileX + 1, tileY)];
                airPollutionDelta += (neighborTile.airPollution - sourceTile.airPollution) / kPollutionSpreadRate;
                landValueDelta += (neighborTile.landValue - sourceTile.landValue) / kPollutionSpreadRate;
            }

            if (tileX > 0) {
                const Tile& neighborTile = readTiles[tileIndex(tileX - 1, tileY)];
                airPollutionDelta += (neighborTile.airPollution - sourceTile.airPollution) / kPollutionSpreadRate;
                landValueDelta += (neighborTile.landValue - sourceTile.landValue) / kPollutionSpreadRate;
            }

            if (tileY < kMapHeight - 1) {
                const Tile& neighborTile = readTiles[tileIndex(tileX, tileY + 1)];
                airPollutionDelta += (neighborTile.airPollution - sourceTile.airPollution) / kPollutionSpreadRate;
                landValueDelta += (neighborTile.landValue - sourceTile.landValue) / kPollutionSpreadRate;
            }

            if (tileY > 0) {
                const Tile& neighborTile = readTiles[tileIndex(tileX, tileY - 1)];
                airPollutionDelta += (neighborTile.airPollution - sourceTile.airPollution) / kPollutionSpreadRate;
                landValueDelta += (neighborTile.landValue - sourceTile.landValue) / kPollutionSpreadRate;
            }

            Tile nextTile = sourceTile;
            nextTile.airPollution = sourceTile.airPollution + airPollutionDelta;
            nextTile.landValue = sourceTile.landValue + landValueDelta;
            writeTiles[currentIndex] = nextTile;
        }
    }
}

// Applies per-tile decay and local land-value pressure in place.
void SimulationRuntime::runLocalChunk(const ChunkRect& chunkRect) {
    if (currentWriteTiles_ == 0) {
        return;
    }

    std::vector<Tile>& writeTiles = *currentWriteTiles_;

    int tileY = chunkRect.startY;
    for (; tileY < chunkRect.startY + chunkRect.height; ++tileY) {
        int tileX = chunkRect.startX;
        for (; tileX < chunkRect.startX + chunkRect.width; ++tileX) {
            Tile& tile = writeTiles[tileIndex(tileX, tileY)];
            if (tile.airPollution < 1) {
                tile.airPollution = 1;
            }

            tile.landValue -= tile.airPollution + 1;
            if (tile.landValue < 1) {
                tile.landValue = 0;
            }

            tile.airPollution -= 1;
        }
    }
}

// Runs the full-map neighbor pass over all chunks.
void SimulationRuntime::runNeighborPass(const std::vector<Tile>& readTiles, std::vector<Tile>& writeTiles) {
    parallelForEachChunk(WorkerTaskType::NeighborPass, &readTiles, &writeTiles);
}

// Drains player commands and applies them at a stable simulation boundary.
void SimulationRuntime::applyQueuedCommands(TileBuffer& writeBuffer) {
    std::deque<PlayerCommand> queuedCommands;
    {
        std::lock_guard<std::mutex> commandLock(commandMutex_);
        queuedCommands.swap(pendingCommands_);
    }

    while (!queuedCommands.empty()) {
        const PlayerCommand playerCommand = queuedCommands.front();
        queuedCommands.pop_front();

        if (!isTileInsideMap(playerCommand.tileX, playerCommand.tileY)) {
            continue;
        }

        switch (playerCommand.type) {
            case PlayerCommandType::PaintPollution:
                writeBuffer.tiles[tileIndex(playerCommand.tileX, playerCommand.tileY)].airPollution = playerCommand.amount;
                break;

            case PlayerCommandType::PlaceLot: {
                const LotAsset* lotAsset = findLotAssetById(playerCommand.assetId);
                if (lotAsset != 0) {
                    tryPlaceLot(*lotAsset, playerCommand.tileX, playerCommand.tileY, playerCommand.rotationSteps, writeBuffer);
                }
                break;
            }

            case PlayerCommandType::AddModuleAtTile: {
                const LotModule* moduleAsset = findModuleAssetById(playerCommand.assetId);
                if (moduleAsset != 0) {
                    tryAddModuleAtTile(*moduleAsset, playerCommand.tileX, playerCommand.tileY, writeBuffer);
                }
                break;
            }

            case PlayerCommandType::RemoveModuleAtTile:
                tryRemoveModuleAtTile(playerCommand.tileX, playerCommand.tileY, writeBuffer);
                break;

            case PlayerCommandType::PlaceRoadStroke:
                if (transportNetwork_.placeRoadStroke(playerCommand.roadStroke, lotOccupancy_, kInvalidLotId)) {
                    commutesDirty_ = true;
                }
                break;
        }
    }
}

// Applies all active lot/module effects after direct commands.
void SimulationRuntime::applyLotEffects(std::vector<Tile>& writeTiles) {
    std::size_t lotIndex = 0;
    for (; lotIndex < lots_.size(); ++lotIndex) {
        lots_[lotIndex].applyEffects(writeTiles);
    }
}

void SimulationRuntime::rebuildCityParameters() {
    const std::size_t parameterCount = cityParameterRegistry_.count();
    nextCityParameters_.assign(parameterCount, 0.0f);
    if (cityParameterDeltaBuffers_.empty()) {
        cityParameterDeltaBuffers_.assign(1u, std::vector<float>(parameterCount, 0.0f));
    }

    std::size_t bufferIndex = 0;
    for (; bufferIndex < cityParameterDeltaBuffers_.size(); ++bufferIndex) {
        cityParameterDeltaBuffers_[bufferIndex].assign(parameterCount, 0.0f);
    }

    std::size_t lotIndex = 0;
    for (; lotIndex < lots_.size(); ++lotIndex) {
        std::vector<float>& deltaBuffer = cityParameterDeltaBuffers_[lotIndex % cityParameterDeltaBuffers_.size()];
        const std::vector<CityParameterContribution>& contributions = lots_[lotIndex].parameterContributions();
        std::size_t contributionIndex = 0;
        for (; contributionIndex < contributions.size(); ++contributionIndex) {
            const CityParameterContribution& contribution = contributions[contributionIndex];
            if (contribution.parameterId >= 0 && contribution.parameterId < static_cast<int>(parameterCount)) {
                deltaBuffer[contribution.parameterId] += contribution.amount;
            }
        }
    }

    for (bufferIndex = 0; bufferIndex < cityParameterDeltaBuffers_.size(); ++bufferIndex) {
        std::size_t parameterIndex = 0;
        for (; parameterIndex < parameterCount; ++parameterIndex) {
            nextCityParameters_[parameterIndex] += cityParameterDeltaBuffers_[bufferIndex][parameterIndex];
        }
    }

    std::vector<float> derivedDeltas(parameterCount, 0.0f);
    std::size_t parameterIndex = 0;
    for (; parameterIndex < parameterCount; ++parameterIndex) {
        const CityParameterDefinition& definition = cityParameterRegistry_.definition(static_cast<int>(parameterIndex));
        std::size_t impactIndex = 0;
        for (; impactIndex < definition.impacts.size(); ++impactIndex) {
            const CityParameterImpact& impact = definition.impacts[impactIndex];
            if (impact.targetParameterId >= 0 && impact.targetParameterId < static_cast<int>(parameterCount)) {
                derivedDeltas[impact.targetParameterId] += nextCityParameters_[parameterIndex] * impact.multiplier;
            }
        }
    }

    for (parameterIndex = 0; parameterIndex < parameterCount; ++parameterIndex) {
        nextCityParameters_[parameterIndex] += derivedDeltas[parameterIndex];
    }
}

void SimulationRuntime::runCommuteAssignment() {
    if (!commutesDirty_) {
        return;
    }

    rebuildCityParameters();

    struct CommuteSource {
        std::size_t lotIndex;
        int demand;
        std::vector<std::uint32_t> accessNodes;
    };

    struct JobDestination {
        std::size_t lotIndex;
        int remainingCapacity;
        std::vector<std::uint32_t> accessNodes;
    };

    const TransportCostMap& costMap = transportNetwork_.costMap();
    std::vector<CommuteSource> sources;
    std::vector<JobDestination> destinations;
    const std::uint8_t allowedModeMask = kTransportModeCar | kTransportModePedestrian;

    std::size_t lotIndex = 0;
    for (; lotIndex < lots_.size(); ++lotIndex) {
        Lot& lot = lots_[lotIndex];
        lot.clearCommutes();

        std::vector<std::uint32_t> accessNodes;
        const LotAsset* lotAsset = findLotAssetById(lot.assetId());
        if (lotAsset != 0) {
            collectLotAccessNodes(lot, *lotAsset, allowedModeMask, accessNodes);
        }

        const int residentDemand = static_cast<int>(lotParameterAmount(lot, cityParameterRegistry_.residentsLowWealthId()) + 0.5f);
        if (residentDemand > 0) {
            lot.addCommuteDemand(residentDemand);
            CommuteSource source;
            source.lotIndex = lotIndex;
            source.demand = residentDemand;
            source.accessNodes = accessNodes;
            sources.push_back(source);
        }

        const int lowWealthJobCapacity = static_cast<int>(lotDerivedParameterAmount(lot, cityParameterRegistry_.jobsLowWealthId()) + 0.5f);
        if (lowWealthJobCapacity > 0) {
            JobDestination destination;
            destination.lotIndex = lotIndex;
            destination.remainingCapacity = lowWealthJobCapacity;
            destination.accessNodes = accessNodes;
            destinations.push_back(destination);
        }
    }

    std::vector<TransportPathResult> acceptedPaths;
    std::vector<std::uint16_t> acceptedDemands;
    TransportPathScratch scratch;
    int totalSatisfied = 0;

    std::size_t sourceIndex = 0;
    for (; sourceIndex < sources.size(); ++sourceIndex) {
        CommuteSource& source = sources[sourceIndex];
        if (source.accessNodes.empty()) {
            continue;
        }

        int remainingDemand = source.demand;
        while (remainingDemand > 0) {
            TransportPathRequest request;
            request.startNodeIds = source.accessNodes;
            request.routeSeed = static_cast<std::uint32_t>((lots_[source.lotIndex].id() * 73856093) ^ (remainingDemand * 19349663) ^ static_cast<int>(commuteRevision_ + 1u));
            request.demand = static_cast<std::uint16_t>(std::min(remainingDemand, static_cast<int>(kTransportMaxLoad)));
            request.useCongestion = true;

            std::vector<int> goalDestinationIndices;
            std::size_t destinationIndex = 0;
            for (; destinationIndex < destinations.size(); ++destinationIndex) {
                const JobDestination& destination = destinations[destinationIndex];
                if (destination.remainingCapacity <= 0 || destination.accessNodes.empty() || destination.lotIndex == source.lotIndex) {
                    continue;
                }

                std::size_t nodeIndex = 0;
                for (; nodeIndex < destination.accessNodes.size(); ++nodeIndex) {
                    request.goalNodeIds.push_back(destination.accessNodes[nodeIndex]);
                    goalDestinationIndices.push_back(static_cast<int>(destinationIndex));
                }
            }

            if (request.goalNodeIds.empty()) {
                break;
            }

            TransportPathResult pathResult;
            if (!costMap.findPath(request, scratch, pathResult)) {
                break;
            }

            int reachedDestinationIndex = -1;
            std::size_t goalIndex = 0;
            for (; goalIndex < request.goalNodeIds.size(); ++goalIndex) {
                if (request.goalNodeIds[goalIndex] == pathResult.reachedNodeId) {
                    reachedDestinationIndex = goalDestinationIndices[goalIndex];
                    break;
                }
            }

            if (reachedDestinationIndex < 0 || reachedDestinationIndex >= static_cast<int>(destinations.size())) {
                break;
            }

            JobDestination& reachedDestination = destinations[static_cast<std::size_t>(reachedDestinationIndex)];
            const int acceptedDemand = std::min(remainingDemand, reachedDestination.remainingCapacity);
            if (acceptedDemand <= 0) {
                break;
            }

            reachedDestination.remainingCapacity -= acceptedDemand;
            remainingDemand -= acceptedDemand;
            totalSatisfied += acceptedDemand;

            const std::uint16_t clampedDemand = static_cast<std::uint16_t>(std::min(acceptedDemand, static_cast<int>(kTransportMaxLoad)));
            acceptedPaths.push_back(pathResult);
            acceptedDemands.push_back(clampedDemand);
            lots_[source.lotIndex].addCommuteRoute(acceptedDemand, buildCommuteRouteSegments(pathResult, clampedDemand));
        }
    }

    transportNetwork_.beginTrafficAssignmentFromZero();
    std::size_t pathIndex = 0;
    for (; pathIndex < acceptedPaths.size(); ++pathIndex) {
        transportNetwork_.applyTrafficPathLoad(acceptedPaths[pathIndex], acceptedDemands[pathIndex], true);
    }
    transportNetwork_.commitTrafficAssignment();

    if (cityParameterRegistry_.satisfactionLowWealthCommuteId() >= 0 &&
        cityParameterRegistry_.satisfactionLowWealthCommuteId() < static_cast<int>(nextCityParameters_.size())) {
        nextCityParameters_[cityParameterRegistry_.satisfactionLowWealthCommuteId()] = static_cast<float>(totalSatisfied);
    }
    if (cityParameterRegistry_.satisfactionDirtyIndustryStaffingId() >= 0 &&
        cityParameterRegistry_.satisfactionDirtyIndustryStaffingId() < static_cast<int>(nextCityParameters_.size())) {
        nextCityParameters_[cityParameterRegistry_.satisfactionDirtyIndustryStaffingId()] = static_cast<float>(totalSatisfied);
    }

    oldCityParameters_ = nextCityParameters_;
    ++commuteRevision_;
    commutesDirty_ = false;
}

// Runs the full-map local tile pass over all chunks.
void SimulationRuntime::runLocalTilePass(std::vector<Tile>& writeTiles) {
    parallelForEachChunk(WorkerTaskType::LocalPass, 0, &writeTiles);
}

// Adds a command to the thread-safe pending input queue.
void SimulationRuntime::enqueueCommand(const PlayerCommand& playerCommand) {
    std::lock_guard<std::mutex> commandLock(commandMutex_);
    pendingCommands_.push_back(playerCommand);
}

// Publishes the completed write buffer and advances triple-buffer ownership.
void SimulationRuntime::publishCompletedBuffer() {
    const int completedBufferIndex = simulationWriteBufferIndex_;
    TileBuffer& completedBuffer = tileBuffers_[completedBufferIndex];
    refreshPublishedLotSnapshot(completedBuffer);
    refreshPublishedRoadSnapshot(completedBuffer);

    {
        std::lock_guard<std::mutex> publishedLock(publishedMutex_);
        publishedBufferIndex_ = completedBufferIndex;
        ++publishedGeneration_;
    }

    simulationReadBufferIndex_ = completedBufferIndex;
    simulationWriteBufferIndex_ = chooseNextWriteBuffer();
}

// Selects an unused buffer for the next simulation write pass.
int SimulationRuntime::chooseNextWriteBuffer() {
    const int immediateBufferIndex = findAvailableWriteBuffer();
    if (immediateBufferIndex >= 0) {
        writeBufferWaitMicros_.store(0);
        return immediateBufferIndex;
    }

    const std::chrono::steady_clock::time_point waitStart = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> renderLock(renderMutex_);
    renderCv_.wait(renderLock, [this]() {
        return !keepRunning_.load() || findAvailableWriteBuffer() >= 0;
    });
    writeBufferWaitMicros_.store(DurationMicros(waitStart, std::chrono::steady_clock::now()));
    return findAvailableWriteBuffer();
}

// Finds a non-read, non-render-pinned tile buffer.
int SimulationRuntime::findAvailableWriteBuffer() const {
    int bufferIndex = 0;
    for (; bufferIndex < 3; ++bufferIndex) {
        if (bufferIndex == simulationReadBufferIndex_) {
            continue;
        }

        if (bufferUseCounts_[bufferIndex].load() == 0) {
            return bufferIndex;
        }
    }

    return -1;
}

// Refreshes lot render/query snapshots only when the lot revision changed.
void SimulationRuntime::refreshPublishedLotSnapshot(TileBuffer& completedBuffer) {
    if (completedBuffer.lotRenderRevision == lotsRevision_ &&
        completedBuffer.commuteRenderRevision == commuteRevision_) {
        return;
    }

    completedBuffer.publishedLots.clear();
    completedBuffer.publishedLots.reserve(lots_.size());
    completedBuffer.publishedLotInfos.clear();
    completedBuffer.publishedLotInfos.reserve(lots_.size());
    completedBuffer.publishedLotOccupancy = lotOccupancy_;

    std::size_t lotIndex = 0;
    for (; lotIndex < lots_.size(); ++lotIndex) {
        PublishedLotInfo publishedLotInfo;
        publishedLotInfo.lotId = lots_[lotIndex].id();
        publishedLotInfo.assetId = lots_[lotIndex].assetId();
        publishedLotInfo.moduleSummary = lots_[lotIndex].moduleSummary();
        publishedLotInfo.parameterSummary = lots_[lotIndex].parameterSummary(cityParameterRegistry_);
        publishedLotInfo.commuteDemand = lots_[lotIndex].commuteDemand();
        publishedLotInfo.commuteSatisfied = lots_[lotIndex].commuteSatisfied();
        publishedLotInfo.commuteRouteSegments = lots_[lotIndex].commuteRouteSegments();
        completedBuffer.publishedLotInfos.push_back(publishedLotInfo);
        completedBuffer.publishedLots.push_back(lots_[lotIndex].buildRenderInstance());
    }

    completedBuffer.lotRenderRevision = lotsRevision_;
    completedBuffer.commuteRenderRevision = commuteRevision_;
}

// Refreshes road render/query snapshots only when the road revision changed.
void SimulationRuntime::refreshPublishedRoadSnapshot(TileBuffer& completedBuffer) {
    if (completedBuffer.roadRenderRevision != transportNetwork_.revision()) {
        completedBuffer.publishedRoads = transportNetwork_.resolvedCells();
        completedBuffer.publishedGroundRoadRenderState = transportNetwork_.groundRoadRenderState();
        completedBuffer.publishedGroundRoadChunkRevisions = transportNetwork_.groundChunkRevisions();
        completedBuffer.publishedElevatedRoadChunkRevisions = transportNetwork_.elevatedChunkRevisions();
        completedBuffer.roadRenderRevision = transportNetwork_.revision();
    }

    if (completedBuffer.overlayRenderRevision != transportNetwork_.trafficOverlayRevision()) {
        completedBuffer.publishedTileOverlayState = transportNetwork_.trafficOverlayState();
        completedBuffer.publishedTileOverlayChunkRevisions = transportNetwork_.trafficOverlayChunkRevisions();
        completedBuffer.overlayRenderRevision = transportNetwork_.trafficOverlayRevision();
    }
}

// Carries render-topology chunk revisions forward into the next write buffer.
void SimulationRuntime::copyChunkRevisionsForWriteBuffer() {
    tileBuffers_[simulationWriteBufferIndex_].chunkRevisions = tileBuffers_[simulationReadBufferIndex_].chunkRevisions;
}

// Bumps the render-topology revision for one tile's chunk.
void SimulationRuntime::markChunkDirtyByTile(std::vector<std::uint64_t>& chunkRevisions, int tileX, int tileY) {
    const int chunkIndex = chunkIndexForTile(tileX, tileY);
    if (chunkIndex < 0 || chunkIndex >= static_cast<int>(chunkRevisions.size())) {
        return;
    }

    ++chunkRevisions[static_cast<std::size_t>(chunkIndex)];
}

// Bumps each touched chunk once for a set of modified tile indices.
void SimulationRuntime::markChunksDirtyByTileIndices(std::vector<std::uint64_t>& chunkRevisions, const std::vector<int>& tileIndices) {
    std::vector<int> touchedChunkIndices;
    touchedChunkIndices.reserve(tileIndices.size());

    std::size_t tileIndexValue = 0;
    for (; tileIndexValue < tileIndices.size(); ++tileIndexValue) {
        const int tileLinearIndex = tileIndices[tileIndexValue];
        const int tileY = tileLinearIndex / kMapWidth;
        const int tileX = tileLinearIndex - (tileY * kMapWidth);
        touchedChunkIndices.push_back(chunkIndexForTile(tileX, tileY));
    }

    std::sort(touchedChunkIndices.begin(), touchedChunkIndices.end());
    touchedChunkIndices.erase(std::unique(touchedChunkIndices.begin(), touchedChunkIndices.end()), touchedChunkIndices.end());

    std::size_t touchedIndex = 0;
    for (; touchedIndex < touchedChunkIndices.size(); ++touchedIndex) {
        const int chunkIndex = touchedChunkIndices[touchedIndex];
        if (chunkIndex < 0 || chunkIndex >= static_cast<int>(chunkRevisions.size())) {
            continue;
        }

        ++chunkRevisions[static_cast<std::size_t>(chunkIndex)];
    }
}

// Maps a world tile coordinate to its chunk index.
int SimulationRuntime::chunkIndexForTile(int tileX, int tileY) const {
    if (!isTileInsideMap(tileX, tileY)) {
        return -1;
    }

    const int chunkColumnCount = kMapWidth / chunkConfig_.chunkWidth;
    const int chunkX = tileX / chunkConfig_.chunkWidth;
    const int chunkY = tileY / chunkConfig_.chunkHeight;
    return (chunkY * chunkColumnCount) + chunkX;
}

// Looks up a loaded lot archetype by id.
const LotAsset* SimulationRuntime::findLotAssetById(const std::string& lotAssetId) const {
    const std::unordered_map<std::string, std::size_t>::const_iterator iterator = lotAssetIndexById_.find(lotAssetId);
    if (iterator == lotAssetIndexById_.end()) {
        return 0;
    }

    return &lotAssets_[iterator->second];
}

// Looks up a loaded module archetype by id.
const LotModule* SimulationRuntime::findModuleAssetById(const std::string& moduleAssetId) const {
    const std::unordered_map<std::string, std::size_t>::const_iterator iterator = moduleAssetIndexById_.find(moduleAssetId);
    if (iterator == moduleAssetIndexById_.end()) {
        return 0;
    }

    return &moduleAssets_[iterator->second];
}

// Finds a mutable live lot by runtime id.
Lot* SimulationRuntime::findLotById(int lotId) {
    std::size_t lotIndex = 0;
    for (; lotIndex < lots_.size(); ++lotIndex) {
        if (lots_[lotIndex].id() == lotId) {
            return &lots_[lotIndex];
        }
    }

    return 0;
}

// Finds immutable published lot metadata by runtime id.
const PublishedLotInfo* SimulationRuntime::findPublishedLotInfoById(const std::vector<PublishedLotInfo>& publishedLotInfos, int lotId) const {
    std::size_t lotIndex = 0;
    for (; lotIndex < publishedLotInfos.size(); ++lotIndex) {
        if (publishedLotInfos[lotIndex].lotId == lotId) {
            return &publishedLotInfos[lotIndex];
        }
    }

    return 0;
}

// Builds a lot from an archetype so committed placement and ghost previews share geometry.
bool SimulationRuntime::buildLotCandidate(const LotAsset& lotAsset, int clickedTileX, int clickedTileY, int rotationSteps, int lotId, Lot& candidateLot) const {
    const int normalizedRotation = NormalizeRotationSteps(rotationSteps);
    candidateLot = Lot(lotId, lotAsset.id, clickedTileX, clickedTileY, normalizedRotation);

    Int2 footprintMinimum;
    Int2 footprintMaximum;
    RotatedRectangleBounds(lotAsset.footprintOrigin, lotAsset.footprintWidth, lotAsset.footprintHeight, normalizedRotation, footprintMinimum, footprintMaximum);
    candidateLot.setExplicitFootprint(
        footprintMinimum,
        footprintMaximum.x - footprintMinimum.x + 1,
        footprintMaximum.y - footprintMinimum.y + 1,
        kMapWidth);

    std::size_t placementIndex = 0;
    for (; placementIndex < lotAsset.initialModules.size(); ++placementIndex) {
        const LotModule* moduleAsset = findModuleAssetById(lotAsset.initialModules[placementIndex].moduleId);
        if (moduleAsset == 0) {
            return false;
        }

        const Int2 rotatedModuleOrigin = RotatedRectangleMinimum(
            lotAsset.initialModules[placementIndex].localOrigin,
            moduleAsset->width,
            moduleAsset->height,
            normalizedRotation);
        candidateLot.addModule(*moduleAsset, rotatedModuleOrigin, kMapWidth);
    }

    return true;
}

// Attempts to instantiate a lot archetype at the clicked tile.
bool SimulationRuntime::tryPlaceLot(const LotAsset& lotAsset, int clickedTileX, int clickedTileY, int rotationSteps, TileBuffer& writeBuffer) {
    Lot candidateLot;
    if (!buildLotCandidate(lotAsset, clickedTileX, clickedTileY, rotationSteps, nextLotId_, candidateLot)) {
        return false;
    }

    if (!canPlaceLot(candidateLot)) {
        return false;
    }

    lots_.push_back(candidateLot);
    setLotOccupancy(candidateLot.id(), candidateLot.occupiedTileIndices());
    markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, candidateLot.occupiedTileIndices());
    ++nextLotId_;
    ++lotsRevision_;
    commutesDirty_ = true;
    return true;
}

// Attempts to attach a module to exactly one adjacent lot.
bool SimulationRuntime::tryAddModuleAtTile(const LotModule& moduleAsset, int clickedTileX, int clickedTileY, TileBuffer& writeBuffer) {
    std::vector<int> adjacentLotIds;
    if (!collectAdjacentLotIdsForModule(moduleAsset, clickedTileX, clickedTileY, adjacentLotIds)) {
        return false;
    }

    const int targetLotId = adjacentLotIds[0];
    Lot* targetLot = findLotById(targetLotId);
    if (targetLot == 0) {
        return false;
    }

    const Int2 localOrigin(clickedTileX - targetLot->anchorTileX(), clickedTileY - targetLot->anchorTileY());
    targetLot->addModule(moduleAsset, localOrigin, kMapWidth);

    std::vector<int> newlyOccupiedTiles;
    const std::vector<int>& occupiedTileIndices = targetLot->occupiedTileIndices();
    std::size_t tileIndexValue = 0;
    for (; tileIndexValue < occupiedTileIndices.size(); ++tileIndexValue) {
        if (lotOccupancy_[occupiedTileIndices[tileIndexValue]] == kInvalidLotId) {
            newlyOccupiedTiles.push_back(occupiedTileIndices[tileIndexValue]);
        }
    }

    setLotOccupancy(targetLotId, newlyOccupiedTiles);
    markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, newlyOccupiedTiles);
    ++lotsRevision_;
    commutesDirty_ = true;
    return true;
}

// Attempts to remove the module occupying the clicked tile.
bool SimulationRuntime::tryRemoveModuleAtTile(int clickedTileX, int clickedTileY, TileBuffer& writeBuffer) {
    const int tileLinearIndex = tileIndex(clickedTileX, clickedTileY);
    const int lotId = lotOccupancy_[tileLinearIndex];
    if (lotId == kInvalidLotId) {
        return false;
    }

    Lot* targetLot = findLotById(lotId);
    if (targetLot == 0) {
        return false;
    }

    const std::vector<int> oldOccupiedTiles = targetLot->occupiedTileIndices();
    const Int2 localTile(clickedTileX - targetLot->anchorTileX(), clickedTileY - targetLot->anchorTileY());
    const int moduleInstanceId = targetLot->moduleInstanceIdAtLocalTile(localTile);
    if (moduleInstanceId < 0) {
        return false;
    }

    if (!targetLot->removeModule(moduleInstanceId, kMapWidth)) {
        return false;
    }

    clearLotOccupancy(oldOccupiedTiles);

    std::vector<int> dirtyTiles = oldOccupiedTiles;
    if (targetLot->modules().empty()) {
        std::size_t lotIndex = 0;
        for (; lotIndex < lots_.size(); ++lotIndex) {
            if (lots_[lotIndex].id() != lotId) {
                continue;
            }

            lots_.erase(lots_.begin() + static_cast<std::ptrdiff_t>(lotIndex));
            break;
        }
    } else {
        targetLot->rebaseAnchorToMinimumTile(kMapWidth);
        setLotOccupancy(lotId, targetLot->occupiedTileIndices());
        dirtyTiles.insert(dirtyTiles.end(), targetLot->occupiedTileIndices().begin(), targetLot->occupiedTileIndices().end());
    }

    markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, dirtyTiles);
    ++lotsRevision_;
    commutesDirty_ = true;
    return true;
}

// Checks occupancy and ground-road conflicts for a candidate lot footprint.
bool SimulationRuntime::canPlaceLot(const Lot& candidateLot) const {
    const std::vector<int>& occupiedTileIndices = candidateLot.occupiedTileIndices();
    std::size_t tileIndexValue = 0;
    for (; tileIndexValue < occupiedTileIndices.size(); ++tileIndexValue) {
        const int tileLinearIndex = occupiedTileIndices[tileIndexValue];
        const int tileY = tileLinearIndex / kMapWidth;
        const int tileX = tileLinearIndex - (tileY * kMapWidth);
        if (!isTileInsideMap(tileX, tileY)) {
            return false;
        }

        if (lotOccupancy_[tileLinearIndex] != kInvalidLotId) {
            return false;
        }

        if (transportNetwork_.hasGroundOccupancy(tileLinearIndex)) {
            return false;
        }
    }

    return true;
}

// Finds the single neighboring lot eligible for a module placement.
bool SimulationRuntime::collectAdjacentLotIdsForModule(const LotModule& moduleAsset, int clickedTileX, int clickedTileY, std::vector<int>& adjacentLotIds) const {
    adjacentLotIds.clear();
    std::vector<int> candidateTileIndices;
    candidateTileIndices.reserve(static_cast<std::size_t>(moduleAsset.width * moduleAsset.height));

    int tileY = 0;
    for (; tileY < moduleAsset.height; ++tileY) {
        int tileX = 0;
        for (; tileX < moduleAsset.width; ++tileX) {
            const int worldTileX = clickedTileX + tileX;
            const int worldTileY = clickedTileY + tileY;
            if (!isTileInsideMap(worldTileX, worldTileY)) {
                return false;
            }

            const int candidateTileIndex = tileIndex(worldTileX, worldTileY);
            if (lotOccupancy_[candidateTileIndex] != kInvalidLotId) {
                return false;
            }

            candidateTileIndices.push_back(candidateTileIndex);
        }
    }

    const int neighborOffsets[4][2] = {
        {1, 0},
        {-1, 0},
        {0, 1},
        {0, -1}
    };

    std::size_t candidateIndex = 0;
    for (; candidateIndex < candidateTileIndices.size(); ++candidateIndex) {
        const int candidateTileIndex = candidateTileIndices[candidateIndex];
        const int candidateTileY = candidateTileIndex / kMapWidth;
        const int candidateTileX = candidateTileIndex - (candidateTileY * kMapWidth);

        int neighborIndex = 0;
        for (; neighborIndex < 4; ++neighborIndex) {
            const int neighborTileX = candidateTileX + neighborOffsets[neighborIndex][0];
            const int neighborTileY = candidateTileY + neighborOffsets[neighborIndex][1];
            if (!isTileInsideMap(neighborTileX, neighborTileY)) {
                continue;
            }

            const int neighborTileIndex = tileIndex(neighborTileX, neighborTileY);
            if (std::find(candidateTileIndices.begin(), candidateTileIndices.end(), neighborTileIndex) != candidateTileIndices.end()) {
                continue;
            }

            const int neighborLotId = lotOccupancy_[neighborTileIndex];
            if (neighborLotId != kInvalidLotId && std::find(adjacentLotIds.begin(), adjacentLotIds.end(), neighborLotId) == adjacentLotIds.end()) {
                adjacentLotIds.push_back(neighborLotId);
            }
        }
    }

    if (adjacentLotIds.size() != 1u) {
        adjacentLotIds.clear();
        return false;
    }

    return true;
}

// Clears lot occupancy for a known set of world tile indices.
void SimulationRuntime::clearLotOccupancy(const std::vector<int>& tileIndices) {
    std::size_t tileIndexValue = 0;
    for (; tileIndexValue < tileIndices.size(); ++tileIndexValue) {
        lotOccupancy_[tileIndices[tileIndexValue]] = kInvalidLotId;
    }
}

// Assigns lot occupancy for a known set of world tile indices.
void SimulationRuntime::setLotOccupancy(int lotId, const std::vector<int>& tileIndices) {
    std::size_t tileIndexValue = 0;
    for (; tileIndexValue < tileIndices.size(); ++tileIndexValue) {
        lotOccupancy_[tileIndices[tileIndexValue]] = lotId;
    }
}

float SimulationRuntime::lotParameterAmount(const Lot& lot, int parameterId) const {
    if (parameterId < 0) {
        return 0.0f;
    }

    float amount = 0.0f;
    const std::vector<CityParameterContribution>& contributions = lot.parameterContributions();
    std::size_t contributionIndex = 0;
    for (; contributionIndex < contributions.size(); ++contributionIndex) {
        if (contributions[contributionIndex].parameterId == parameterId) {
            amount += contributions[contributionIndex].amount;
        }
    }

    return amount;
}

float SimulationRuntime::lotDerivedParameterAmount(const Lot& lot, int parameterId) const {
    float amount = lotParameterAmount(lot, parameterId);
    const std::vector<CityParameterContribution>& contributions = lot.parameterContributions();
    std::size_t contributionIndex = 0;
    for (; contributionIndex < contributions.size(); ++contributionIndex) {
        const CityParameterContribution& contribution = contributions[contributionIndex];
        if (contribution.parameterId < 0 || contribution.parameterId >= static_cast<int>(cityParameterRegistry_.count())) {
            continue;
        }

        const CityParameterDefinition& definition = cityParameterRegistry_.definition(contribution.parameterId);
        std::size_t impactIndex = 0;
        for (; impactIndex < definition.impacts.size(); ++impactIndex) {
            const CityParameterImpact& impact = definition.impacts[impactIndex];
            if (impact.targetParameterId == parameterId) {
                amount += contribution.amount * impact.multiplier;
            }
        }
    }

    return amount;
}

void SimulationRuntime::collectLotAccessNodes(const Lot& lot, const LotAsset& lotAsset, std::uint8_t allowedModeMask, std::vector<std::uint32_t>& accessNodes) const {
    accessNodes.clear();
    if (lotAsset.accessDefinitions.empty()) {
        return;
    }

    const TransportCostMap& costMap = transportNetwork_.costMap();
    std::size_t accessIndex = 0;
    for (; accessIndex < lotAsset.accessDefinitions.size(); ++accessIndex) {
        const LotAccessDefinition& accessDefinition = lotAsset.accessDefinitions[accessIndex];
        const std::uint8_t usableModeMask = static_cast<std::uint8_t>(accessDefinition.modeMask & allowedModeMask);
        if (usableModeMask == 0u) {
            continue;
        }

        const Int2 rotatedLocalTile = RotateLocalTile(accessDefinition.localTile, lot.rotationSteps());
        const std::uint8_t rotatedDirection = RotateRoadDirection(accessDefinition.direction, lot.rotationSteps());
        const int buildingTileX = lot.anchorTileX() + rotatedLocalTile.x;
        const int buildingTileY = lot.anchorTileY() + rotatedLocalTile.y;
        const int accessTileX = buildingTileX + RoadDirectionDeltaX(rotatedDirection);
        const int accessTileY = buildingTileY + RoadDirectionDeltaY(rotatedDirection);
        if (!isTileInsideMap(accessTileX, accessTileY)) {
            continue;
        }

        const int accessTileIndex = tileIndex(accessTileX, accessTileY);
        const std::uint8_t accessDirectionTowardBuilding = OppositeRoadDirection(rotatedDirection);
        std::size_t layerIndex = 0;
        for (; layerIndex < static_cast<std::size_t>(TransportLayerId::Count); ++layerIndex) {
            std::size_t modeIndex = 0;
            for (; modeIndex < static_cast<std::size_t>(TransportMode::Count); ++modeIndex) {
                const TransportMode mode = static_cast<TransportMode>(modeIndex);
                if ((usableModeMask & TransportModeMaskFor(mode)) == 0u) {
                    continue;
                }

                const TransportCostCell& accessCell = costMap.cell(static_cast<TransportLayerId>(layerIndex), mode, accessTileIndex);
                if ((accessCell.buildingAccessMask & accessDirectionTowardBuilding) != 0u) {
                    accessNodes.push_back(costMap.nodeId(static_cast<TransportLayerId>(layerIndex), mode, accessTileIndex));
                }
            }
        }
    }

    std::sort(accessNodes.begin(), accessNodes.end());
    accessNodes.erase(std::unique(accessNodes.begin(), accessNodes.end()), accessNodes.end());
}

std::vector<CommuteRouteSegment> SimulationRuntime::buildCommuteRouteSegments(const TransportPathResult& pathResult, std::uint16_t demand) const {
    std::vector<CommuteRouteSegment> segments;
    if (!pathResult.success || demand == 0u) {
        return segments;
    }

    const TransportCostMap& costMap = transportNetwork_.costMap();
    std::size_t stepIndex = 0;
    for (; stepIndex < pathResult.steps.size(); ++stepIndex) {
        const TransportPathStep& step = pathResult.steps[stepIndex];
        if (step.kind != TransportPathStepKind::Movement || step.roadDirection == 0u) {
            continue;
        }

        const int fromTileIndex = costMap.nodeTileIndex(step.fromNodeId);
        const int toTileIndex = costMap.nodeTileIndex(step.toNodeId);
        const int fromTileY = fromTileIndex / kMapWidth;
        const int fromTileX = fromTileIndex - (fromTileY * kMapWidth);
        const int toTileY = toTileIndex / kMapWidth;
        const int toTileX = toTileIndex - (toTileY * kMapWidth);

        if (!segments.empty()) {
            CommuteRouteSegment& previous = segments.back();
            if (previous.direction == step.roadDirection &&
                previous.layer == costMap.nodeLayer(step.fromNodeId) &&
                previous.mode == costMap.nodeMode(step.fromNodeId) &&
                previous.endTileX == fromTileX &&
                previous.endTileY == fromTileY) {
                previous.endTileX = toTileX;
                previous.endTileY = toTileY;
                previous.demand = std::max(previous.demand, demand);
                continue;
            }
        }

        CommuteRouteSegment segment;
        segment.startTileX = fromTileX;
        segment.startTileY = fromTileY;
        segment.endTileX = toTileX;
        segment.endTileY = toTileY;
        segment.layer = costMap.nodeLayer(step.fromNodeId);
        segment.mode = costMap.nodeMode(step.fromNodeId);
        segment.direction = step.roadDirection;
        segment.demand = demand;
        segments.push_back(segment);
    }

    return segments;
}

// Validates a tile coordinate against the fixed map bounds.
bool SimulationRuntime::isTileInsideMap(int tileX, int tileY) const {
    return tileX >= 0 && tileX < kMapWidth && tileY >= 0 && tileY < kMapHeight;
}

// Converts a tile coordinate to row-major storage index.
int SimulationRuntime::tileIndex(int tileX, int tileY) const {
    return (tileY * kMapWidth) + tileX;
}
