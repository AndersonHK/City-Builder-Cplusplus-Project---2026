#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "AssetLoader.h"
#include "ChunkConfig.h"
#include "City.h"
#include "CityParameters.h"
#include "CommuteTypes.h"
#include "Lot.h"
#include "LotAutoLayoutResolver.h"
#include "LotModule.h"
#include "RendererPayload.h"
#include "Tile.h"
#include "TransportNetwork.h"

enum class GameSpeed {
    Paused,
    Play,
    Fast,
    FastForward
};

enum class CommuteCategory {
    None,
    Short,
    Medium,
    Long
};

struct RuntimeOptions {
    bool fastForward;
    bool detectL2CacheSize;
    std::size_t manualL2BytesPerLogicalThread;
    double usableL2Fraction;
    int mapWidth;
    int mapHeight;
    bool showNonFatalAssetWarningDialogs;
    std::function<void(const std::string&, const std::string&)> nonFatalAssetWarningHandler;

    // Defaults to fast simulation and detected cache sizing.
    RuntimeOptions()
        : fastForward(true),
          detectL2CacheSize(true),
          manualL2BytesPerLogicalThread(0),
          usableL2Fraction(0.75),
          mapWidth(1024),
          mapHeight(1024),
          showNonFatalAssetWarningDialogs(true),
          nonFatalAssetWarningHandler() {
    }
};

enum class PlayerCommandType {
    PaintPollution,
    PlaceLot,
    AddModuleAtTile,
    RemoveModuleAtTile,
    PlaceRoadStroke,
    BulldozeAtTile,
    BulldozeArea,
    ZoneArea,
    ZoneLot,
    ApplyRciPlan
};

struct PlayerCommand {
    PlayerCommandType type;
    int tileX;
    int tileY;
    int endTileX;
    int endTileY;
    int amount;
    int rotationSteps;
    std::uint16_t zoningType;
    std::string assetId;
    RciLot zoningLot;
    RciPlan rciPlan;
    RoadStrokeCommand roadStroke;

    // Starts as a no-op-ish pollution command until a queue helper fills it.
    PlayerCommand()
        : type(PlayerCommandType::PaintPollution),
          tileX(0),
          tileY(0),
          endTileX(0),
          endTileY(0),
          amount(0),
          rotationSteps(0),
          zoningType(TileZoningNone) {
    }
};

struct TileQueryResult {
    bool isValid;
    Tile tile;
    std::uint64_t generation;
    std::uint64_t lotRevision;
    std::uint64_t roadRevision;
    std::uint64_t commuteRevision;
    bool hasLot;
    int lotId;
    std::string lotAssetId;
    std::uint16_t lotZoningType;
    bool lotIsEmpty;
    bool hasRciLot;
    std::string rciName;
    std::uint16_t rciZoningType;
    std::string rciLandValueLevel;
    int rciCapacityCurrent;
    int rciCapacityMaximum;
    std::string moduleSummary;
    std::string parameterSummary;
    int commuteDemand;
    int commuteSatisfied;
    CommuteCategory worstCommuteCategory;
    int residentsLowWealthCurrent;
    int residentsLowWealthTotal;
    int jobsLowWealthCurrent;
    int jobsLowWealthTotal;
    std::string complaintSummary;
    std::vector<CommuteRouteSegment> commuteRouteSegments;
    std::vector<CommuteRouteSegment> roadCommuteSegments;
    std::vector<TransportLayerId> roadLayers;
    std::vector<ResolvedRoadCell> roads;

    // Defaults to an invalid query result.
    TileQueryResult()
        : isValid(false),
          generation(0),
          lotRevision(0),
          roadRevision(0),
          commuteRevision(0),
          hasLot(false),
          lotId(-1),
          lotZoningType(TileZoningNone),
          lotIsEmpty(false),
          hasRciLot(false),
          rciZoningType(TileZoningNone),
          rciCapacityCurrent(0),
          rciCapacityMaximum(0),
          commuteDemand(0),
          commuteSatisfied(0),
          worstCommuteCategory(CommuteCategory::None),
          residentsLowWealthCurrent(0),
          residentsLowWealthTotal(0),
          jobsLowWealthCurrent(0),
          jobsLowWealthTotal(0) {
    }
};

