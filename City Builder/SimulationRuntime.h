#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "ChunkConfig.h"
#include "Lot.h"
#include "LotModule.h"
#include "Tile.h"

struct RuntimeOptions {
    bool fastForward;
    bool detectL2CacheSize;
    std::size_t manualL2BytesPerLogicalThread;
    double usableL2Fraction;

    RuntimeOptions()
        : fastForward(true),
          detectL2CacheSize(true),
          manualL2BytesPerLogicalThread(0),
          usableL2Fraction(0.75) {
    }
};

enum class PlayerCommandType {
    PaintPollution,
    PlaceSmokestack,
    PlacePark
};

struct PlayerCommand {
    PlayerCommandType type;
    int tileX;
    int tileY;
    int amount;

    PlayerCommand()
        : type(PlayerCommandType::PaintPollution),
          tileX(0),
          tileY(0),
          amount(0) {
    }
};

struct TileQueryResult {
    bool isValid;
    Tile tile;
    std::uint64_t generation;

    TileQueryResult()
        : isValid(false),
          generation(0) {
    }
};

struct PublishedWorldSnapshot {
    int bufferIndex;
    const std::vector<Tile>* tiles;
    int width;
    int height;
    std::vector<LotRenderInstance> lots;
    std::uint64_t generation;

    PublishedWorldSnapshot()
        : bufferIndex(-1),
          tiles(0),
          width(0),
          height(0),
          generation(0) {
    }
};

class SimulationRuntime {
public:
    explicit SimulationRuntime(const RuntimeOptions& runtimeOptions);
    ~SimulationRuntime();

    void start();
    void stop();

    void queuePaintPollution(int tileX, int tileY, int amount);
    void queuePlaceSmokestack(int tileX, int tileY);
    void queuePlacePark(int tileX, int tileY);

    TileQueryResult queryTile(int tileX, int tileY) const;

    PublishedWorldSnapshot acquirePublishedSnapshot();
    void releasePublishedSnapshot(const PublishedWorldSnapshot& snapshot);

    int mapWidth() const;
    int mapHeight() const;
    int updatesPerSecond() const;
    const ChunkConfig& chunkConfig() const;

private:
    struct TileBuffer {
        std::vector<Tile> tiles;
    };

    void initializeWorld();
    void simulationLoop();
    void startWorkers();
    void stopWorkers();
    void workerMain();
    void parallelForEachChunk(const std::function<void(const ChunkRect&)>& chunkTask);

    void runNeighborPass(const std::vector<Tile>& readTiles, std::vector<Tile>& writeTiles);
    void applyQueuedCommands(std::vector<Tile>& writeTiles);
    void applyLotEffects(std::vector<Tile>& writeTiles);
    void runLocalTilePass(std::vector<Tile>& writeTiles);
    void publishCompletedBuffer();
    int chooseNextWriteBuffer() const;

    bool tryPlaceLot(const LotDefinition& lotDefinition, int clickedTileX, int clickedTileY, std::vector<Tile>& writeTiles);
    bool isTileInsideMap(int tileX, int tileY) const;
    int tileIndex(int tileX, int tileY) const;

    static const int kMapWidth = 1024;
    static const int kMapHeight = 1024;
    static const int kMinimumJobsPerWorkerMultiplier = 8;
    static const int kPollutionSpreadRate = 4;

    RuntimeOptions runtimeOptions_;
    ChunkConfig chunkConfig_;

    std::vector<TileBuffer> tileBuffers_;
    int simulationReadBufferIndex_;
    int simulationWriteBufferIndex_;

    mutable std::mutex publishedMutex_;
    int publishedBufferIndex_;
    std::vector<LotRenderInstance> publishedLots_;
    std::uint64_t publishedGeneration_;

    std::atomic<int> bufferUseCounts_[3];
    std::atomic<std::uint64_t> lastRenderedGeneration_;
    std::condition_variable renderCv_;
    mutable std::mutex renderMutex_;

    std::vector<Lot> lots_;

    std::deque<PlayerCommand> pendingCommands_;
    mutable std::mutex commandMutex_;

    LotModule smokeStackModule_;
    LotModule parkModule_;
    LotDefinition smokeStackDefinition_;
    LotDefinition parkDefinition_;

    std::thread simulationThread_;
    std::atomic<bool> keepRunning_;

    std::vector<std::thread> workerThreads_;
    std::mutex workerMutex_;
    std::condition_variable workerCv_;
    std::condition_variable workerFinishedCv_;
    std::function<void(const ChunkRect&)> currentChunkTask_;
    std::vector<ChunkRect> chunkLayout_;
    std::size_t nextChunkIndex_;
    std::size_t workersRemaining_;
    bool chunkTaskReady_;
    bool stopWorkerThreads_;
    std::uint64_t chunkTaskGeneration_;

    std::atomic<int> updatesPerSecond_;
};
