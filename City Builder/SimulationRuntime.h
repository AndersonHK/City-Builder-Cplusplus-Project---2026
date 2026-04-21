#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
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
    const std::vector<LotRenderInstance>* lots;
    const std::vector<std::uint64_t>* chunkRevisions;
    int width;
    int height;
    std::uint64_t generation;
    std::uint64_t lotRevision;

    PublishedWorldSnapshot()
        : bufferIndex(-1),
          tiles(0),
          lots(0),
          chunkRevisions(0),
          width(0),
          height(0),
          generation(0),
          lotRevision(0) {
    }
};

struct RuntimeTimingSnapshot {
    long long neighborPassMicros;
    long long commandPassMicros;
    long long lotEffectsMicros;
    long long localPassMicros;
    long long publishMicros;
    long long writeBufferWaitMicros;

    RuntimeTimingSnapshot()
        : neighborPassMicros(0),
          commandPassMicros(0),
          lotEffectsMicros(0),
          localPassMicros(0),
          publishMicros(0),
          writeBufferWaitMicros(0) {
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
    const std::vector<ChunkRect>& chunkLayout() const;
    RuntimeTimingSnapshot timingSnapshot() const;

private:
    struct TileBuffer {
        std::vector<Tile> tiles;
        std::vector<LotRenderInstance> publishedLots;
        std::vector<std::uint64_t> chunkRevisions;
        std::uint64_t lotRenderRevision;

        TileBuffer()
            : lotRenderRevision(0) {
        }
    };

    enum class WorkerTaskType {
        None,
        NeighborPass,
        LocalPass
    };

    void initializeWorld();
    void simulationLoop();
    void startWorkers();
    void stopWorkers();
    void workerMain();
    void parallelForEachChunk(WorkerTaskType workerTask, const std::vector<Tile>* readTiles, std::vector<Tile>* writeTiles);
    void runPendingChunkTasks();
    void completeChunkTaskParticipant();
    void executeChunkTask(const ChunkRect& chunkRect);
    void runNeighborChunk(const ChunkRect& chunkRect);
    void runLocalChunk(const ChunkRect& chunkRect);

    void runNeighborPass(const std::vector<Tile>& readTiles, std::vector<Tile>& writeTiles);
    void applyQueuedCommands(TileBuffer& writeBuffer);
    void applyLotEffects(std::vector<Tile>& writeTiles);
    void runLocalTilePass(std::vector<Tile>& writeTiles);
    void publishCompletedBuffer();
    int chooseNextWriteBuffer();
    int findAvailableWriteBuffer() const;
    void refreshPublishedLotSnapshot(TileBuffer& completedBuffer);
    void copyChunkRevisionsForWriteBuffer();
    void markChunkDirtyByTile(std::vector<std::uint64_t>& chunkRevisions, int tileX, int tileY);
    int chunkIndexForTile(int tileX, int tileY) const;

    bool tryPlaceLot(const LotDefinition& lotDefinition, int clickedTileX, int clickedTileY, TileBuffer& writeBuffer);
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
    std::uint64_t publishedGeneration_;

    std::atomic<int> bufferUseCounts_[3];
    std::atomic<std::uint64_t> lastRenderedGeneration_;
    std::condition_variable renderCv_;
    mutable std::mutex renderMutex_;

    std::vector<Lot> lots_;
    std::uint64_t lotsRevision_;

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
    std::vector<ChunkRect> chunkLayout_;
    std::atomic<std::size_t> nextChunkIndex_;
    std::atomic<std::size_t> workersRemaining_;
    bool chunkTaskReady_;
    bool stopWorkerThreads_;
    std::uint64_t chunkTaskGeneration_;
    WorkerTaskType currentWorkerTask_;
    const std::vector<Tile>* currentReadTiles_;
    std::vector<Tile>* currentWriteTiles_;

    std::atomic<int> updatesPerSecond_;
    std::atomic<long long> neighborPassMicros_;
    std::atomic<long long> commandPassMicros_;
    std::atomic<long long> lotEffectsMicros_;
    std::atomic<long long> localPassMicros_;
    std::atomic<long long> publishMicros_;
    std::atomic<long long> writeBufferWaitMicros_;
};