struct PublishedLotInfo {
    int lotId;
    std::string assetId;
    std::uint16_t zoningType;
    bool isEmpty;
    int minimumTileX;
    int minimumTileY;
    int footprintWidth;
    int footprintHeight;
    std::string moduleSummary;
    std::string parameterSummary;
    int commuteDemand;
    int commuteSatisfied;
    CommuteCategory worstCommuteCategory;
    int residentsLowWealthCurrent;
    int residentsLowWealthTotal;
    int jobsLowWealthCurrent;
    int jobsLowWealthTotal;
    int rciCapacityCurrent;
    int rciCapacityMaximum;
    std::string complaintSummary;
    std::vector<CommuteRouteSegment> commuteRouteSegments;

    // Defaults to an invalid published lot metadata record.
    PublishedLotInfo()
        : lotId(-1),
          zoningType(TileZoningNone),
          isEmpty(false),
          minimumTileX(0),
          minimumTileY(0),
          footprintWidth(0),
          footprintHeight(0),
          commuteDemand(0),
          commuteSatisfied(0),
          worstCommuteCategory(CommuteCategory::None),
          residentsLowWealthCurrent(0),
          residentsLowWealthTotal(0),
          jobsLowWealthCurrent(0),
          jobsLowWealthTotal(0),
          rciCapacityCurrent(0),
          rciCapacityMaximum(0) {
    }
};

struct PublishedWorldSnapshot {
    int bufferIndex;
    const std::vector<Tile>* tiles;
    const std::vector<LotRenderInstance>* lots;
    const std::vector<RciLot>* zoningLots;
    const std::vector<std::uint64_t>* chunkRevisions;
    const std::vector<int>* lotOccupancy;
    const std::vector<ResolvedRoadCell>* roads;
    const std::vector<std::uint8_t>* groundRoadRenderState;
    // One typed scalar/semantic payload per tile. It is never CPU-colored RGBA.
    const std::vector<RendererScalarPayload>* tileOverlayState;
    const std::vector<RenderMeshBinding>* renderMeshBindings;
    const std::vector<std::uint64_t>* groundRoadChunkRevisions;
    const std::vector<std::uint64_t>* elevatedRoadChunkRevisions;
    const std::vector<std::uint64_t>* tileOverlayChunkRevisions;
    int width;
    int height;
    std::uint64_t generation;
    std::uint64_t simulationTick;
    std::uint64_t lotRevision;
    std::uint64_t zoningLotRevision;
    std::uint64_t roadRevision;
    std::uint64_t overlayRevision;
    int population;

