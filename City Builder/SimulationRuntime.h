#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "ChunkConfig.h"
#include "CityParameters.h"
#include "CommuteTypes.h"
#include "Lot.h"
#include "LotModule.h"
#include "Tile.h"
#include "TransportNetwork.h"

struct RuntimeOptions {
    bool fastForward;
    bool detectL2CacheSize;
    std::size_t manualL2BytesPerLogicalThread;
    double usableL2Fraction;

    // Defaults to fast simulation and detected cache sizing.
    RuntimeOptions()
        : fastForward(true),
          detectL2CacheSize(true),
          manualL2BytesPerLogicalThread(0),
          usableL2Fraction(0.75) {
    }
};

enum class PlayerCommandType {
    PaintPollution,
    PlaceLot,
    AddModuleAtTile,
    RemoveModuleAtTile,
    PlaceRoadStroke
};

struct PlayerCommand {
    PlayerCommandType type;
    int tileX;
    int tileY;
    int amount;
    std::string assetId;
    RoadStrokeCommand roadStroke;

    // Starts as a no-op-ish pollution command until a queue helper fills it.
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
    bool hasLot;
    int lotId;
    std::string lotAssetId;
    std::string moduleSummary;
    std::string parameterSummary;
    int commuteDemand;
    int commuteSatisfied;
    std::vector<CommuteRouteSegment> commuteRouteSegments;
    std::vector<TransportLayerId> roadLayers;
    std::vector<ResolvedRoadCell> roads;

    // Defaults to an invalid query result.
    TileQueryResult()
        : isValid(false),
          generation(0),
          hasLot(false),
          lotId(-1),
          commuteDemand(0),
          commuteSatisfied(0) {
    }
};

struct PublishedLotInfo {
    int lotId;
    std::string assetId;
    std::string moduleSummary;
    std::string parameterSummary;
    int commuteDemand;
    int commuteSatisfied;
    std::vector<CommuteRouteSegment> commuteRouteSegments;

    // Defaults to an invalid published lot metadata record.
    PublishedLotInfo()
        : lotId(-1),
          commuteDemand(0),
          commuteSatisfied(0) {
    }
};

struct PublishedWorldSnapshot {
    int bufferIndex;
    const std::vector<Tile>* tiles;
    const std::vector<LotRenderInstance>* lots;
    const std::vector<std::uint64_t>* chunkRevisions;
    const std::vector<int>* lotOccupancy;
    const std::vector<ResolvedRoadCell>* roads;
    const std::vector<std::uint8_t>* groundRoadRenderState;
    const std::vector<std::uint8_t>* tileOverlayState;
    const std::vector<std::uint64_t>* groundRoadChunkRevisions;
    const std::vector<std::uint64_t>* elevatedRoadChunkRevisions;
    const std::vector<std::uint64_t>* tileOverlayChunkRevisions;
    int width;
    int height;
    std::uint64_t generation;
    std::uint64_t lotRevision;
    std::uint64_t roadRevision;
    std::uint64_t overlayRevision;

    // Defaults to an empty snapshot before acquirePublishedSnapshot fills it.
    PublishedWorldSnapshot()
        : bufferIndex(-1),
          tiles(0),
          lots(0),
          chunkRevisions(0),
          lotOccupancy(0),
          roads(0),
          groundRoadRenderState(0),
          tileOverlayState(0),
          groundRoadChunkRevisions(0),
          elevatedRoadChunkRevisions(0),
          tileOverlayChunkRevisions(0),
          width(0),
          height(0),
          generation(0),
          lotRevision(0),
          roadRevision(0),
          overlayRevision(0) {
    }
};

struct RuntimeTimingSnapshot {
    long long neighborPassMicros;
    long long commandPassMicros;
    long long lotEffectsMicros;
    long long localPassMicros;
    long long publishMicros;
    long long writeBufferWaitMicros;

    // Initializes all timing counters to zero.
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
    void queuePlaceLot(const std::string& lotAssetId, int tileX, int tileY);
    void queueAddModuleAtTile(const std::string& moduleAssetId, int tileX, int tileY);
    void queueRemoveModuleAtTile(int tileX, int tileY);
    void queuePlaceRoadStroke(const RoadStrokeCommand& roadStrokeCommand);
    void queuePlaceSmokestack(int tileX, int tileY);
    void queuePlacePark(int tileX, int tileY);
    void queuePlaceFactory(int tileX, int tileY);
    void queuePlaceHouse(int tileX, int tileY);
    void queueAddSmokestackModule(int tileX, int tileY);
    void queueAddParkModule(int tileX, int tileY);
    void queuePlaceStreetRoad(const Int2& startTile, const Int2& cornerTile, const Int2& endTile);
    void queuePlaceHighwayRoad(const Int2& startTile, const Int2& cornerTile, const Int2& endTile);

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
        std::vector<PublishedLotInfo> publishedLotInfos;
        std::vector<std::uint64_t> chunkRevisions;
        std::vector<int> publishedLotOccupancy;
        std::vector<ResolvedRoadCell> publishedRoads;
        std::vector<std::uint8_t> publishedGroundRoadRenderState;
        std::vector<std::uint8_t> publishedTileOverlayState;
        std::vector<std::uint64_t> publishedGroundRoadChunkRevisions;
        std::vector<std::uint64_t> publishedElevatedRoadChunkRevisions;
        std::vector<std::uint64_t> publishedTileOverlayChunkRevisions;
        std::uint64_t lotRenderRevision;
        std::uint64_t roadRenderRevision;
        std::uint64_t overlayRenderRevision;
        std::uint64_t commuteRenderRevision;

