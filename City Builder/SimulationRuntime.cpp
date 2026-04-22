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
long long DurationMicros(const std::chrono::steady_clock::time_point& startTime, const std::chrono::steady_clock::time_point& endTime) {
    return std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
}

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
}

SimulationRuntime::SimulationRuntime(const RuntimeOptions& runtimeOptions)
    : simulationReadBufferIndex_(0),
      simulationWriteBufferIndex_(1),
      publishedBufferIndex_(0),
      publishedGeneration_(0),
      nextLotId_(1),
      lotsRevision_(0),
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

    loadAssets();

    tileBuffers_.resize(3);
    const std::size_t totalTileCount = static_cast<std::size_t>(kMapWidth) * static_cast<std::size_t>(kMapHeight);
    const std::size_t chunkCount = chunkLayout_.size();
    lotOccupancy_.assign(totalTileCount, kInvalidLotId);

    int bufferIndex = 0;
    for (; bufferIndex < 3; ++bufferIndex) {
        tileBuffers_[bufferIndex].tiles.resize(totalTileCount);
        tileBuffers_[bufferIndex].chunkRevisions.assign(chunkCount, 1);
        tileBuffers_[bufferIndex].publishedLots.clear();
        tileBuffers_[bufferIndex].publishedLotInfos.clear();
        tileBuffers_[bufferIndex].publishedLotOccupancy.assign(totalTileCount, kInvalidLotId);
        tileBuffers_[bufferIndex].lotRenderRevision = 0;
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

SimulationRuntime::~SimulationRuntime() {
    stop();
}

void SimulationRuntime::start() {
    if (keepRunning_.exchange(true)) {
        return;
    }

    startWorkers();
    simulationThread_ = std::thread(&SimulationRuntime::simulationLoop, this);
}

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

void SimulationRuntime::queuePlaceLot(const std::string& lotAssetId, int tileX, int tileY) {
    if (!isTileInsideMap(tileX, tileY)) {
        return;
    }

    PlayerCommand playerCommand;
    playerCommand.type = PlayerCommandType::PlaceLot;
    playerCommand.tileX = tileX;
    playerCommand.tileY = tileY;
    playerCommand.assetId = lotAssetId;
    enqueueCommand(playerCommand);
}

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

void SimulationRuntime::queuePlaceSmokestack(int tileX, int tileY) {
    queuePlaceLot("smokestack_lot", tileX, tileY);
}

void SimulationRuntime::queuePlacePark(int tileX, int tileY) {
    queuePlaceLot("park_lot", tileX, tileY);
}

void SimulationRuntime::queueAddSmokestackModule(int tileX, int tileY) {
    queueAddModuleAtTile("smokestack_module", tileX, tileY);
}

void SimulationRuntime::queueAddParkModule(int tileX, int tileY) {
    queueAddModuleAtTile("park_module", tileX, tileY);
}

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
            }
        }
    }

    return queryResult;
}

PublishedWorldSnapshot SimulationRuntime::acquirePublishedSnapshot() {
    PublishedWorldSnapshot snapshot;

    std::lock_guard<std::mutex> publishedLock(publishedMutex_);
    snapshot.bufferIndex = publishedBufferIndex_;
    snapshot.tiles = &tileBuffers_[publishedBufferIndex_].tiles;
    snapshot.lots = &tileBuffers_[publishedBufferIndex_].publishedLots;
    snapshot.chunkRevisions = &tileBuffers_[publishedBufferIndex_].chunkRevisions;
    snapshot.lotOccupancy = &tileBuffers_[publishedBufferIndex_].publishedLotOccupancy;
    snapshot.width = kMapWidth;
    snapshot.height = kMapHeight;
    snapshot.generation = publishedGeneration_;
    snapshot.lotRevision = tileBuffers_[publishedBufferIndex_].lotRenderRevision;

    if (snapshot.bufferIndex >= 0) {
        bufferUseCounts_[snapshot.bufferIndex].fetch_add(1);
    }

    return snapshot;
}

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

int SimulationRuntime::mapWidth() const {
    return kMapWidth;
}

int SimulationRuntime::mapHeight() const {
    return kMapHeight;
}

int SimulationRuntime::updatesPerSecond() const {
    return updatesPerSecond_.load();
}