    // Defaults to an empty snapshot before acquirePublishedSnapshot fills it.
    PublishedWorldSnapshot()
        : bufferIndex(-1),
          tiles(0),
          lots(0),
          zoningLots(0),
          chunkRevisions(0),
          lotOccupancy(0),
          roads(0),
          groundRoadRenderState(0),
          tileOverlayState(0),
          renderMeshBindings(0),
          groundRoadChunkRevisions(0),
          elevatedRoadChunkRevisions(0),
          tileOverlayChunkRevisions(0),
          width(0),
          height(0),
          generation(0),
          simulationTick(0),
          lotRevision(0),
          zoningLotRevision(0),
          roadRevision(0),
          overlayRevision(0),
          population(0) {
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
    void setGameSpeed(GameSpeed gameSpeed);
    GameSpeed gameSpeed() const;

    void queuePaintPollution(int tileX, int tileY, int amount);
    void queuePlaceLot(const std::string& lotAssetId, int tileX, int tileY, int rotationSteps = 0);
    void queueAddModuleAtTile(const std::string& moduleAssetId, int tileX, int tileY);
    void queueRemoveModuleAtTile(int tileX, int tileY);
    void queueBulldozeAtTile(int tileX, int tileY);
    void queueBulldozeArea(int startTileX, int startTileY, int endTileX, int endTileY);
    void queueZoneArea(int startTileX, int startTileY, int endTileX, int endTileY, std::uint16_t zoningType);
    void queueZoneLot(const RciLot& zoningLot);
    void queueRciPlan(const RciPlan& plan);
    void queuePlaceRoadStroke(const RoadStrokeCommand& roadStrokeCommand);
    void queuePlaceSmokestack(int tileX, int tileY, int rotationSteps = 0);
    void queuePlacePark(int tileX, int tileY, int rotationSteps = 0);
    void queuePlaceFactory(int tileX, int tileY, int rotationSteps = 0);
    void queuePlaceHouse(int tileX, int tileY, int rotationSteps = 0);
    void queueAddSmokestackModule(int tileX, int tileY);
    void queueAddParkModule(int tileX, int tileY);
    void queuePlaceStreetRoad(const Int2& startTile, const Int2& cornerTile, const Int2& endTile);
    void queuePlaceHighwayRoad(const Int2& startTile, const Int2& cornerTile, const Int2& endTile);

    bool buildLotPreviewInstances(const std::string& lotAssetId, int tileX, int tileY, int rotationSteps, std::vector<LotRenderInstance>& renderInstances, bool& isPlacementValid) const;
    bool canPlaceRoadStroke(const RoadStrokeCommand& roadStrokeCommand) const;
    bool buildRciPlan(const RciTool& tool, int startTileX, int startTileY, int endTileX, int endTileY, RciPlanMode mode, RciPlan& plan) const;
    TileQueryResult queryTile(int tileX, int tileY) const;
    CitySaveState exportCitySaveState() const;
    void importCitySaveState(const CitySaveState& saveState, bool reportInvalidSavedLots = true);
    bool selectRciConstructorLotAssetForDiagnostics(
        std::uint16_t zoningType,
        const std::string& rciTypeId,
        int width,
        int height,
        float demandBudget,
        float maxDensityPerTile,
        std::uint8_t frontDirection,
        std::uint32_t variationSeed,
        std::string& lotAssetId,
        int& rotationSteps,
        int& capacity) const;

    PublishedWorldSnapshot acquirePublishedSnapshot();
    void releasePublishedSnapshot(const PublishedWorldSnapshot& snapshot);

    // Builds visible-chunk RCI desirability payloads for the selected RCI type.
    // The renderer shader owns the ramp and alpha; this returns capped scalars.
    bool fillRciDesirabilityOverlayChunkValues(const std::string& rciTypeId, const PublishedWorldSnapshot& snapshot, const ChunkRect& chunkRect, std::vector<RendererScalarPayload>& textureValues) const;

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
        std::vector<RciLot> publishedZoningLots;
        std::vector<CommuteRouteSegment> publishedCommuteRouteSegments;
        std::vector<std::uint64_t> chunkRevisions;
        std::vector<int> publishedLotOccupancy;
        std::vector<ResolvedRoadCell> publishedRoads;
        std::vector<std::uint8_t> publishedGroundRoadRenderState;
        std::vector<RendererScalarPayload> publishedTileOverlayState;
        std::vector<std::uint64_t> publishedGroundRoadChunkRevisions;
        std::vector<std::uint64_t> publishedElevatedRoadChunkRevisions;
        std::vector<std::uint64_t> publishedTileOverlayChunkRevisions;
        std::uint64_t lotRenderRevision;
        std::uint64_t zoningLotRenderRevision;
        std::uint64_t roadRenderRevision;
        std::uint64_t overlayRenderRevision;
        std::uint64_t commuteRenderRevision;

        // Starts with no published render payloads for this buffer.
        TileBuffer()
            : lotRenderRevision(0),
              zoningLotRenderRevision(0),
              roadRenderRevision(0),
              overlayRenderRevision(0),
              commuteRenderRevision(0) {
        }
    };