        // Starts with no published render payloads for this buffer.
        TileBuffer()
            : lotRenderRevision(0),
              roadRenderRevision(0),
              overlayRenderRevision(0),
              commuteRenderRevision(0) {
        }
    };

    enum class WorkerTaskType {
        None,
        NeighborPass,
        LocalPass
    };

    void loadAssets();
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
    void rebuildCityParameters();
    void runCommuteAssignment();
    void runLocalTilePass(std::vector<Tile>& writeTiles);
    void enqueueCommand(const PlayerCommand& playerCommand);
    void publishCompletedBuffer();
    int chooseNextWriteBuffer();
    int findAvailableWriteBuffer() const;
    void refreshPublishedLotSnapshot(TileBuffer& completedBuffer);
    void refreshPublishedRoadSnapshot(TileBuffer& completedBuffer);
    void copyChunkRevisionsForWriteBuffer();
    void markChunkDirtyByTile(std::vector<std::uint64_t>& chunkRevisions, int tileX, int tileY);
    void markChunksDirtyByTileIndices(std::vector<std::uint64_t>& chunkRevisions, const std::vector<int>& tileIndices);
    int chunkIndexForTile(int tileX, int tileY) const;

    const LotAsset* findLotAssetById(const std::string& lotAssetId) const;
    const LotModule* findModuleAssetById(const std::string& moduleAssetId) const;
    Lot* findLotById(int lotId);
    const PublishedLotInfo* findPublishedLotInfoById(const std::vector<PublishedLotInfo>& publishedLotInfos, int lotId) const;

    bool tryPlaceLot(const LotAsset& lotAsset, int clickedTileX, int clickedTileY, TileBuffer& writeBuffer);
    bool tryAddModuleAtTile(const LotModule& moduleAsset, int clickedTileX, int clickedTileY, TileBuffer& writeBuffer);
    bool tryRemoveModuleAtTile(int clickedTileX, int clickedTileY, TileBuffer& writeBuffer);
    bool canPlaceLot(const Lot& candidateLot) const;
    bool collectAdjacentLotIdsForModule(const LotModule& moduleAsset, int clickedTileX, int clickedTileY, std::vector<int>& adjacentLotIds) const;
    void clearLotOccupancy(const std::vector<int>& tileIndices);
    void setLotOccupancy(int lotId, const std::vector<int>& tileIndices);
    float lotParameterAmount(const Lot& lot, int parameterId) const;
    float lotDerivedParameterAmount(const Lot& lot, int parameterId) const;
    std::vector<CommuteRouteSegment> buildCommuteRouteSegments(const TransportPathResult& pathResult, std::uint16_t demand) const;
    bool isTileInsideMap(int tileX, int tileY) const;
    int tileIndex(int tileX, int tileY) const;

    static const int kMapWidth = 1024;
    static const int kMapHeight = 1024;
    static const int kMinimumJobsPerWorkerMultiplier = 8;
    static const int kPollutionSpreadRate = 4;
    static const int kInvalidLotId = -1;

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

    std::vector<LotModule> moduleAssets_;
    std::vector<LotAsset> lotAssets_;
    std::unordered_map<std::string, std::size_t> moduleAssetIndexById_;
    std::unordered_map<std::string, std::size_t> lotAssetIndexById_;
    std::vector<int> lotOccupancy_;
    std::vector<Lot> lots_;
    int nextLotId_;
    std::uint64_t lotsRevision_;
    CityParameterRegistry cityParameterRegistry_;
    std::vector<float> oldCityParameters_;
    std::vector<float> nextCityParameters_;
    std::vector<std::vector<float> > cityParameterDeltaBuffers_;
    std::uint64_t commuteRevision_;
    bool commutesDirty_;
    TransportNetwork transportNetwork_;

    std::deque<PlayerCommand> pendingCommands_;
    mutable std::mutex commandMutex_;

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