const ChunkConfig& SimulationRuntime::chunkConfig() const {
    return chunkConfig_;
}

const std::vector<ChunkRect>& SimulationRuntime::chunkLayout() const {
    return chunkLayout_;
}

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

void SimulationRuntime::loadAssets() {
    LoadedGameAssets loadedAssets;
    std::string errorMessage;
    if (!LoadGameAssets(GetExecutableDirectory() + "\\Data", loadedAssets, errorMessage)) {
        throw std::runtime_error(errorMessage);
    }

    moduleAssets_ = loadedAssets.modules;
    lotAssets_ = loadedAssets.lots;

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
        tileBuffers_[bufferIndex].lotRenderRevision = 0;
    }

    lotOccupancy_.assign(static_cast<std::size_t>(kMapWidth) * static_cast<std::size_t>(kMapHeight), kInvalidLotId);
    lots_.clear();
    nextLotId_ = 1;
    lotsRevision_ = 0;

    std::lock_guard<std::mutex> publishedLock(publishedMutex_);
    publishedBufferIndex_ = simulationReadBufferIndex_;
    publishedGeneration_ = 0;
}

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

void SimulationRuntime::startWorkers() {
    stopWorkerThreads_ = false;
    workerThreads_.clear();

    int workerIndex = 0;
    for (; workerIndex < chunkConfig_.workerThreadCount; ++workerIndex) {
        workerThreads_.push_back(std::thread(&SimulationRuntime::workerMain, this));
    }
}

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

void SimulationRuntime::runPendingChunkTasks() {
    for (;;) {
        const std::size_t chunkIndex = nextChunkIndex_.fetch_add(1);
        if (chunkIndex >= chunkLayout_.size()) {
            break;
        }

        executeChunkTask(chunkLayout_[chunkIndex]);
    }
}

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

void SimulationRuntime::runNeighborPass(const std::vector<Tile>& readTiles, std::vector<Tile>& writeTiles) {
    parallelForEachChunk(WorkerTaskType::NeighborPass, &readTiles, &writeTiles);
}

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
                    tryPlaceLot(*lotAsset, playerCommand.tileX, playerCommand.tileY, writeBuffer);
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
        }
    }
}

void SimulationRuntime::applyLotEffects(std::vector<Tile>& writeTiles) {
    std::size_t lotIndex = 0;
    for (; lotIndex < lots_.size(); ++lotIndex) {
        lots_[lotIndex].applyEffects(writeTiles);
    }
}

void SimulationRuntime::runLocalTilePass(std::vector<Tile>& writeTiles) {
    parallelForEachChunk(WorkerTaskType::LocalPass, 0, &writeTiles);
}

void SimulationRuntime::enqueueCommand(const PlayerCommand& playerCommand) {
    std::lock_guard<std::mutex> commandLock(commandMutex_);
    pendingCommands_.push_back(playerCommand);
}

void SimulationRuntime::publishCompletedBuffer() {
    const int completedBufferIndex = simulationWriteBufferIndex_;
    TileBuffer& completedBuffer = tileBuffers_[completedBufferIndex];
    refreshPublishedLotSnapshot(completedBuffer);

    {
        std::lock_guard<std::mutex> publishedLock(publishedMutex_);
        publishedBufferIndex_ = completedBufferIndex;
        ++publishedGeneration_;
    }

    simulationReadBufferIndex_ = completedBufferIndex;
    simulationWriteBufferIndex_ = chooseNextWriteBuffer();
}

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

void SimulationRuntime::refreshPublishedLotSnapshot(TileBuffer& completedBuffer) {
    if (completedBuffer.lotRenderRevision == lotsRevision_) {
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
        completedBuffer.publishedLotInfos.push_back(publishedLotInfo);
        completedBuffer.publishedLots.push_back(lots_[lotIndex].buildRenderInstance());
    }

    completedBuffer.lotRenderRevision = lotsRevision_;
}

void SimulationRuntime::copyChunkRevisionsForWriteBuffer() {
    tileBuffers_[simulationWriteBufferIndex_].chunkRevisions = tileBuffers_[simulationReadBufferIndex_].chunkRevisions;
}