    struct RciDevelopmentSource {
        RciRect rect;
        std::string rciTypeId;
        bool isBuilt;
        std::size_t sourceIndex;
        int lotId;
        int capacity;
        std::uint8_t frontDirection;

        RciDevelopmentSource()
            : isBuilt(false),
              sourceIndex(0),
              lotId(-1),
              capacity(0),
              frontDirection(kRoadDirectionNorth) {
        }
    };

    struct RciConstructorSourceCursor {
        bool hasValue;
        bool isBuilt;
        int minTileX;
        int minTileY;
        int maxTileX;
        int maxTileY;
        int lotId;

        RciConstructorSourceCursor()
            : hasValue(false),
              isBuilt(false),
              minTileX(0),
              minTileY(0),
              maxTileX(0),
              maxTileY(0),
              lotId(-1) {
        }
    };

    struct RciConstructionCandidate {
        bool isValid;
        std::uint16_t zoningType;
        std::string rciTypeId;
        RciRect rect;
        const LotAsset* lotAsset;
        int rotationSteps;
        int capacity;
        float selectionCapacity;
        int consumedBuiltCapacity;
        int standaloneEmptyCapacity;
        int netGrowth;
        int desirability;
        std::vector<std::size_t> sourceIndices;
        std::vector<RciDevelopmentSource> consumedSources;
        Lot lot;

