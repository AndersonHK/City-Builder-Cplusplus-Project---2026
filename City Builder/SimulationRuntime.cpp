#include "SimulationRuntime.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iostream>
#include <random>

SimulationRuntime::SimulationRuntime(const RuntimeOptions& runtimeOptions)
    : simulationReadBufferIndex_(0),
      simulationWriteBufferIndex_(1),
      publishedBufferIndex_(0),
      publishedGeneration_(0),
      smokeStackModule_(1, 1, 10000, -1000),
      parkModule_(2, 2, -4000, 10000),
      keepRunning_(false),
      nextChunkIndex_(0),
      workersRemaining_(0),
      chunkTaskReady_(false),
      stopWorkerThreads_(false),
      chunkTaskGeneration_(0),
      updatesPerSecond_(0) {
    runtimeOptions_ = runtimeOptions;

    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    const int workerThreadCount = hardwareThreads > 2u ? static_cast<int>(hardwareThreads) - 2 : 1;
    chunkConfig_ = CalculateChunkConfig(kMapWidth, kMapHeight, sizeof(Tile), workerThreadCount, runtimeOptions_.manualL2BytesPerLogicalThread, runtimeOptions_.detectL2CacheSize, runtimeOptions_.usableL2Fraction, kMinimumJobsPerWorkerMultiplier);
    chunkLayout_ = BuildChunkLayout(kMapWidth, kMapHeight, chunkConfig_.chunkWidth, chunkConfig_.chunkHeight);

    smokeStackDefinition_.footprintWidth = 1;
    smokeStackDefinition_.footprintHeight = 2;
    smokeStackDefinition_.renderOriginOffsetX = 0;
    smokeStackDefinition_.renderOriginOffsetY = -1;
    smokeStackDefinition_.occupiedOffsets.push_back(Int2(0, 0));
    smokeStackDefinition_.occupiedOffsets.push_back(Int2(0, -1));
    smokeStackDefinition_.modules.push_back(&smokeStackModule_);
    smokeStackDefinition_.modules.push_back(&smokeStackModule_);

    parkDefinition_.footprintWidth = 2;
    parkDefinition_.footprintHeight = 2;
    parkDefinition_.renderOriginOffsetX = -1;
    parkDefinition_.renderOriginOffsetY = -1;
    parkDefinition_.occupiedOffsets.push_back(Int2(0, 0));
    parkDefinition_.occupiedOffsets.push_back(Int2(0, -1));
    parkDefinition_.occupiedOffsets.push_back(Int2(-1, 0));
    parkDefinition_.occupiedOffsets.push_back(Int2(-1, -1));
    parkDefinition_.modules.push_back(&parkModule_);

    tileBuffers_.resize(3);
    int bufferIndex = 0;
    for (; bufferIndex < 3; ++bufferIndex) {
        tileBuffers_[bufferIndex].tiles.resize(static_cast<std::size_t>(kMapWidth) * static_cast<std::size_t>(kMapHeight));
        bufferUseCounts_[bufferIndex].store(0);
    }

    lastRenderedGeneration_.store(0);
    initializeWorld();

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

    std::lock_guard<std::mutex> commandLock(commandMutex_);
    pendingCommands_.push_back(playerCommand);
}

void SimulationRuntime::queuePlaceSmokestack(int tileX, int tileY) {
    if (!isTileInsideMap(tileX, tileY)) {
        return;
    }

    PlayerCommand playerCommand;
    playerCommand.type = PlayerCommandType::PlaceSmokestack;
    playerCommand.tileX = tileX;
    playerCommand.tileY = tileY;
    playerCommand.amount = 0;

    std::lock_guard<std::mutex> commandLock(commandMutex_);
    pendingCommands_.push_back(playerCommand);
}

void SimulationRuntime::queuePlacePark(int tileX, int tileY) {
    if (!isTileInsideMap(tileX, tileY)) {
        return;
    }

    PlayerCommand playerCommand;
    playerCommand.type = PlayerCommandType::PlacePark;
    playerCommand.tileX = tileX;
    playerCommand.tileY = tileY;
    playerCommand.amount = 0;

    std::lock_guard<std::mutex> commandLock(commandMutex_);
    pendingCommands_.push_back(playerCommand);
}