void SimulationRuntime::markChunkDirtyByTile(std::vector<std::uint64_t>& chunkRevisions, int tileX, int tileY) {
    const int chunkIndex = chunkIndexForTile(tileX, tileY);
    if (chunkIndex < 0 || chunkIndex >= static_cast<int>(chunkRevisions.size())) {
        return;
    }

    ++chunkRevisions[static_cast<std::size_t>(chunkIndex)];
}

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

int SimulationRuntime::chunkIndexForTile(int tileX, int tileY) const {
    if (!isTileInsideMap(tileX, tileY)) {
        return -1;
    }

    const int chunkColumnCount = kMapWidth / chunkConfig_.chunkWidth;
    const int chunkX = tileX / chunkConfig_.chunkWidth;
    const int chunkY = tileY / chunkConfig_.chunkHeight;
    return (chunkY * chunkColumnCount) + chunkX;
}

const LotAsset* SimulationRuntime::findLotAssetById(const std::string& lotAssetId) const {
    const std::unordered_map<std::string, std::size_t>::const_iterator iterator = lotAssetIndexById_.find(lotAssetId);
    if (iterator == lotAssetIndexById_.end()) {
        return 0;
    }

    return &lotAssets_[iterator->second];
}

const LotModule* SimulationRuntime::findModuleAssetById(const std::string& moduleAssetId) const {
    const std::unordered_map<std::string, std::size_t>::const_iterator iterator = moduleAssetIndexById_.find(moduleAssetId);
    if (iterator == moduleAssetIndexById_.end()) {
        return 0;
    }

    return &moduleAssets_[iterator->second];
}

Lot* SimulationRuntime::findLotById(int lotId) {
    std::size_t lotIndex = 0;
    for (; lotIndex < lots_.size(); ++lotIndex) {
        if (lots_[lotIndex].id() == lotId) {
            return &lots_[lotIndex];
        }
    }

    return 0;
}

const PublishedLotInfo* SimulationRuntime::findPublishedLotInfoById(const std::vector<PublishedLotInfo>& publishedLotInfos, int lotId) const {
    std::size_t lotIndex = 0;
    for (; lotIndex < publishedLotInfos.size(); ++lotIndex) {
        if (publishedLotInfos[lotIndex].lotId == lotId) {
            return &publishedLotInfos[lotIndex];
        }
    }

    return 0;
}

bool SimulationRuntime::tryPlaceLot(const LotAsset& lotAsset, int clickedTileX, int clickedTileY, TileBuffer& writeBuffer) {
    Lot candidateLot(nextLotId_, lotAsset.id, clickedTileX, clickedTileY);

    std::size_t placementIndex = 0;
    for (; placementIndex < lotAsset.initialModules.size(); ++placementIndex) {
        const LotModule* moduleAsset = findModuleAssetById(lotAsset.initialModules[placementIndex].moduleId);
        if (moduleAsset == 0) {
            return false;
        }

        candidateLot.addModule(*moduleAsset, lotAsset.initialModules[placementIndex].localOrigin, kMapWidth);
    }

    if (!canPlaceLot(candidateLot)) {
        return false;
    }

    lots_.push_back(candidateLot);
    setLotOccupancy(candidateLot.id(), candidateLot.occupiedTileIndices());
    markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, candidateLot.occupiedTileIndices());
    ++nextLotId_;
    ++lotsRevision_;
    return true;
}

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
    return true;
}

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
    return true;
}

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
    }

    return true;
}

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

void SimulationRuntime::clearLotOccupancy(const std::vector<int>& tileIndices) {
    std::size_t tileIndexValue = 0;
    for (; tileIndexValue < tileIndices.size(); ++tileIndexValue) {
        lotOccupancy_[tileIndices[tileIndexValue]] = kInvalidLotId;
    }
}

void SimulationRuntime::setLotOccupancy(int lotId, const std::vector<int>& tileIndices) {
    std::size_t tileIndexValue = 0;
    for (; tileIndexValue < tileIndices.size(); ++tileIndexValue) {
        lotOccupancy_[tileIndices[tileIndexValue]] = lotId;
    }
}

bool SimulationRuntime::isTileInsideMap(int tileX, int tileY) const {
    return tileX >= 0 && tileX < kMapWidth && tileY >= 0 && tileY < kMapHeight;
}

int SimulationRuntime::tileIndex(int tileX, int tileY) const {
    return (tileY * kMapWidth) + tileX;
}