        RciConstructionCandidate()
            : isValid(false),
              zoningType(TileZoningNone),
              lotAsset(0),
              rotationSteps(0),
              capacity(0),
              selectionCapacity(0.0f),
              consumedBuiltCapacity(0),
              standaloneEmptyCapacity(0),
              netGrowth(0),
              desirability(0) {
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
    bool waitForTickPermission(GameSpeed& activeGameSpeed, std::chrono::steady_clock::time_point& nextPlayTick, bool& commandOnlyFrame);
    bool hasPendingCommands() const;
    void publishPausedCommandFrame();
    std::unique_lock<std::mutex> acquireLiveStateForPreviewValidation() const;
    void waitForPreviewValidationPriority() const;
    bool buildRciPlanWithLiveStateLock(const RciTool& tool, int startTileX, int startTileY, int endTileX, int endTileY, RciPlanMode mode, RciPlan& plan) const;
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
    void advanceLotConstruction(TileBuffer& writeBuffer);
    void applyLotEffects(std::vector<Tile>& writeTiles);
    void rebuildCityParameters(const TileBuffer& writeBuffer);
    void refreshCityPopulation();
    void runCommuteAssignment(const TileBuffer& writeBuffer);
    void queueCommuteRecalculationForLot(int lotId);
    void queueCommuteSourcesForDestination(int destinationLotId);
    void queueCommuteRecalculationForRoadTopologyChange(const std::vector<int>& dirtyTileIndices);
    void removeCommuteLoadsForLot(const Lot& lot);
    void runLocalTilePass(std::vector<Tile>& writeTiles);
    void enqueueCommand(const PlayerCommand& playerCommand);
    void publishCompletedBuffer();
    int chooseNextWriteBuffer();
    int findAvailableWriteBuffer() const;
    void refreshPublishedLotSnapshot(TileBuffer& completedBuffer);
    void refreshPublishedZoningLotSnapshot(TileBuffer& completedBuffer);
    void refreshPublishedRoadSnapshot(TileBuffer& completedBuffer);
    void copyChunkRevisionsForWriteBuffer();
    void markChunkDirtyByTile(std::vector<std::uint64_t>& chunkRevisions, int tileX, int tileY);
    void markChunksDirtyByTileIndices(std::vector<std::uint64_t>& chunkRevisions, const std::vector<int>& tileIndices);
    int chunkIndexForTile(int tileX, int tileY) const;

    const LotAsset* findLotAssetById(const std::string& lotAssetId) const;
    const LotModule* findModuleAssetById(const std::string& moduleAssetId) const;
    const LotModule* resolveModulePlacement(const LotAsset& lotAsset, const LotModulePlacementDefinition& placement, std::size_t placementIndex, int anchorTileX, int anchorTileY, int rotationSteps, int lotId) const;
    Lot* findLotById(int lotId);
    const PublishedLotInfo* findPublishedLotInfoById(const std::vector<PublishedLotInfo>& publishedLotInfos, int lotId) const;
    std::uint16_t zoningTypeForLotInBuffer(const Lot& lot, const TileBuffer& buffer, std::uint16_t fallbackZoningType) const;

    bool buildLotCandidate(const LotAsset& lotAsset, int clickedTileX, int clickedTileY, int rotationSteps, int lotId, Lot& candidateLot) const;
    bool buildLotCandidateForParcel(const LotAsset& lotAsset, int clickedTileX, int clickedTileY, int rotationSteps, int lotId, int parcelWidth, int parcelHeight, Lot& candidateLot) const;
    bool populateAutoLayoutLot(const LotAsset& lotAsset, int lotWidth, int lotHeight, int anchorTileX, int anchorTileY, int rotationSteps, int lotId, Lot& candidateLot, LotAutoPrimaryGeometry& primary) const;
    bool tryPlaceLot(const LotAsset& lotAsset, int clickedTileX, int clickedTileY, int rotationSteps, TileBuffer& writeBuffer);
    void runRciConstructor(TileBuffer& writeBuffer);
    bool tryConstructOneRciLot(std::uint16_t zoningType, const std::string& rciTypeId, TileBuffer& writeBuffer);
    std::string rciConstructorCursorKey(std::uint16_t zoningType, const std::string& rciTypeId) const;
    RciConstructorSourceCursor rciConstructorCursorForSource(const RciDevelopmentSource& source) const;
    bool rciConstructorCursorLess(const RciConstructorSourceCursor& left, const RciConstructorSourceCursor& right) const;
    std::size_t rciConstructorStartIndex(const RciConstructorSourceCursor& cursor, const std::vector<RciDevelopmentSource>& sources) const;
    bool tryConstructRciDevelopmentFromSource(std::uint16_t zoningType, const std::string& rciTypeId, std::size_t seedSourceIndex, const std::vector<RciDevelopmentSource>& sources, float demandBudget, TileBuffer& writeBuffer, int& constructedCapacity);
    std::vector<RciDevelopmentSource> collectRciDevelopmentSources(std::uint16_t zoningType, const std::string& rciTypeId) const;
    std::vector<std::size_t> buildRciDevelopmentBlock(std::size_t seedSourceIndex, const std::vector<RciDevelopmentSource>& sources) const;
    bool evaluateRciConstructionCandidate(std::uint16_t zoningType, const std::string& rciTypeId, const RciRect& candidateRect, std::size_t seedSourceIndex, const std::vector<std::size_t>& blockSourceIndices, const std::vector<RciDevelopmentSource>& sources, float demandBudget, const TileBuffer& writeBuffer, RciConstructionCandidate& candidate) const;
    bool commitRciConstructionCandidate(const RciConstructionCandidate& candidate, TileBuffer& writeBuffer);
    bool rciCandidateTilesAreDevelopable(std::uint16_t zoningType, const RciRect& rect, const std::vector<std::size_t>& consumedSourceIndices, const std::vector<RciDevelopmentSource>& sources, const TileBuffer& writeBuffer) const;
    const LotAsset* findRciConstructorLotAsset(std::uint16_t zoningType, const std::string& rciTypeId, int width, int height, float demandBudget, float maxDensityPerTile, std::uint8_t frontDirection, std::uint32_t variationSeed, int& rotationSteps, int& capacity) const;
    bool lotTemplateSupportsParcelSize(const LotAsset& lotAsset, int width, int depth, int rotationSteps) const;
    const RciTool* findRciToolByZoningType(std::uint16_t zoningType) const;
    bool hasRciConstructorLotAsset(std::uint16_t zoningType, int width, int height, std::uint8_t frontDirection) const;
    const RciGrowthRule* findRciGrowthRule(std::uint16_t zoningType) const;
    const RciDesirabilityRule* findRciDesirabilityRule(const std::string& rciTypeId) const;
    float rciMaxDensityPerTile(std::uint16_t zoningType) const;
    int averageLandValueForRect(const RciRect& rect, const TileBuffer& writeBuffer, bool& hasTiles) const;
    std::string rciLandValueLevelForLot(const PublishedLotInfo& publishedLotInfo, const LotAsset& lotAsset, const TileBuffer& writeBuffer) const;
    float rciLandValueDensityMultiplier(const RciRect& rect, const TileBuffer& writeBuffer) const;
    float rciLocalMaxDensityPerTile(std::uint16_t zoningType, const RciRect& rect, const TileBuffer& writeBuffer) const;
    int rciDesirabilityForTile(const std::string& rciTypeId, const Tile& tile) const;
    int rciDesirabilityForRect(const std::string& rciTypeId, const RciRect& rect, const TileBuffer& writeBuffer) const;
    int rciDesirabilityForCandidate(const Lot& lot, const LotAsset& lotAsset, const TileBuffer& writeBuffer) const;
    std::uint32_t rciVariationSeedForRect(std::uint16_t zoningType, const RciRect& rect) const;
    bool rciParcelTileIsAvailable(int tileX, int tileY, std::uint16_t zoningType, const TileBuffer& writeBuffer, const std::vector<std::uint8_t>& blockedTiles) const;
    bool rciParcelRectIsAvailable(const RciRect& rect, std::uint16_t zoningType, const TileBuffer& writeBuffer, const std::vector<std::uint8_t>& blockedTiles) const;
    void markRciParcelBlocked(const RciRect& rect, std::vector<std::uint8_t>& blockedTiles) const;
    bool tryAddRciParcel(const RciTool& tool, const RciRect& rect, std::uint8_t frontDirection, TileBuffer& writeBuffer, std::vector<std::uint8_t>& blockedTiles);
    int rciRoadFacingDepthAtTile(int tileX, int tileY, int deltaX, int deltaY, std::uint16_t zoningType, const TileBuffer& writeBuffer, const std::vector<std::uint8_t>& blockedTiles, int maximumDepth) const;
    RciRect mapRoadFacingRciLotRect(int roadFacingDirection, int frontageStartX, int frontageStartY, const RciRect& localRect) const;
    std::size_t parcelizeRoadFacingRciRun(const RciTool& tool, int roadFacingDirection, int frontageStartX, int frontageStartY, const std::vector<int>& frontageDepths, TileBuffer& writeBuffer, std::vector<std::uint8_t>& blockedTiles);
    std::size_t parcelizeRoadFacingRciTiles(std::uint16_t zoningType, const RciTool& tool, TileBuffer& writeBuffer, std::vector<std::uint8_t>& blockedTiles);
    std::size_t parcelizeRemainingRciTiles(std::uint16_t zoningType, const RciTool& tool, TileBuffer& writeBuffer, std::vector<std::uint8_t>& blockedTiles);
    std::size_t parcelizeUnparcelledRciTiles(std::uint16_t zoningType, TileBuffer& writeBuffer);
    std::size_t parcelizeAllUnparcelledRciTiles(TileBuffer& writeBuffer);
    float rciDemandForRciType(const std::string& rciTypeId) const;
    int rciPendingConstructionCapacity(const std::string& rciTypeId) const;
    int rciCapacityForLotAsset(const LotAsset& lotAsset, std::uint16_t zoningType) const;
    int rciCapacityForLotAsset(const LotAsset& lotAsset, std::uint16_t zoningType, int width, int height, int rotationSteps) const;
    int rciCapacityForLot(const Lot& lot, const std::string& rciTypeId) const;
    int rciDemandParameterId(const std::string& rciTypeId) const;
    int rciDemandParameterId(std::uint16_t zoningType) const;
    bool exposeRciLotForRedevelopment(std::size_t lotIndex, const LotAsset& lotAsset, TileBuffer& writeBuffer, std::uint64_t availableAfterTick);
    RciLot buildRedevelopmentRciLot(const Lot& lot, const LotAsset& lotAsset, std::uint16_t zoningType, std::uint64_t availableAfterTick) const;
    bool tryAddModuleAtTile(const LotModule& moduleAsset, int clickedTileX, int clickedTileY, TileBuffer& writeBuffer);
    bool tryRemoveModuleAtTile(int clickedTileX, int clickedTileY, TileBuffer& writeBuffer);
    bool tryBulldozeAtTile(int clickedTileX, int clickedTileY, TileBuffer& writeBuffer);
    bool tryBulldozeArea(int startTileX, int startTileY, int endTileX, int endTileY, TileBuffer& writeBuffer);
    bool tryZoneArea(int startTileX, int startTileY, int endTileX, int endTileY, std::uint16_t zoningType, TileBuffer& writeBuffer);
    bool tryClearZoningArea(int startTileX, int startTileY, int endTileX, int endTileY, TileBuffer& writeBuffer);
    bool tryZoneLot(const RciLot& zoningLot, TileBuffer& writeBuffer);
    bool tryApplyRciPlan(const RciPlan& plan, TileBuffer& writeBuffer);
    bool applyZoningRect(const RciRect& rect, std::uint16_t zoningType, TileBuffer& writeBuffer, std::vector<int>& changedTileIndices, bool& hasZoneableTile);
    bool isZoningRectFullyZoneable(const RciRect& rect, const TileBuffer& writeBuffer, bool requireUnoccupied) const;
    bool isTileZoneableForRci(int tileLinearIndex, const Tile& tile) const;
    bool tileHasBlockingNonRciLot(int tileLinearIndex) const;
    bool removeZoningLotsIntersectingRect(const RciRect& rect);
    void clearZoningForRoadStroke(const RoadStrokeCommand& roadStrokeCommand, TileBuffer& writeBuffer, bool parcelizeAfterClear = true);
    bool canPlaceLot(const Lot& candidateLot) const;
    bool collectAdjacentLotIdsForModule(const LotModule& moduleAsset, int clickedTileX, int clickedTileY, std::vector<int>& adjacentLotIds) const;
    void clearLotOccupancy(const std::vector<int>& tileIndices);
    void setLotOccupancy(int lotId, const std::vector<int>& tileIndices);
    int lotParameterAmount(const Lot& lot, int parameterId) const;
    int lotDerivedParameterAmount(const Lot& lot, int parameterId) const;
    int lotActualParameterAmount(const Lot& lot, const LotAsset* lotAsset, int parameterId, const TileBuffer& writeBuffer) const;
    int lotActualDerivedParameterAmount(const Lot& lot, const LotAsset* lotAsset, int parameterId, const TileBuffer& writeBuffer) const;
    void collectLotAccessNodes(const Lot& lot, const LotAsset& lotAsset, std::uint8_t allowedModeMask, std::vector<std::uint32_t>& accessNodes) const;
    std::vector<CommuteRouteSegment> buildCommuteRouteSegments(const TransportPathResult& pathResult, std::uint16_t demand, CommuteTimeOfDay timeOfDay) const;
    bool commuteRouteIsStillValid(const CommuteRouteRecord& route) const;
    bool commuteRouteTouchesTiles(const CommuteRouteRecord& route, const std::vector<int>& sortedTileIndices) const;
    bool commutePathTouchesTiles(const TransportPathResult& pathResult, const std::vector<int>& sortedTileIndices) const;
    bool lotAccessMayTouchTiles(const Lot& lot, const std::vector<int>& sortedTileIndices) const;
    bool isTileInsideMap(int tileX, int tileY) const;
    int tileIndex(int tileX, int tileY) const;

    static const int kMapWidth = 1024;
    static const int kMapHeight = 1024;
    static const int kMinimumJobsPerWorkerMultiplier = 8;
    static const int kAirPollutionSpreadRate = 16;
    static const int kParkEffectSpreadRate = 32;
    static const int kLandValueSpreadRate = 16;
    static const int kAirPollutionDecayRate = kAirPollutionSpreadRate * 640;
    static const int kParkEffectDecayRate = kParkEffectSpreadRate * 160;
    static const int kInvalidLotId = -1;

    RuntimeOptions runtimeOptions_;
    int mapWidth_;
    int mapHeight_;
    ChunkConfig chunkConfig_;
    // Guards mutable simulation state read outside the simulation thread, such
    // as placement previews and save/load export/import.
    mutable std::mutex liveStateMutex_;
    mutable std::mutex livePreviewValidationMutex_;
    mutable std::condition_variable livePreviewValidationCv_;
    mutable std::atomic<int> livePreviewValidationWaiters_;

    std::vector<TileBuffer> tileBuffers_;
    int simulationReadBufferIndex_;
    int simulationWriteBufferIndex_;

    mutable std::mutex publishedMutex_;
    int publishedBufferIndex_;
    std::uint64_t publishedGeneration_;
    std::uint64_t publishedSimulationTick_;
    int publishedPopulation_;

    std::atomic<int> bufferUseCounts_[3];
    std::atomic<std::uint64_t> lastRenderedGeneration_;
    std::condition_variable renderCv_;
    mutable std::mutex renderMutex_;

    std::vector<LotModule> moduleAssets_;
    std::vector<LotAsset> lotAssets_;
    std::vector<RenderMeshBinding> renderMeshBindings_;
    std::vector<RciGrowthRule> rciGrowthRules_;
    std::vector<RciDesirabilityRule> rciDesirabilityRules_;
    RciToolCatalog rciTools_;
    std::unordered_map<std::string, std::size_t> moduleAssetIndexById_;
    std::unordered_map<std::string, std::size_t> lotAssetIndexById_;
    std::vector<int> lotOccupancy_;
    std::vector<Lot> lots_;
    std::vector<RciLot> zoningLots_;
    int nextLotId_;
    std::uint64_t lotsRevision_;
    std::uint64_t zoningLotsRevision_;
    std::uint64_t simulationTick_;
    int cityPopulation_;
    CityParameterRegistry cityParameterRegistry_;
    std::vector<float> oldCityParameters_;
    std::vector<float> nextCityParameters_;
    std::vector<float> initialCityDemands_;
    std::vector<std::vector<float> > cityParameterDeltaBuffers_;
    int rciConstructorAttemptsPerTick_;
    float rciConstructorOverbuildMultiplier_;
    float rciConstructorMergeCapacityDiscount_;
    float rciConstructorRedevelopmentCapacityIncrease_;
    int rciBaselineLandValue_;
    std::unordered_map<std::string, RciConstructorSourceCursor> rciConstructorSourceCursors_;
    std::uint64_t commuteRevision_;
    bool commutesDirty_;
    std::vector<int> forcedCommuteLotIds_;
    std::size_t commuteRebalanceCursor_;
    // Simulation-thread-only A* scratch. Keeping it persistent lets the stamped
    // arrays in TransportPathScratch amortize their map-sized allocation.
    TransportPathScratch commutePathScratch_;
    TransportNetwork transportNetwork_;

    std::deque<PlayerCommand> pendingCommands_;
    mutable std::mutex commandMutex_;

    std::thread simulationThread_;
    std::atomic<bool> keepRunning_;
    std::atomic<GameSpeed> gameSpeed_;
    std::mutex speedMutex_;
    std::condition_variable speedCv_;

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