TileQueryResult SimulationRuntime::queryTile(int tileX, int tileY) const {
    TileQueryResult queryResult;
    if (!isTileInsideMap(tileX, tileY)) {
        return queryResult;
    }

    std::lock_guard<std::mutex> publishedLock(publishedMutex_);
    queryResult.isValid = true;
    queryResult.tile = tileBuffers_[publishedBufferIndex_].tiles[tileIndex(tileX, tileY)];
    queryResult.generation = publishedGeneration_;
    return queryResult;
}

PublishedWorldSnapshot SimulationRuntime::acquirePublishedSnapshot() {
    PublishedWorldSnapshot snapshot;

    std::lock_guard<std::mutex> publishedLock(publishedMutex_);
    snapshot.bufferIndex = publishedBufferIndex_;
    snapshot.tiles = &tileBuffers_[publishedBufferIndex_].tiles;
    snapshot.width = kMapWidth;
    snapshot.height = kMapHeight;
    snapshot.lots = publishedLots_;
    snapshot.generation = publishedGeneration_;

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

    std::lock_guard<std::mutex> publishedLock(publishedMutex_);
    publishedLots_.clear();
    publishedBufferIndex_ = simulationReadBufferIndex_;
    publishedGeneration_ = 0;
}

void SimulationRuntime::simulationLoop() {
    int updatesThisSecond = 0;
    std::chrono::steady_clock::time_point secondWindowStart = std::chrono::steady_clock::now();

    while (keepRunning_.load()) {
        const std::vector<Tile>& readTiles = tileBuffers_[simulationReadBufferIndex_].tiles;
        std::vector<Tile>& writeTiles = tileBuffers_[simulationWriteBufferIndex_].tiles;

        runNeighborPass(readTiles, writeTiles);
        applyQueuedCommands(writeTiles);
        applyLotEffects(writeTiles);
        runLocalTilePass(writeTiles);
        publishCompletedBuffer();

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

    std::size_t workerIndex = 0;
    for (; workerIndex < workerThreads_.size(); ++workerIndex) {
        if (workerThreads_[workerIndex].joinable()) {
            workerThreads_[workerIndex].join();
        }
    }

    workerThreads_.clear();
    stopWorkerThreads_ = false;
    chunkTaskReady_ = false;
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

        for (;;) {
            std::function<void(const ChunkRect&)> chunkTask;
            ChunkRect chunkRect;

            {
                std::lock_guard<std::mutex> workerLock(workerMutex_);
                if (nextChunkIndex_ >= chunkLayout_.size()) {
                    if (workersRemaining_ > 0) {
                        --workersRemaining_;
                        if (workersRemaining_ == 0) {
                            chunkTaskReady_ = false;
                            workerFinishedCv_.notify_one();
                        }
                    }

                    break;
                }

                chunkRect = chunkLayout_[nextChunkIndex_];
                ++nextChunkIndex_;
                chunkTask = currentChunkTask_;
            }

            if (chunkTask) {
                chunkTask(chunkRect);
            }
        }
    }
}

void SimulationRuntime::parallelForEachChunk(const std::function<void(const ChunkRect&)>& chunkTask) {
    if (chunkLayout_.empty()) {
        return;
    }

    if (workerThreads_.empty()) {
        std::size_t chunkIndex = 0;
        for (; chunkIndex < chunkLayout_.size(); ++chunkIndex) {
            chunkTask(chunkLayout_[chunkIndex]);
        }
        return;
    }

    {
        std::lock_guard<std::mutex> workerLock(workerMutex_);
        currentChunkTask_ = chunkTask;
        nextChunkIndex_ = 0;
        workersRemaining_ = workerThreads_.size();
        chunkTaskReady_ = true;
        ++chunkTaskGeneration_;
    }

    workerCv_.notify_all();

    std::unique_lock<std::mutex> workerLock(workerMutex_);
    workerFinishedCv_.wait(workerLock, [this]() {
        return !chunkTaskReady_;
    });
}

void SimulationRuntime::runNeighborPass(const std::vector<Tile>& readTiles, std::vector<Tile>& writeTiles) {
    // Chunk sizing is derived from the Tile working set so this hot pass stays inside a declared cache budget.
    parallelForEachChunk([this, &readTiles, &writeTiles](const ChunkRect& chunkRect) {
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
    });
}

void SimulationRuntime::applyQueuedCommands(std::vector<Tile>& writeTiles) {
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
                writeTiles[tileIndex(playerCommand.tileX, playerCommand.tileY)].airPollution = playerCommand.amount;
                break;

            case PlayerCommandType::PlaceSmokestack:
                tryPlaceLot(smokeStackDefinition_, playerCommand.tileX, playerCommand.tileY, writeTiles);
                break;

            case PlayerCommandType::PlacePark:
                tryPlaceLot(parkDefinition_, playerCommand.tileX, playerCommand.tileY, writeTiles);
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
    parallelForEachChunk([this, &writeTiles](const ChunkRect& chunkRect) {
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
    });
}

void SimulationRuntime::publishCompletedBuffer() {
    const int completedBufferIndex = simulationWriteBufferIndex_;

    {
        std::lock_guard<std::mutex> publishedLock(publishedMutex_);
        publishedBufferIndex_ = completedBufferIndex;
        ++publishedGeneration_;

        publishedLots_.clear();
        publishedLots_.reserve(lots_.size());
        std::size_t lotIndex = 0;
        for (; lotIndex < lots_.size(); ++lotIndex) {
            publishedLots_.push_back(lots_[lotIndex].buildRenderInstance());
        }
    }

    simulationReadBufferIndex_ = completedBufferIndex;
    simulationWriteBufferIndex_ = chooseNextWriteBuffer();
}

int SimulationRuntime::chooseNextWriteBuffer() const {
    int bufferIndex = 0;
    for (; bufferIndex < 3; ++bufferIndex) {
        if (bufferIndex == simulationReadBufferIndex_) {
            continue;
        }

        if (bufferUseCounts_[bufferIndex].load() == 0) {
            return bufferIndex;
        }
    }

    // Triple buffering should normally leave one safe write buffer free, but this keeps the ownership rule explicit.
    for (;;) {
        bufferIndex = 0;
        for (; bufferIndex < 3; ++bufferIndex) {
            if (bufferIndex == simulationReadBufferIndex_) {
                continue;
            }

            if (bufferUseCounts_[bufferIndex].load() == 0) {
                return bufferIndex;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool SimulationRuntime::tryPlaceLot(const LotDefinition& lotDefinition, int clickedTileX, int clickedTileY, std::vector<Tile>& writeTiles) {
    std::size_t offsetIndex = 0;
    for (; offsetIndex < lotDefinition.occupiedOffsets.size(); ++offsetIndex) {
        const Int2& occupiedOffset = lotDefinition.occupiedOffsets[offsetIndex];
        const int tileX = clickedTileX + occupiedOffset.x;
        const int tileY = clickedTileY + occupiedOffset.y;
        if (!isTileInsideMap(tileX, tileY)) {
            return false;
        }

        if (!writeTiles[tileIndex(tileX, tileY)].isVacant) {
            return false;
        }
    }

    lots_.push_back(Lot(clickedTileX, clickedTileY, lotDefinition, kMapWidth));
    const Lot& placedLot = lots_.back();

    const std::vector<int>& occupiedTileIndices = placedLot.occupiedTileIndices();
    std::size_t occupiedTileIndex = 0;
    for (; occupiedTileIndex < occupiedTileIndices.size(); ++occupiedTileIndex) {
        writeTiles[occupiedTileIndices[occupiedTileIndex]].isVacant = false;
    }

    return true;
}

bool SimulationRuntime::isTileInsideMap(int tileX, int tileY) const {
    return tileX >= 0 && tileX < kMapWidth && tileY >= 0 && tileY < kMapHeight;
}

int SimulationRuntime::tileIndex(int tileX, int tileY) const {
    return (tileY * kMapWidth) + tileX;
}
