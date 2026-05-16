#include "SimulationRuntime.h"

#include "AssetLoader.h"
#include "CrashLogger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
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

const float kTransportCostUnitsPerCommuteTime = 1000.0f;
const float kMaximumCommuteTime = 300.0f;
const float kMaximumCommuteCost = kMaximumCommuteTime * kTransportCostUnitsPerCommuteTime;
const float kLongCommuteComplaintCost = kMaximumCommuteCost * 0.5f;
const int kRciRedevelopmentGraceTicks = 30;

const char* RciNameForZoningType(std::uint16_t zoningType) {
    if (zoningType == TileZoningResidential) {
        return "Residence";
    }
    if (zoningType == TileZoningIndustrial) {
        return "Industry";
    }
    return "Unzoned";
}

const char* RciToolIdForZoningType(std::uint16_t zoningType) {
    if (zoningType == TileZoningResidential) {
        return "residential";
    }
    if (zoningType == TileZoningIndustrial) {
        return "industrial";
    }
    return "rci";
}

RciColor RciColorForZoningType(std::uint16_t zoningType) {
    if (zoningType == TileZoningResidential) {
        return RciColor(0.18f, 0.86f, 0.32f, 0.50f);
    }
    if (zoningType == TileZoningIndustrial) {
        return RciColor(0.92f, 0.76f, 0.15f, 0.50f);
    }
    return RciColor(0.45f, 0.45f, 0.45f, 0.50f);
}

bool CommuteRouteSegmentTouchesTile(const CommuteRouteSegment& segment, int tileX, int tileY) {
    const int minTileX = std::min(segment.startTileX, segment.endTileX);
    const int maxTileX = std::max(segment.startTileX, segment.endTileX);
    const int minTileY = std::min(segment.startTileY, segment.endTileY);
    const int maxTileY = std::max(segment.startTileY, segment.endTileY);
    return tileX >= minTileX && tileX <= maxTileX &&
        tileY >= minTileY && tileY <= maxTileY;
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

void RotatedRectangleDimensions(const Int2& localOrigin, int width, int height, int rotationSteps, int& rotatedWidth, int& rotatedHeight) {
    Int2 minimum;
    Int2 maximum;
    RotatedRectangleBounds(localOrigin, width, height, rotationSteps, minimum, maximum);
    rotatedWidth = maximum.x - minimum.x + 1;
    rotatedHeight = maximum.y - minimum.y + 1;
}
}

// Allocates the triple-buffered world and derives chunk/work scheduling config.
SimulationRuntime::SimulationRuntime(const RuntimeOptions& runtimeOptions)
    : simulationReadBufferIndex_(0),
      simulationWriteBufferIndex_(1),
      publishedBufferIndex_(0),
      publishedGeneration_(0),
      publishedSimulationTick_(0),
      publishedPopulation_(0),
      nextLotId_(1),
      lotsRevision_(0),
      zoningLotsRevision_(0),
      simulationTick_(0),
      cityPopulation_(0),
      rciConstructorAttemptsPerTick_(5),
      rciConstructorOverbuildMultiplier_(1.2f),
      commuteRevision_(0),
      commutesDirty_(true),
      commuteRebalanceCursor_(0),
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
        tileBuffers_[bufferIndex].publishedZoningLots.clear();
        tileBuffers_[bufferIndex].publishedCommuteRouteSegments.clear();
        tileBuffers_[bufferIndex].publishedLotOccupancy.assign(totalTileCount, kInvalidLotId);
        tileBuffers_[bufferIndex].publishedRoads.assign(totalTileCount * TransportNetwork::layerCount(), ResolvedRoadCell());
        tileBuffers_[bufferIndex].publishedGroundRoadRenderState.assign(totalTileCount * kGroundRoadRenderChannelsPerTile, 0);
        tileBuffers_[bufferIndex].publishedTileOverlayState.assign(totalTileCount * 4u, 0);
        tileBuffers_[bufferIndex].publishedGroundRoadChunkRevisions.assign(chunkCount, 1);
        tileBuffers_[bufferIndex].publishedElevatedRoadChunkRevisions.assign(chunkCount, 1);
        tileBuffers_[bufferIndex].publishedTileOverlayChunkRevisions.assign(chunkCount, 1);
        tileBuffers_[bufferIndex].lotRenderRevision = 0;
        tileBuffers_[bufferIndex].zoningLotRenderRevision = 0;
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

// Queues building destruction at the clicked tile; zoning and roads use separate commands.
void SimulationRuntime::queueBulldozeAtTile(int tileX, int tileY) {
    if (!isTileInsideMap(tileX, tileY)) {
        return;
    }

    PlayerCommand playerCommand;
    playerCommand.type = PlayerCommandType::BulldozeAtTile;
    playerCommand.tileX = tileX;
    playerCommand.tileY = tileY;
    enqueueCommand(playerCommand);
}

void SimulationRuntime::queueBulldozeArea(int startTileX, int startTileY, int endTileX, int endTileY) {
    if (!isTileInsideMap(startTileX, startTileY) && !isTileInsideMap(endTileX, endTileY)) {
        return;
    }

    PlayerCommand playerCommand;
    playerCommand.type = PlayerCommandType::BulldozeArea;
    playerCommand.tileX = std::max(0, std::min(startTileX, kMapWidth - 1));
    playerCommand.tileY = std::max(0, std::min(startTileY, kMapHeight - 1));
    playerCommand.endTileX = std::max(0, std::min(endTileX, kMapWidth - 1));
    playerCommand.endTileY = std::max(0, std::min(endTileY, kMapHeight - 1));
    enqueueCommand(playerCommand);
}

void SimulationRuntime::queueZoneArea(int startTileX, int startTileY, int endTileX, int endTileY, std::uint16_t zoningType) {
    if (zoningType == TileZoningNone) {
        return;
    }

    if (!isTileInsideMap(startTileX, startTileY) && !isTileInsideMap(endTileX, endTileY)) {
        return;
    }

    PlayerCommand playerCommand;
    playerCommand.type = PlayerCommandType::ZoneArea;
    playerCommand.tileX = std::max(0, std::min(startTileX, kMapWidth - 1));
    playerCommand.tileY = std::max(0, std::min(startTileY, kMapHeight - 1));
    playerCommand.endTileX = std::max(0, std::min(endTileX, kMapWidth - 1));
    playerCommand.endTileY = std::max(0, std::min(endTileY, kMapHeight - 1));
    playerCommand.zoningType = zoningType;
    enqueueCommand(playerCommand);
}

void SimulationRuntime::queueZoneLot(const RciLot& zoningLot) {
    if (zoningLot.zoningType == TileZoningNone || !zoningLot.rect.isValid()) {
        return;
    }

    if (!isTileInsideMap(zoningLot.rect.minTileX, zoningLot.rect.minTileY) &&
        !isTileInsideMap(zoningLot.rect.maxTileX, zoningLot.rect.maxTileY)) {
        return;
    }

    PlayerCommand playerCommand;
    playerCommand.type = PlayerCommandType::ZoneLot;
    playerCommand.tileX = std::max(0, std::min(zoningLot.rect.minTileX, kMapWidth - 1));
    playerCommand.tileY = std::max(0, std::min(zoningLot.rect.minTileY, kMapHeight - 1));
    playerCommand.endTileX = std::max(0, std::min(zoningLot.rect.maxTileX, kMapWidth - 1));
    playerCommand.endTileY = std::max(0, std::min(zoningLot.rect.maxTileY, kMapHeight - 1));
    playerCommand.zoningLot = zoningLot;
    playerCommand.zoningLot.rect = RciRect(playerCommand.tileX, playerCommand.tileY, playerCommand.endTileX, playerCommand.endTileY);
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

// Builds renderer-facing lot preview instances without mutating simulation state.
bool SimulationRuntime::buildLotPreviewInstances(const std::string& lotAssetId, int tileX, int tileY, int rotationSteps, std::vector<LotRenderInstance>& renderInstances, bool& isPlacementValid) const {
    renderInstances.clear();
    isPlacementValid = false;
    const LotAsset* lotAsset = findLotAssetById(lotAssetId);
    if (lotAsset == 0) {
        return false;
    }

    Lot candidateLot;
    if (!buildLotCandidate(*lotAsset, tileX, tileY, rotationSteps, -1, candidateLot)) {
        return false;
    }

    candidateLot.buildRenderInstances(renderInstances);

    const int minimumX = candidateLot.minimumTileX();
    const int minimumY = candidateLot.minimumTileY();
    const int maximumX = minimumX + candidateLot.footprintWidth() - 1;
    const int maximumY = minimumY + candidateLot.footprintHeight() - 1;
    isPlacementValid = isTileInsideMap(minimumX, minimumY) &&
        isTileInsideMap(maximumX, maximumY) &&
        canPlaceLot(candidateLot);

    return true;
}

bool SimulationRuntime::canPlaceRoadStroke(const RoadStrokeCommand& roadStrokeCommand) const {
    if (!isTileInsideMap(roadStrokeCommand.startTile.x, roadStrokeCommand.startTile.y) ||
        !isTileInsideMap(roadStrokeCommand.cornerTile.x, roadStrokeCommand.cornerTile.y) ||
        !isTileInsideMap(roadStrokeCommand.endTile.x, roadStrokeCommand.endTile.y)) {
        return false;
    }

    return transportNetwork_.canPlaceRoadStroke(roadStrokeCommand, lotOccupancy_, kInvalidLotId);
}

// Reads the currently published snapshot for debug tile inspection.
TileQueryResult SimulationRuntime::queryTile(int tileX, int tileY) const {
    TileQueryResult queryResult;
    if (!isTileInsideMap(tileX, tileY)) {
        return queryResult;
    }

    std::lock_guard<std::mutex> publishedLock(publishedMutex_);
    const TileBuffer& publishedBuffer = tileBuffers_[publishedBufferIndex_];
    const int queriedTileIndex = tileIndex(tileX, tileY);
    queryResult.isValid = true;
    queryResult.tile = publishedBuffer.tiles[queriedTileIndex];
    queryResult.generation = publishedGeneration_;
    queryResult.lotRevision = publishedBuffer.lotRenderRevision;
    queryResult.roadRevision = publishedBuffer.roadRenderRevision;
    queryResult.commuteRevision = publishedBuffer.commuteRenderRevision;

    if (!publishedBuffer.publishedLotOccupancy.empty()) {
        const int lotId = publishedBuffer.publishedLotOccupancy[queriedTileIndex];
        if (lotId != kInvalidLotId) {
            queryResult.hasLot = true;
            queryResult.lotId = lotId;

            const PublishedLotInfo* publishedLotInfo = findPublishedLotInfoById(publishedBuffer.publishedLotInfos, lotId);
            if (publishedLotInfo != 0) {
                queryResult.lotAssetId = publishedLotInfo->assetId;
                queryResult.lotZoningType = publishedLotInfo->zoningType;
                queryResult.lotIsEmpty = publishedLotInfo->isEmpty;
                if (publishedLotInfo->isEmpty && publishedLotInfo->zoningType != TileZoningNone) {
                    queryResult.hasRciLot = true;
                    queryResult.rciName = RciNameForZoningType(publishedLotInfo->zoningType);
                    queryResult.rciZoningType = publishedLotInfo->zoningType;
                }
                queryResult.moduleSummary = publishedLotInfo->moduleSummary;
                queryResult.parameterSummary = publishedLotInfo->parameterSummary;
                queryResult.commuteDemand = publishedLotInfo->commuteDemand;
                queryResult.commuteSatisfied = publishedLotInfo->commuteSatisfied;
                queryResult.residentsLowWealthCurrent = publishedLotInfo->residentsLowWealthCurrent;
                queryResult.residentsLowWealthTotal = publishedLotInfo->residentsLowWealthTotal;
                queryResult.jobsLowWealthCurrent = publishedLotInfo->jobsLowWealthCurrent;
                queryResult.jobsLowWealthTotal = publishedLotInfo->jobsLowWealthTotal;
                queryResult.complaintSummary = publishedLotInfo->complaintSummary;
                queryResult.commuteRouteSegments = publishedLotInfo->commuteRouteSegments;
            }
        }
    }

    if (!queryResult.hasLot && !publishedBuffer.publishedZoningLots.empty()) {
        std::size_t zoningLotIndex = 0;
        for (; zoningLotIndex < publishedBuffer.publishedZoningLots.size(); ++zoningLotIndex) {
            const RciLot& zoningLot = publishedBuffer.publishedZoningLots[zoningLotIndex];
            if (tileX < zoningLot.rect.minTileX || tileX > zoningLot.rect.maxTileX ||
                tileY < zoningLot.rect.minTileY || tileY > zoningLot.rect.maxTileY) {
                continue;
            }

            queryResult.hasRciLot = true;
            queryResult.rciName = zoningLot.name.empty() ? RciNameForZoningType(zoningLot.zoningType) : zoningLot.name;
            queryResult.rciZoningType = zoningLot.zoningType;
            break;
        }
    }

    if (!queryResult.hasLot && !queryResult.hasRciLot && queryResult.tile.zoningType != TileZoningNone) {
        queryResult.hasRciLot = true;
        queryResult.rciName = RciNameForZoningType(queryResult.tile.zoningType);
        queryResult.rciZoningType = queryResult.tile.zoningType;
    }

    if (!publishedBuffer.publishedRoads.empty()) {
        std::size_t layerIndex = 0;
        for (; layerIndex < TransportNetwork::layerCount(); ++layerIndex) {
            const std::size_t slot = TransportNetwork::slotIndex(static_cast<TransportLayerId>(layerIndex), queriedTileIndex, static_cast<std::size_t>(kMapWidth) * static_cast<std::size_t>(kMapHeight));
            const ResolvedRoadCell& roadCell = publishedBuffer.publishedRoads[slot];
            if (roadCell.family == static_cast<std::uint8_t>(RoadFamily::None)) {
                continue;
            }

            queryResult.roadLayers.push_back(static_cast<TransportLayerId>(layerIndex));
            queryResult.roads.push_back(roadCell);
        }
    }

    if (!queryResult.roads.empty()) {
        std::size_t segmentIndex = 0;
        for (; segmentIndex < publishedBuffer.publishedCommuteRouteSegments.size(); ++segmentIndex) {
            const CommuteRouteSegment& segment = publishedBuffer.publishedCommuteRouteSegments[segmentIndex];
            if (CommuteRouteSegmentTouchesTile(segment, tileX, tileY)) {
                queryResult.roadCommuteSegments.push_back(segment);
            }
        }
    }

    return queryResult;
}

CitySaveState SimulationRuntime::exportCitySaveState() const {
    CitySaveState saveState;
    saveState.width = kMapWidth;
    saveState.height = kMapHeight;
    saveState.nextLotId = nextLotId_;
    saveState.simulationTick = simulationTick_;
    saveState.tiles = tileBuffers_[simulationReadBufferIndex_].tiles;
    saveState.zoningLots = zoningLots_;
    saveState.cityParameters = oldCityParameters_;
    saveState.transport = transportNetwork_.exportSaveState();

    saveState.lots.reserve(lots_.size());
    saveState.previewLots.reserve(lots_.size() * 4u);
    std::size_t lotIndex = 0;
    for (; lotIndex < lots_.size(); ++lotIndex) {
        const Lot& lot = lots_[lotIndex];
        CitySaveLotState lotSaveState;
        lotSaveState.lotId = lot.id();
        lotSaveState.assetId = lot.assetId();
        lotSaveState.anchorTileX = lot.anchorTileX();
        lotSaveState.anchorTileY = lot.anchorTileY();
        lotSaveState.rotationSteps = lot.rotationSteps();
        lotSaveState.constructionTotalTicks = lot.constructionTotalTicks();
        lotSaveState.constructionRemainingTicks = lot.constructionRemainingTicks();

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
        lot.buildRenderInstances(saveState.previewLots);
    }

    return saveState;
}

void SimulationRuntime::importCitySaveState(const CitySaveState& saveState) {
    CrashScope crashScope("SimulationRuntime::importCitySaveState");
    const std::size_t totalTileCount = static_cast<std::size_t>(kMapWidth) * static_cast<std::size_t>(kMapHeight);
    if (saveState.width != kMapWidth || saveState.height != kMapHeight || saveState.tiles.size() != totalTileCount) {
        LogError("SimulationRuntime::importCitySaveState", "City save dimensions do not match the current runtime.");
        throw std::runtime_error("City save dimensions do not match the current runtime.");
    }

    {
        std::lock_guard<std::mutex> commandLock(commandMutex_);
        pendingCommands_.clear();
    }

    lotOccupancy_.assign(totalTileCount, kInvalidLotId);
    lots_.clear();
    zoningLots_ = saveState.zoningLots;
    nextLotId_ = std::max(1, saveState.nextLotId);
    simulationTick_ = saveState.simulationTick;

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
                int rotatedModuleWidth = 0;
                int rotatedModuleHeight = 0;
                RotatedRectangleDimensions(Int2(0, 0), moduleAsset->width, moduleAsset->height, lotSaveState.rotationSteps, rotatedModuleWidth, rotatedModuleHeight);
                lot.addModule(*moduleAsset, lotSaveState.modules[moduleIndex].localOrigin, kMapWidth, rotatedModuleWidth, rotatedModuleHeight);
            }
        }
        lot.setConstructionState(lotSaveState.constructionTotalTicks, lotSaveState.constructionRemainingTicks, kMapWidth);

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
    refreshCityPopulation();

    transportNetwork_.importSaveState(saveState.transport);

    int bufferIndex = 0;
    for (; bufferIndex < 3; ++bufferIndex) {
        tileBuffers_[bufferIndex].tiles = saveState.tiles;
        tileBuffers_[bufferIndex].chunkRevisions.assign(chunkLayout_.size(), 2);
        tileBuffers_[bufferIndex].publishedLots.clear();
        tileBuffers_[bufferIndex].publishedLotInfos.clear();
        tileBuffers_[bufferIndex].publishedZoningLots.clear();
        tileBuffers_[bufferIndex].publishedCommuteRouteSegments.clear();
        tileBuffers_[bufferIndex].publishedLotOccupancy.assign(totalTileCount, kInvalidLotId);
        tileBuffers_[bufferIndex].publishedRoads.assign(totalTileCount * TransportNetwork::layerCount(), ResolvedRoadCell());
        tileBuffers_[bufferIndex].publishedGroundRoadRenderState.assign(totalTileCount * kGroundRoadRenderChannelsPerTile, 0);
        tileBuffers_[bufferIndex].publishedTileOverlayState.assign(totalTileCount * 4u, 0);
        tileBuffers_[bufferIndex].publishedGroundRoadChunkRevisions.assign(chunkLayout_.size(), 1);
        tileBuffers_[bufferIndex].publishedElevatedRoadChunkRevisions.assign(chunkLayout_.size(), 1);
        tileBuffers_[bufferIndex].publishedTileOverlayChunkRevisions.assign(chunkLayout_.size(), 1);
        tileBuffers_[bufferIndex].lotRenderRevision = 0;
        tileBuffers_[bufferIndex].zoningLotRenderRevision = 0;
        tileBuffers_[bufferIndex].roadRenderRevision = 0;
        tileBuffers_[bufferIndex].overlayRenderRevision = 0;
        tileBuffers_[bufferIndex].commuteRenderRevision = 0;
        bufferUseCounts_[bufferIndex].store(0);
    }

    ++lotsRevision_;
    ++zoningLotsRevision_;
    ++commuteRevision_;
    commutesDirty_ = true;
    forcedCommuteLotIds_.clear();
    commuteRebalanceCursor_ = 0u;
    simulationReadBufferIndex_ = 0;
    simulationWriteBufferIndex_ = 1;
    refreshPublishedLotSnapshot(tileBuffers_[simulationReadBufferIndex_]);
    refreshPublishedZoningLotSnapshot(tileBuffers_[simulationReadBufferIndex_]);
    refreshPublishedRoadSnapshot(tileBuffers_[simulationReadBufferIndex_]);

    {
        std::lock_guard<std::mutex> publishedLock(publishedMutex_);
        publishedBufferIndex_ = simulationReadBufferIndex_;
        publishedSimulationTick_ = simulationTick_;
        publishedPopulation_ = cityPopulation_;
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
    snapshot.zoningLots = &tileBuffers_[publishedBufferIndex_].publishedZoningLots;
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
    snapshot.simulationTick = publishedSimulationTick_;
    snapshot.lotRevision = tileBuffers_[publishedBufferIndex_].lotRenderRevision;
    snapshot.zoningLotRevision = tileBuffers_[publishedBufferIndex_].zoningLotRenderRevision;
    snapshot.roadRevision = tileBuffers_[publishedBufferIndex_].roadRenderRevision;
    snapshot.overlayRevision = tileBuffers_[publishedBufferIndex_].overlayRenderRevision;
    snapshot.population = publishedPopulation_;

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
    CrashScope crashScope("SimulationRuntime::loadAssets");
    LoadedGameAssets loadedAssets;
    std::string errorMessage;
    if (!LoadGameAssets(GetExecutableDirectory() + "\\Data", cityParameterRegistry_, loadedAssets, errorMessage)) {
        LogError("SimulationRuntime::loadAssets", errorMessage);
        throw std::runtime_error(errorMessage);
    }

    moduleAssets_ = loadedAssets.modules;
    lotAssets_ = loadedAssets.lots;
    initialCityDemands_ = loadedAssets.initialDemands;
    if (initialCityDemands_.size() < cityParameterRegistry_.count()) {
        initialCityDemands_.resize(cityParameterRegistry_.count(), 0.0f);
    }
    rciConstructorAttemptsPerTick_ = std::max(1, loadedAssets.rciConstructorAttemptsPerTick);
    rciConstructorOverbuildMultiplier_ = std::max(0.0f, loadedAssets.rciConstructorOverbuildMultiplier);
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
        tileBuffers_[bufferIndex].publishedZoningLots.clear();
        tileBuffers_[bufferIndex].publishedCommuteRouteSegments.clear();
        tileBuffers_[bufferIndex].publishedLotOccupancy.assign(static_cast<std::size_t>(kMapWidth) * static_cast<std::size_t>(kMapHeight), kInvalidLotId);
        tileBuffers_[bufferIndex].publishedRoads.assign(static_cast<std::size_t>(kMapWidth) * static_cast<std::size_t>(kMapHeight) * TransportNetwork::layerCount(), ResolvedRoadCell());
        tileBuffers_[bufferIndex].publishedGroundRoadRenderState.assign(static_cast<std::size_t>(kMapWidth) * static_cast<std::size_t>(kMapHeight) * kGroundRoadRenderChannelsPerTile, 0);
        tileBuffers_[bufferIndex].publishedTileOverlayState.assign(static_cast<std::size_t>(kMapWidth) * static_cast<std::size_t>(kMapHeight) * 4u, 0);
        tileBuffers_[bufferIndex].publishedGroundRoadChunkRevisions.assign(chunkLayout_.size(), 1);
        tileBuffers_[bufferIndex].publishedElevatedRoadChunkRevisions.assign(chunkLayout_.size(), 1);
        tileBuffers_[bufferIndex].publishedTileOverlayChunkRevisions.assign(chunkLayout_.size(), 1);
        tileBuffers_[bufferIndex].lotRenderRevision = 0;
        tileBuffers_[bufferIndex].zoningLotRenderRevision = 0;
        tileBuffers_[bufferIndex].roadRenderRevision = 0;
        tileBuffers_[bufferIndex].overlayRenderRevision = 0;
        tileBuffers_[bufferIndex].commuteRenderRevision = 0;
    }

    lotOccupancy_.assign(static_cast<std::size_t>(kMapWidth) * static_cast<std::size_t>(kMapHeight), kInvalidLotId);
    lots_.clear();
    zoningLots_.clear();
    nextLotId_ = 1;
    lotsRevision_ = 0;
    zoningLotsRevision_ = 0;
    commuteRevision_ = 0;
    simulationTick_ = 0;
    cityPopulation_ = 0;
    commutesDirty_ = true;
    forcedCommuteLotIds_.clear();
    commuteRebalanceCursor_ = 0u;
    oldCityParameters_.assign(cityParameterRegistry_.count(), 0.0f);
    nextCityParameters_.assign(cityParameterRegistry_.count(), 0.0f);
    if (initialCityDemands_.size() < cityParameterRegistry_.count()) {
        initialCityDemands_.resize(cityParameterRegistry_.count(), 0.0f);
    }
    transportNetwork_.clear();

    std::lock_guard<std::mutex> publishedLock(publishedMutex_);
    publishedBufferIndex_ = simulationReadBufferIndex_;
    publishedSimulationTick_ = simulationTick_;
    publishedPopulation_ = cityPopulation_;
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
        advanceLotConstruction(tileBuffers_[simulationWriteBufferIndex_]);
        commandPassMicros_.store(DurationMicros(commandStart, std::chrono::steady_clock::now()));

        const std::chrono::steady_clock::time_point lotEffectsStart = std::chrono::steady_clock::now();
        applyLotEffects(tileBuffers_[simulationWriteBufferIndex_].tiles);
        runCommuteAssignment();
        runRciConstructor(tileBuffers_[simulationWriteBufferIndex_]);
        lotEffectsMicros_.store(DurationMicros(lotEffectsStart, std::chrono::steady_clock::now()));

        const std::chrono::steady_clock::time_point localStart = std::chrono::steady_clock::now();
        runLocalTilePass(tileBuffers_[simulationWriteBufferIndex_].tiles);
        localPassMicros_.store(DurationMicros(localStart, std::chrono::steady_clock::now()));

        ++simulationTick_;

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
                    clearZoningForRoadStroke(playerCommand.roadStroke, writeBuffer);
                    commutesDirty_ = true;
                    forcedCommuteLotIds_.clear();
                    commuteRebalanceCursor_ = 0u;
                }
                break;

            case PlayerCommandType::BulldozeAtTile:
                tryBulldozeAtTile(playerCommand.tileX, playerCommand.tileY, writeBuffer);
                break;

            case PlayerCommandType::BulldozeArea:
                tryBulldozeArea(playerCommand.tileX, playerCommand.tileY, playerCommand.endTileX, playerCommand.endTileY, writeBuffer);
                break;

            case PlayerCommandType::ZoneArea:
                tryZoneArea(playerCommand.tileX, playerCommand.tileY, playerCommand.endTileX, playerCommand.endTileY, playerCommand.zoningType, writeBuffer);
                break;

            case PlayerCommandType::ZoneLot:
                tryZoneLot(playerCommand.zoningLot, writeBuffer);
                break;
        }
    }
}

// Advances constructor-started lots before demand is recalculated. Until a lot
// completes, its modules render as rising construction geometry but do not
// contribute city parameters, pollution, land value, or commute demand.
void SimulationRuntime::advanceLotConstruction(TileBuffer& writeBuffer) {
    std::vector<int> dirtyTiles;
    std::size_t lotIndex = 0;
    for (; lotIndex < lots_.size(); ++lotIndex) {
        Lot& lot = lots_[lotIndex];
        const bool wasUnderConstruction = lot.isUnderConstruction();
        if (!lot.advanceConstructionTick(kMapWidth)) {
            continue;
        }

        const std::vector<int>& occupiedTileIndices = lot.occupiedTileIndices();
        dirtyTiles.insert(dirtyTiles.end(), occupiedTileIndices.begin(), occupiedTileIndices.end());
        if (wasUnderConstruction && !lot.isUnderConstruction()) {
            queueCommuteRecalculationForLot(lot.id());
            queueCommuteSourcesForDestination(lot.id());
        }
    }

    if (dirtyTiles.empty()) {
        return;
    }

    markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, dirtyTiles);
    ++lotsRevision_;
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

void SimulationRuntime::refreshCityPopulation() {
    cityPopulation_ = CalculatePopulationFromCityParameters(oldCityParameters_, cityParameterRegistry_);
}

void SimulationRuntime::queueCommuteRecalculationForLot(int lotId) {
    if (lotId == kInvalidLotId) {
        return;
    }

    if (std::find(forcedCommuteLotIds_.begin(), forcedCommuteLotIds_.end(), lotId) == forcedCommuteLotIds_.end()) {
        forcedCommuteLotIds_.push_back(lotId);
    }
}

void SimulationRuntime::queueCommuteSourcesForDestination(int destinationLotId) {
    if (destinationLotId == kInvalidLotId) {
        return;
    }

    std::size_t lotIndex = 0;
    for (; lotIndex < lots_.size(); ++lotIndex) {
        const std::vector<CommuteRouteRecord>& routes = lots_[lotIndex].commuteRoutes();
        std::size_t routeIndex = 0;
        for (; routeIndex < routes.size(); ++routeIndex) {
            if (routes[routeIndex].destinationLotId == destinationLotId) {
                queueCommuteRecalculationForLot(lots_[lotIndex].id());
                break;
            }
        }
    }
}

void SimulationRuntime::removeCommuteLoadsForLot(const Lot& lot) {
    const std::vector<CommuteRouteRecord>& routes = lot.commuteRoutes();
    if (routes.empty()) {
        return;
    }

    transportNetwork_.beginTrafficAssignmentFromOldLoad();
    std::size_t routeIndex = 0;
    for (; routeIndex < routes.size(); ++routeIndex) {
        transportNetwork_.applyTrafficPathLoad(routes[routeIndex].pathResult, routes[routeIndex].transportLoad, false);
    }
    transportNetwork_.commitTrafficAssignment();
    ++commuteRevision_;
}

void SimulationRuntime::runCommuteAssignment() {
    rebuildCityParameters();

    struct CommuteSource {
        std::size_t lotIndex;
        int lotId;
        int demand;
        std::vector<std::uint32_t> accessNodes;
    };

    struct JobDestination {
        std::size_t lotIndex;
        int lotId;
        int remainingCapacity;
        std::vector<std::uint32_t> accessNodes;
    };

    const TransportCostMap& costMap = transportNetwork_.costMap();
    std::vector<CommuteSource> sources;
    std::vector<JobDestination> destinations;
    std::unordered_map<int, std::size_t> lotIndexById;
    const std::uint8_t allowedModeMask = kTransportModeCar | kTransportModePedestrian;
    bool commuteStateChanged = commutesDirty_;
    int totalResidentDemand = 0;

    std::size_t lotIndex = 0;
    for (; lotIndex < lots_.size(); ++lotIndex) {
        Lot& lot = lots_[lotIndex];
        if (commutesDirty_) {
            lot.clearCommutes();
        }

        lotIndexById[lot.id()] = lotIndex;

        std::vector<std::uint32_t> accessNodes;
        const LotAsset* lotAsset = findLotAssetById(lot.assetId());
        if (lotAsset != 0) {
            collectLotAccessNodes(lot, *lotAsset, allowedModeMask, accessNodes);
        }

        const int residentDemand = static_cast<int>(lotParameterAmount(lot, cityParameterRegistry_.residentsLowWealthId()) + 0.5f);
        const int previousResidentDemand = lot.lowWealthResidentsTotal();
        const int previousCommuteDemand = lot.commuteDemand();
        lot.setLowWealthResidentsTotal(residentDemand);
        lot.setLowWealthResidentsRoadAccess(residentDemand <= 0 || !accessNodes.empty());
        lot.setCommuteDemand(residentDemand);
        totalResidentDemand += std::max(0, residentDemand);
        if (residentDemand > 0) {
            if (!commutesDirty_ &&
                (previousResidentDemand != residentDemand ||
                    previousCommuteDemand != residentDemand ||
                    accessNodes.empty() ||
                    lot.commuteSatisfied() > residentDemand)) {
                queueCommuteRecalculationForLot(lot.id());
            }

            CommuteSource source;
            source.lotIndex = lotIndex;
            source.lotId = lot.id();
            source.demand = residentDemand;
            source.accessNodes = accessNodes;
            sources.push_back(source);
        } else if (!commutesDirty_ && !lot.commuteRoutes().empty()) {
            queueCommuteRecalculationForLot(lot.id());
        }

        const int lowWealthJobCapacity = static_cast<int>(lotDerivedParameterAmount(lot, cityParameterRegistry_.jobsLowWealthId()) + 0.5f);
        const int previousJobCapacity = lot.lowWealthJobsTotal();
        lot.setLowWealthJobsTotal(lowWealthJobCapacity);
        lot.setLowWealthJobsRoadAccess(lowWealthJobCapacity <= 0 || !accessNodes.empty());
        if (!commutesDirty_ && previousJobCapacity != lowWealthJobCapacity) {
            queueCommuteSourcesForDestination(lot.id());
        }

        if (lowWealthJobCapacity > 0) {
            JobDestination destination;
            destination.lotIndex = lotIndex;
            destination.lotId = lot.id();
            destination.remainingCapacity = lowWealthJobCapacity;
            destination.accessNodes = accessNodes;
            destinations.push_back(destination);
        }
    }

    if (!commutesDirty_) {
        for (lotIndex = 0; lotIndex < lots_.size(); ++lotIndex) {
            const std::vector<CommuteRouteRecord>& routes = lots_[lotIndex].commuteRoutes();
            std::size_t routeIndex = 0;
            for (; routeIndex < routes.size(); ++routeIndex) {
                if (lotIndexById.find(routes[routeIndex].destinationLotId) == lotIndexById.end() ||
                    !commuteRouteIsStillValid(routes[routeIndex])) {
                    queueCommuteRecalculationForLot(lots_[lotIndex].id());
                    break;
                }
            }
        }
    }

    std::vector<std::size_t> selectedSourceIndices;
    std::vector<std::size_t> selectedLotIndices;
    std::vector<bool> selectedLot(lots_.size(), false);
    if (commutesDirty_) {
        std::size_t sourceIndex = 0;
        for (; sourceIndex < sources.size(); ++sourceIndex) {
            selectedSourceIndices.push_back(sourceIndex);
            selectedLot[sources[sourceIndex].lotIndex] = true;
        }
    } else {
        std::size_t forcedIndex = 0;
        for (; forcedIndex < forcedCommuteLotIds_.size(); ++forcedIndex) {
            const std::unordered_map<int, std::size_t>::const_iterator lotIterator = lotIndexById.find(forcedCommuteLotIds_[forcedIndex]);
            if (lotIterator == lotIndexById.end()) {
                continue;
            }

            if (!selectedLot[lotIterator->second]) {
                selectedLot[lotIterator->second] = true;
                selectedLotIndices.push_back(lotIterator->second);
            }

            std::size_t sourceIndex = 0;
            for (; sourceIndex < sources.size(); ++sourceIndex) {
                if (sources[sourceIndex].lotIndex == lotIterator->second) {
                    selectedSourceIndices.push_back(sourceIndex);
                    break;
                }
            }
        }

        if (!sources.empty() && totalResidentDemand > 0) {
            int routineSourceBudget = std::max(1, (static_cast<int>(sources.size()) + 99) / 100);
            std::size_t scannedSourceCount = 0;
            if (commuteRebalanceCursor_ >= sources.size()) {
                commuteRebalanceCursor_ = 0u;
            }

            for (; scannedSourceCount < sources.size() && routineSourceBudget > 0; ++scannedSourceCount) {
                const std::size_t sourceIndex = (commuteRebalanceCursor_ + scannedSourceCount) % sources.size();
                const std::size_t sourceLotIndex = sources[sourceIndex].lotIndex;
                if (selectedLot[sourceLotIndex] || sources[sourceIndex].accessNodes.empty()) {
                    continue;
                }

                selectedSourceIndices.push_back(sourceIndex);
                selectedLot[sourceLotIndex] = true;
                selectedLotIndices.push_back(sourceLotIndex);
                --routineSourceBudget;
            }

            commuteRebalanceCursor_ = (commuteRebalanceCursor_ + std::max<std::size_t>(1u, scannedSourceCount)) % sources.size();
        }
    }

    const auto updateCommuteSatisfactionParameters = [this]() {
        int totalSatisfied = 0;
        std::size_t satisfiedLotIndex = 0;
        for (; satisfiedLotIndex < lots_.size(); ++satisfiedLotIndex) {
            totalSatisfied += lots_[satisfiedLotIndex].commuteSatisfied();
        }

        if (cityParameterRegistry_.satisfactionLowWealthCommuteId() >= 0 &&
            cityParameterRegistry_.satisfactionLowWealthCommuteId() < static_cast<int>(nextCityParameters_.size())) {
            nextCityParameters_[cityParameterRegistry_.satisfactionLowWealthCommuteId()] = static_cast<float>(totalSatisfied);
        }
        if (cityParameterRegistry_.satisfactionDirtyIndustryStaffingId() >= 0 &&
            cityParameterRegistry_.satisfactionDirtyIndustryStaffingId() < static_cast<int>(nextCityParameters_.size())) {
            nextCityParameters_[cityParameterRegistry_.satisfactionDirtyIndustryStaffingId()] = static_cast<float>(totalSatisfied);
        }
    };

    if (selectedLotIndices.empty() && !commutesDirty_) {
        updateCommuteSatisfactionParameters();
        oldCityParameters_ = nextCityParameters_;
        refreshCityPopulation();
        if (!forcedCommuteLotIds_.empty()) {
            forcedCommuteLotIds_.clear();
        }
        return;
    }

    if (commutesDirty_) {
        transportNetwork_.beginTrafficAssignmentFromZero();
    } else {
        transportNetwork_.beginTrafficAssignmentFromOldLoad();
        std::size_t selectedIndex = 0;
        for (; selectedIndex < selectedLotIndices.size(); ++selectedIndex) {
            Lot& sourceLot = lots_[selectedLotIndices[selectedIndex]];
            const std::vector<CommuteRouteRecord>& existingRoutes = sourceLot.commuteRoutes();
            std::size_t routeIndex = 0;
            for (; routeIndex < existingRoutes.size(); ++routeIndex) {
                transportNetwork_.applyTrafficPathLoad(existingRoutes[routeIndex].pathResult, existingRoutes[routeIndex].transportLoad, false);
            }

            if (!existingRoutes.empty()) {
                commuteStateChanged = true;
            }
            sourceLot.clearCommuteRoutes();
        }
    }

    std::vector<int> filledJobsByLot(lots_.size(), 0);
    for (lotIndex = 0; lotIndex < lots_.size(); ++lotIndex) {
        const std::vector<CommuteRouteRecord>& routes = lots_[lotIndex].commuteRoutes();
        std::size_t routeIndex = 0;
        for (; routeIndex < routes.size(); ++routeIndex) {
            const std::unordered_map<int, std::size_t>::const_iterator destinationIterator = lotIndexById.find(routes[routeIndex].destinationLotId);
            if (destinationIterator != lotIndexById.end()) {
                filledJobsByLot[destinationIterator->second] += routes[routeIndex].demand;
            }
        }
    }

    for (lotIndex = 0; lotIndex < lots_.size(); ++lotIndex) {
        lots_[lotIndex].setLowWealthJobsFilled(filledJobsByLot[lotIndex]);
    }

    std::size_t destinationIndex = 0;
    for (; destinationIndex < destinations.size(); ++destinationIndex) {
        JobDestination& destination = destinations[destinationIndex];
        destination.remainingCapacity = std::max(0, destination.remainingCapacity - filledJobsByLot[destination.lotIndex]);
    }

    TransportPathScratch scratch;

    std::size_t selectedIndex = 0;
    for (; selectedIndex < selectedSourceIndices.size(); ++selectedIndex) {
        CommuteSource& source = sources[selectedSourceIndices[selectedIndex]];
        if (source.accessNodes.empty()) {
            continue;
        }

        int remainingDemand = source.demand;
        while (remainingDemand > 0) {
            TransportPathRequest request;
            request.startNodeIds = source.accessNodes;
            request.routeSeed = static_cast<std::uint32_t>((lots_[source.lotIndex].id() * 73856093) ^ (remainingDemand * 19349663) ^ static_cast<int>(commuteRevision_ + 1u));
            request.demand = static_cast<std::uint16_t>(std::min(remainingDemand, static_cast<int>(kTransportMaxLoad)));
            request.maximumCost = kMaximumCommuteCost;
            request.useCongestion = true;

            std::vector<int> goalDestinationIndices;
            destinationIndex = 0;
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

            const std::uint16_t clampedDemand = static_cast<std::uint16_t>(std::min(acceptedDemand, static_cast<int>(kTransportMaxLoad)));
            const bool longCommute = pathResult.totalCost >= kLongCommuteComplaintCost;
            lots_[source.lotIndex].addCommuteRoute(
                reachedDestination.lotId,
                acceptedDemand,
                clampedDemand,
                longCommute,
                pathResult,
                buildCommuteRouteSegments(pathResult, clampedDemand));
            lots_[reachedDestination.lotIndex].addLowWealthJobsFilled(acceptedDemand);
            transportNetwork_.applyTrafficPathLoad(pathResult, clampedDemand, true);
            commuteStateChanged = true;
        }
    }

    transportNetwork_.commitTrafficAssignment();

    updateCommuteSatisfactionParameters();

    oldCityParameters_ = nextCityParameters_;
    refreshCityPopulation();
    if (commuteStateChanged || !forcedCommuteLotIds_.empty()) {
        ++commuteRevision_;
    }
    commutesDirty_ = false;
    forcedCommuteLotIds_.clear();
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
    refreshPublishedZoningLotSnapshot(completedBuffer);
    refreshPublishedRoadSnapshot(completedBuffer);

    {
        std::lock_guard<std::mutex> publishedLock(publishedMutex_);
        publishedBufferIndex_ = completedBufferIndex;
        publishedSimulationTick_ = simulationTick_;
        publishedPopulation_ = cityPopulation_;
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
    completedBuffer.publishedLots.reserve(lots_.size() * 4u);
    completedBuffer.publishedLotInfos.clear();
    completedBuffer.publishedLotInfos.reserve(lots_.size());
    completedBuffer.publishedCommuteRouteSegments.clear();
    completedBuffer.publishedLotOccupancy = lotOccupancy_;

    std::size_t lotIndex = 0;
    for (; lotIndex < lots_.size(); ++lotIndex) {
        PublishedLotInfo publishedLotInfo;
        publishedLotInfo.lotId = lots_[lotIndex].id();
        publishedLotInfo.assetId = lots_[lotIndex].assetId();
        const LotAsset* lotAsset = findLotAssetById(lots_[lotIndex].assetId());
        if (lotAsset != 0) {
            publishedLotInfo.zoningType = lotAsset->zoningType;
        }
        publishedLotInfo.isEmpty = lots_[lotIndex].modules().empty();
        publishedLotInfo.moduleSummary = lots_[lotIndex].moduleSummary();
        publishedLotInfo.parameterSummary = lots_[lotIndex].parameterSummary(cityParameterRegistry_);
        publishedLotInfo.commuteDemand = lots_[lotIndex].commuteDemand();
        publishedLotInfo.commuteSatisfied = lots_[lotIndex].commuteSatisfied();
        publishedLotInfo.residentsLowWealthCurrent = lots_[lotIndex].commuteSatisfied();
        publishedLotInfo.residentsLowWealthTotal = lots_[lotIndex].lowWealthResidentsTotal();
        publishedLotInfo.jobsLowWealthCurrent = lots_[lotIndex].lowWealthJobsFilled();
        publishedLotInfo.jobsLowWealthTotal = lots_[lotIndex].lowWealthJobsTotal();
        publishedLotInfo.complaintSummary = lots_[lotIndex].complaintSummary();
        completedBuffer.publishedLotInfos.push_back(publishedLotInfo);
        lots_[lotIndex].buildRenderInstances(completedBuffer.publishedLots);
    }

    std::unordered_map<int, std::size_t> publishedLotIndexById;
    publishedLotIndexById.reserve(completedBuffer.publishedLotInfos.size());
    for (lotIndex = 0; lotIndex < completedBuffer.publishedLotInfos.size(); ++lotIndex) {
        publishedLotIndexById[completedBuffer.publishedLotInfos[lotIndex].lotId] = lotIndex;
    }

    for (lotIndex = 0; lotIndex < lots_.size(); ++lotIndex) {
        const std::vector<CommuteRouteRecord>& routes = lots_[lotIndex].commuteRoutes();
        const std::unordered_map<int, std::size_t>::const_iterator sourceIterator = publishedLotIndexById.find(lots_[lotIndex].id());
        if (sourceIterator != publishedLotIndexById.end()) {
            PublishedLotInfo& sourceInfo = completedBuffer.publishedLotInfos[sourceIterator->second];
            std::size_t routeIndex = 0;
            for (; routeIndex < routes.size(); ++routeIndex) {
                sourceInfo.commuteRouteSegments.insert(sourceInfo.commuteRouteSegments.end(), routes[routeIndex].segments.begin(), routes[routeIndex].segments.end());
                completedBuffer.publishedCommuteRouteSegments.insert(completedBuffer.publishedCommuteRouteSegments.end(), routes[routeIndex].segments.begin(), routes[routeIndex].segments.end());
            }
        }

        std::size_t routeIndex = 0;
        for (; routeIndex < routes.size(); ++routeIndex) {
            const std::unordered_map<int, std::size_t>::const_iterator destinationIterator = publishedLotIndexById.find(routes[routeIndex].destinationLotId);
            if (destinationIterator == publishedLotIndexById.end()) {
                continue;
            }

            PublishedLotInfo& destinationInfo = completedBuffer.publishedLotInfos[destinationIterator->second];
            destinationInfo.commuteRouteSegments.insert(destinationInfo.commuteRouteSegments.end(), routes[routeIndex].segments.begin(), routes[routeIndex].segments.end());
        }
    }

    completedBuffer.lotRenderRevision = lotsRevision_;
    completedBuffer.commuteRenderRevision = commuteRevision_;
}

void SimulationRuntime::refreshPublishedZoningLotSnapshot(TileBuffer& completedBuffer) {
    if (completedBuffer.zoningLotRenderRevision == zoningLotsRevision_) {
        return;
    }

    completedBuffer.publishedZoningLots = zoningLots_;
    completedBuffer.zoningLotRenderRevision = zoningLotsRevision_;
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
        int rotatedModuleWidth = 0;
        int rotatedModuleHeight = 0;
        RotatedRectangleDimensions(Int2(0, 0), moduleAsset->width, moduleAsset->height, normalizedRotation, rotatedModuleWidth, rotatedModuleHeight);
        candidateLot.addModule(*moduleAsset, rotatedModuleOrigin, kMapWidth, rotatedModuleWidth, rotatedModuleHeight);
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
    queueCommuteRecalculationForLot(candidateLot.id());
    return true;
}

void SimulationRuntime::runRciConstructor(TileBuffer& writeBuffer) {
    tryConstructOneRciLot(TileZoningResidential, writeBuffer);
    tryConstructOneRciLot(TileZoningIndustrial, writeBuffer);
}

bool SimulationRuntime::tryConstructOneRciLot(std::uint16_t zoningType, TileBuffer& writeBuffer) {
    if (zoningType == TileZoningNone || zoningLots_.empty()) {
        return false;
    }

    const float demand = rciDemandForZoningType(zoningType);
    if (demand <= 0.0f) {
        return false;
    }

    // Under-construction lots do not affect city parameters yet, but they still
    // reserve demand budget so the constructor does not keep filling the same demand.
    float remainingBudget = (demand * rciConstructorOverbuildMultiplier_) - rciPendingConstructionCapacity(zoningType);
    if (remainingBudget <= 0.0f) {
        return false;
    }

    bool constructedAny = false;
    int attempts = 0;
    std::size_t zoningLotIndex = 0;
    while (zoningLotIndex < zoningLots_.size() && attempts < rciConstructorAttemptsPerTick_ && remainingBudget > 0.0f) {
        const RciLot& zoningLot = zoningLots_[zoningLotIndex];
        if (zoningLot.zoningType != zoningType || zoningLot.availableAfterTick > simulationTick_) {
            ++zoningLotIndex;
            continue;
        }

        ++attempts;
        float constructedCapacity = 0.0f;
        if (tryConstructRciLotAtIndex(zoningLotIndex, remainingBudget, writeBuffer, constructedCapacity)) {
            constructedAny = true;
            remainingBudget -= constructedCapacity;
            continue;
        }

        ++zoningLotIndex;
    }

    return constructedAny;
}

bool SimulationRuntime::tryConstructRciLotAtIndex(std::size_t zoningLotIndex, float demandBudget, TileBuffer& writeBuffer, float& constructedCapacity) {
    constructedCapacity = 0.0f;
    if (zoningLotIndex >= zoningLots_.size()) {
        return false;
    }

    const RciLot zoningLot = zoningLots_[zoningLotIndex];
    if (zoningLot.zoningType == TileZoningNone || !zoningLot.rect.isValid() || zoningLot.availableAfterTick > simulationTick_) {
        return false;
    }

    int rotationSteps = 0;
    float lotCapacity = 0.0f;
    const LotAsset* lotAsset = findRciConstructorLotAsset(zoningLot.zoningType, zoningLot.rect.width(), zoningLot.rect.height(), demandBudget, rotationSteps, lotCapacity);
    if (lotAsset == 0) {
        return false;
    }

    Int2 footprintMinimum;
    Int2 footprintMaximum;
    RotatedRectangleBounds(lotAsset->footprintOrigin, lotAsset->footprintWidth, lotAsset->footprintHeight, rotationSteps, footprintMinimum, footprintMaximum);
    if (footprintMaximum.x - footprintMinimum.x + 1 != zoningLot.rect.width() ||
        footprintMaximum.y - footprintMinimum.y + 1 != zoningLot.rect.height()) {
        return false;
    }

    const int anchorTileX = zoningLot.rect.minTileX - footprintMinimum.x;
    const int anchorTileY = zoningLot.rect.minTileY - footprintMinimum.y;
    Lot candidateLot;
    if (!buildLotCandidate(*lotAsset, anchorTileX, anchorTileY, rotationSteps, nextLotId_, candidateLot)) {
        return false;
    }

    if (candidateLot.minimumTileX() != zoningLot.rect.minTileX ||
        candidateLot.minimumTileY() != zoningLot.rect.minTileY ||
        candidateLot.footprintWidth() != zoningLot.rect.width() ||
        candidateLot.footprintHeight() != zoningLot.rect.height()) {
        return false;
    }

    const std::vector<int>& occupiedTileIndices = candidateLot.occupiedTileIndices();
    std::size_t tileIndexValue = 0;
    for (; tileIndexValue < occupiedTileIndices.size(); ++tileIndexValue) {
        const int tileLinearIndex = occupiedTileIndices[tileIndexValue];
        if (tileLinearIndex < 0 || tileLinearIndex >= static_cast<int>(writeBuffer.tiles.size())) {
            return false;
        }

        if (writeBuffer.tiles[tileLinearIndex].zoningType != zoningLot.zoningType) {
            return false;
        }
    }

    if (!canPlaceLot(candidateLot)) {
        return false;
    }

    candidateLot.startConstruction(lotAsset->constructionTicks, kMapWidth);
    lots_.push_back(candidateLot);
    setLotOccupancy(candidateLot.id(), candidateLot.occupiedTileIndices());
    markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, candidateLot.occupiedTileIndices());
    ++nextLotId_;
    ++lotsRevision_;
    constructedCapacity = lotCapacity;
    if (!candidateLot.isUnderConstruction()) {
        queueCommuteRecalculationForLot(candidateLot.id());
    }

    zoningLots_.erase(zoningLots_.begin() + static_cast<std::ptrdiff_t>(zoningLotIndex));
    ++zoningLotsRevision_;
    return true;
}

const LotAsset* SimulationRuntime::findRciConstructorLotAsset(std::uint16_t zoningType, int width, int height, float demandBudget, int& rotationSteps, float& capacity) const {
    rotationSteps = 0;
    capacity = 0.0f;
    const LotAsset* bestMatch = 0;
    int bestRotationSteps = 0;
    std::size_t lotAssetIndex = 0;
    for (; lotAssetIndex < lotAssets_.size(); ++lotAssetIndex) {
        const LotAsset& lotAsset = lotAssets_[lotAssetIndex];
        if (lotAsset.zoningType != zoningType) {
            continue;
        }

        int candidateRotationSteps = -1;
        if (lotAsset.footprintWidth == width && lotAsset.footprintHeight == height) {
            candidateRotationSteps = 0;
        } else if (lotAsset.footprintWidth == height && lotAsset.footprintHeight == width) {
            candidateRotationSteps = 1;
        }

        if (candidateRotationSteps < 0) {
            continue;
        }

        const float candidateCapacity = rciCapacityForLotAsset(lotAsset, zoningType);
        if (candidateCapacity <= 0.0f || candidateCapacity > demandBudget + 0.001f) {
            continue;
        }

        if (bestMatch == 0 || candidateCapacity > capacity) {
            bestMatch = &lotAsset;
            bestRotationSteps = candidateRotationSteps;
            capacity = candidateCapacity;
        }
    }

    if (bestMatch != 0) {
        rotationSteps = bestRotationSteps;
    }
    return bestMatch;
}

float SimulationRuntime::rciDemandForZoningType(std::uint16_t zoningType) const {
    const int residentsLowWealthId = cityParameterRegistry_.residentsLowWealthId();
    const int jobsLowWealthId = cityParameterRegistry_.jobsLowWealthId();
    const int jobsDirtyIndustryId = cityParameterRegistry_.jobsDirtyIndustryId();

    const auto parameterValue = [this](int parameterId) -> float {
        if (parameterId < 0 || parameterId >= static_cast<int>(oldCityParameters_.size())) {
            return 0.0f;
        }

        return oldCityParameters_[static_cast<std::size_t>(parameterId)];
    };

    const auto initialDemand = [this](int parameterId) -> float {
        if (parameterId < 0 || parameterId >= static_cast<int>(initialCityDemands_.size())) {
            return 0.0f;
        }

        return initialCityDemands_[static_cast<std::size_t>(parameterId)];
    };

    if (zoningType == TileZoningResidential) {
        return std::max(0.0f, initialDemand(residentsLowWealthId) + parameterValue(jobsLowWealthId) - parameterValue(residentsLowWealthId));
    }

    if (zoningType == TileZoningIndustrial) {
        return std::max(0.0f, initialDemand(jobsDirtyIndustryId) + parameterValue(residentsLowWealthId) - parameterValue(jobsDirtyIndustryId));
    }

    return 0.0f;
}

float SimulationRuntime::rciPendingConstructionCapacity(std::uint16_t zoningType) const {
    float capacity = 0.0f;
    std::size_t lotIndex = 0;
    for (; lotIndex < lots_.size(); ++lotIndex) {
        const Lot& lot = lots_[lotIndex];
        if (!lot.isUnderConstruction()) {
            continue;
        }

        const LotAsset* lotAsset = findLotAssetById(lot.assetId());
        if (lotAsset == 0 || lotAsset->zoningType != zoningType) {
            continue;
        }

        capacity += rciCapacityForLotAsset(*lotAsset, zoningType);
    }

    return capacity;
}

float SimulationRuntime::rciCapacityForLotAsset(const LotAsset& lotAsset, std::uint16_t zoningType) const {
    const int parameterId = rciDemandParameterId(zoningType);
    if (parameterId < 0) {
        return 0.0f;
    }

    float capacity = 0.0f;
    std::size_t placementIndex = 0;
    for (; placementIndex < lotAsset.initialModules.size(); ++placementIndex) {
        const LotModule* moduleAsset = findModuleAssetById(lotAsset.initialModules[placementIndex].moduleId);
        if (moduleAsset == 0) {
            continue;
        }

        std::size_t contributionIndex = 0;
        for (; contributionIndex < moduleAsset->parameterContributions.size(); ++contributionIndex) {
            const CityParameterContribution& contribution = moduleAsset->parameterContributions[contributionIndex];
            if (contribution.parameterId == parameterId) {
                capacity += contribution.amount;
            }
        }
    }

    return capacity;
}

int SimulationRuntime::rciDemandParameterId(std::uint16_t zoningType) const {
    if (zoningType == TileZoningResidential) {
        return cityParameterRegistry_.residentsLowWealthId();
    }

    if (zoningType == TileZoningIndustrial) {
        return cityParameterRegistry_.jobsDirtyIndustryId();
    }

    return -1;
}

RciLot SimulationRuntime::buildRedevelopmentRciLot(const Lot& lot, const LotAsset& lotAsset, std::uint64_t availableAfterTick) const {
    RciLot redevelopmentLot;
    redevelopmentLot.toolId = RciToolIdForZoningType(lotAsset.zoningType);
    redevelopmentLot.name = RciNameForZoningType(lotAsset.zoningType);
    redevelopmentLot.zoningType = lotAsset.zoningType;
    redevelopmentLot.color = RciColorForZoningType(lotAsset.zoningType);
    redevelopmentLot.rect = RciRect(
        lot.minimumTileX(),
        lot.minimumTileY(),
        lot.minimumTileX() + lot.footprintWidth() - 1,
        lot.minimumTileY() + lot.footprintHeight() - 1);
    redevelopmentLot.availableAfterTick = availableAfterTick;
    return redevelopmentLot;
}

bool SimulationRuntime::exposeRciLotForRedevelopment(std::size_t lotIndex, const LotAsset& lotAsset, TileBuffer& writeBuffer, std::uint64_t availableAfterTick) {
    if (lotIndex >= lots_.size() || lotAsset.zoningType == TileZoningNone) {
        return false;
    }

    const int lotId = lots_[lotIndex].id();
    const std::vector<int> occupiedTiles = lots_[lotIndex].occupiedTileIndices();
    const RciLot redevelopmentLot = buildRedevelopmentRciLot(lots_[lotIndex], lotAsset, availableAfterTick);

    queueCommuteSourcesForDestination(lotId);
    removeCommuteLoadsForLot(lots_[lotIndex]);
    clearLotOccupancy(occupiedTiles);
    lots_.erase(lots_.begin() + static_cast<std::ptrdiff_t>(lotIndex));

    std::size_t tileIndexValue = 0;
    for (; tileIndexValue < occupiedTiles.size(); ++tileIndexValue) {
        const int tileLinearIndex = occupiedTiles[tileIndexValue];
        if (tileLinearIndex < 0 || tileLinearIndex >= static_cast<int>(writeBuffer.tiles.size())) {
            continue;
        }

        writeBuffer.tiles[tileLinearIndex].zoningType = lotAsset.zoningType;
        writeBuffer.tiles[tileLinearIndex].isVacant = true;
    }

    removeZoningLotsIntersectingRect(redevelopmentLot.rect);
    zoningLots_.push_back(redevelopmentLot);
    markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, occupiedTiles);
    ++lotsRevision_;
    ++zoningLotsRevision_;
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
    int rotatedModuleWidth = 0;
    int rotatedModuleHeight = 0;
    RotatedRectangleDimensions(Int2(0, 0), moduleAsset.width, moduleAsset.height, targetLot->rotationSteps(), rotatedModuleWidth, rotatedModuleHeight);
    targetLot->addModule(moduleAsset, localOrigin, kMapWidth, rotatedModuleWidth, rotatedModuleHeight);

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
    queueCommuteRecalculationForLot(targetLotId);
    queueCommuteSourcesForDestination(targetLotId);
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

    const LotAsset* targetLotAsset = findLotAssetById(targetLot->assetId());
    const bool preserveEmptyRciLot = targetLotAsset != 0 && targetLotAsset->zoningType != TileZoningNone;
    queueCommuteSourcesForDestination(lotId);

    std::vector<int> dirtyTiles = oldOccupiedTiles;
    if (targetLot->modules().empty()) {
        if (preserveEmptyRciLot) {
            std::size_t lotIndex = 0;
            for (; lotIndex < lots_.size(); ++lotIndex) {
                if (lots_[lotIndex].id() == lotId) {
                    return exposeRciLotForRedevelopment(lotIndex, *targetLotAsset, writeBuffer, simulationTick_ + kRciRedevelopmentGraceTicks);
                }
            }

            return false;
        } else {
            clearLotOccupancy(oldOccupiedTiles);
            std::size_t lotIndex = 0;
            for (; lotIndex < lots_.size(); ++lotIndex) {
                if (lots_[lotIndex].id() != lotId) {
                    continue;
                }

                removeCommuteLoadsForLot(lots_[lotIndex]);
                lots_.erase(lots_.begin() + static_cast<std::ptrdiff_t>(lotIndex));
                break;
            }
        }
    } else {
        clearLotOccupancy(oldOccupiedTiles);
        targetLot->rebaseAnchorToMinimumTile(kMapWidth);
        setLotOccupancy(lotId, targetLot->occupiedTileIndices());
        dirtyTiles.insert(dirtyTiles.end(), targetLot->occupiedTileIndices().begin(), targetLot->occupiedTileIndices().end());
    }

    markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, dirtyTiles);
    ++lotsRevision_;
    queueCommuteRecalculationForLot(lotId);
    return true;
}

// Destroys buildings under a tile. RCI lots turn back into empty redevelopment
// parcels, while roads remain a bulldozer responsibility and zoning remains a
// separate dezoning concern.
bool SimulationRuntime::tryBulldozeAtTile(int clickedTileX, int clickedTileY, TileBuffer& writeBuffer) {
    const int tileLinearIndex = tileIndex(clickedTileX, clickedTileY);
    const int lotId = lotOccupancy_[tileLinearIndex];
    if (lotId != kInvalidLotId) {
        std::size_t lotIndex = 0;
        for (; lotIndex < lots_.size(); ++lotIndex) {
            if (lots_[lotIndex].id() != lotId) {
                continue;
            }

            const std::vector<int> oldOccupiedTiles = lots_[lotIndex].occupiedTileIndices();
            const LotAsset* lotAsset = findLotAssetById(lots_[lotIndex].assetId());
            if (lotAsset != 0 && lotAsset->zoningType != TileZoningNone) {
                if (lots_[lotIndex].modules().empty()) {
                    return false;
                }

                return exposeRciLotForRedevelopment(lotIndex, *lotAsset, writeBuffer, simulationTick_ + kRciRedevelopmentGraceTicks);
            }

            queueCommuteSourcesForDestination(lotId);
            removeCommuteLoadsForLot(lots_[lotIndex]);
            clearLotOccupancy(oldOccupiedTiles);
            lots_.erase(lots_.begin() + static_cast<std::ptrdiff_t>(lotIndex));
            markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, oldOccupiedTiles);
            ++lotsRevision_;
            return true;
        }

        return false;
    }

    if (transportNetwork_.removeRoadAtTile(clickedTileX, clickedTileY)) {
        commutesDirty_ = true;
        return true;
    }

    return false;
}

bool SimulationRuntime::tryBulldozeArea(int startTileX, int startTileY, int endTileX, int endTileY, TileBuffer& writeBuffer) {
    const int minTileX = std::max(0, std::min(startTileX, endTileX));
    const int maxTileX = std::min(kMapWidth - 1, std::max(startTileX, endTileX));
    const int minTileY = std::max(0, std::min(startTileY, endTileY));
    const int maxTileY = std::min(kMapHeight - 1, std::max(startTileY, endTileY));

    bool removedAny = false;
    int tileY = minTileY;
    for (; tileY <= maxTileY; ++tileY) {
        int tileX = minTileX;
        for (; tileX <= maxTileX; ++tileX) {
            removedAny = tryBulldozeAtTile(tileX, tileY, writeBuffer) || removedAny;
        }
    }

    return removedAny;
}

bool SimulationRuntime::tryZoneArea(int startTileX, int startTileY, int endTileX, int endTileY, std::uint16_t zoningType, TileBuffer& writeBuffer) {
    if (zoningType == TileZoningNone) {
        return false;
    }

    const RciRect rect(
        std::max(0, std::min(startTileX, endTileX)),
        std::max(0, std::min(startTileY, endTileY)),
        std::min(kMapWidth - 1, std::max(startTileX, endTileX)),
        std::min(kMapHeight - 1, std::max(startTileY, endTileY)));

    std::vector<int> changedTileIndices;
    bool hasZoneableTile = false;
    const bool changedTiles = applyZoningRect(rect, zoningType, writeBuffer, changedTileIndices, hasZoneableTile);
    if (!changedTiles) {
        return false;
    }

    if (!changedTileIndices.empty()) {
        markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, changedTileIndices);
    }
    return true;
}

bool SimulationRuntime::tryZoneLot(const RciLot& zoningLot, TileBuffer& writeBuffer) {
    if (zoningLot.zoningType == TileZoningNone || !zoningLot.rect.isValid()) {
        return false;
    }

    const RciRect rect(
        std::max(0, zoningLot.rect.minTileX),
        std::max(0, zoningLot.rect.minTileY),
        std::min(kMapWidth - 1, zoningLot.rect.maxTileX),
        std::min(kMapHeight - 1, zoningLot.rect.maxTileY));
    if (!rect.isValid()) {
        return false;
    }

    if (!isZoningRectFullyZoneable(rect, writeBuffer, true)) {
        return false;
    }

    std::vector<int> changedTileIndices;
    bool hasZoneableTile = false;
    applyZoningRect(rect, zoningLot.zoningType, writeBuffer, changedTileIndices, hasZoneableTile);
    if (!hasZoneableTile) {
        return false;
    }

    removeZoningLotsIntersectingRect(rect);
    RciLot storedLot = zoningLot;
    storedLot.rect = rect;
    zoningLots_.push_back(storedLot);
    ++zoningLotsRevision_;

    if (!changedTileIndices.empty()) {
        markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, changedTileIndices);
    }
    return true;
}

bool SimulationRuntime::applyZoningRect(const RciRect& rect, std::uint16_t zoningType, TileBuffer& writeBuffer, std::vector<int>& changedTileIndices, bool& hasZoneableTile) {
    changedTileIndices.clear();
    hasZoneableTile = false;
    if (!rect.isValid() || zoningType == TileZoningNone) {
        return false;
    }

    changedTileIndices.reserve(static_cast<std::size_t>(rect.width()) * static_cast<std::size_t>(rect.height()));
    int tileY = rect.minTileY;
    for (; tileY <= rect.maxTileY; ++tileY) {
        int tileX = rect.minTileX;
        for (; tileX <= rect.maxTileX; ++tileX) {
            const int tileLinearIndex = tileIndex(tileX, tileY);
            Tile& tile = writeBuffer.tiles[tileLinearIndex];
            if (!isTileZoneableForRci(tileLinearIndex, tile)) {
                continue;
            }

            hasZoneableTile = true;
            if (tile.zoningType == zoningType) {
                continue;
            }

            tile.zoningType = zoningType;
            changedTileIndices.push_back(tileLinearIndex);
        }
    }

    return !changedTileIndices.empty();
}

// Used by lot-mode zoning before it creates a persistent parcel record. Area
// zoning may tint existing RCI lots, but new empty parcels must not overlap any
// live lot or the constructor would later fight lot occupancy.
bool SimulationRuntime::isZoningRectFullyZoneable(const RciRect& rect, const TileBuffer& writeBuffer, bool requireUnoccupied) const {
    if (!rect.isValid()) {
        return false;
    }

    int tileY = rect.minTileY;
    for (; tileY <= rect.maxTileY; ++tileY) {
        int tileX = rect.minTileX;
        for (; tileX <= rect.maxTileX; ++tileX) {
            const int tileLinearIndex = tileIndex(tileX, tileY);
            if (!isTileZoneableForRci(tileLinearIndex, writeBuffer.tiles[tileLinearIndex])) {
                return false;
            }
            if (requireUnoccupied && lotOccupancy_[tileLinearIndex] != kInvalidLotId) {
                return false;
            }
        }
    }

    return true;
}

bool SimulationRuntime::isTileZoneableForRci(int tileLinearIndex, const Tile& tile) const {
    if (tileLinearIndex < 0 || tileLinearIndex >= static_cast<int>(lotOccupancy_.size())) {
        return false;
    }

    if (transportNetwork_.hasGroundOccupancy(tileLinearIndex)) {
        return false;
    }

    if (lotOccupancy_[tileLinearIndex] != kInvalidLotId) {
        return !tileHasBlockingNonRciLot(tileLinearIndex);
    }

    return tile.isVacant;
}

bool SimulationRuntime::tileHasBlockingNonRciLot(int tileLinearIndex) const {
    if (tileLinearIndex < 0 || tileLinearIndex >= static_cast<int>(lotOccupancy_.size())) {
        return true;
    }

    const int lotId = lotOccupancy_[tileLinearIndex];
    if (lotId == kInvalidLotId) {
        return false;
    }

    std::size_t lotIndex = 0;
    for (; lotIndex < lots_.size(); ++lotIndex) {
        if (lots_[lotIndex].id() != lotId) {
            continue;
        }

        const LotAsset* lotAsset = findLotAssetById(lots_[lotIndex].assetId());
        return lotAsset == 0 || lotAsset->zoningType == TileZoningNone;
    }

    return true;
}

bool SimulationRuntime::removeZoningLotsIntersectingRect(const RciRect& rect) {
    if (!rect.isValid() || zoningLots_.empty()) {
        return false;
    }

    const std::size_t oldSize = zoningLots_.size();
    zoningLots_.erase(
        std::remove_if(
            zoningLots_.begin(),
            zoningLots_.end(),
            [&rect](const RciLot& zoningLot) {
                return zoningLot.rect.intersects(rect);
            }),
        zoningLots_.end());

    if (zoningLots_.size() == oldSize) {
        return false;
    }

    ++zoningLotsRevision_;
    return true;
}

void SimulationRuntime::clearZoningForRoadStroke(const RoadStrokeCommand& roadStrokeCommand, TileBuffer& writeBuffer) {
    RoadTemplate roadTemplate = roadStrokeCommand.roadTemplate;
    roadTemplate.family = roadStrokeCommand.family;
    roadTemplate.layer = roadStrokeCommand.layer;
    if (roadTemplate.elements.empty()) {
        roadTemplate = Road::makeTemplate(roadStrokeCommand.family, roadStrokeCommand.layer, 1, RoadTrafficSide::RightHand, RoadDirectionMode::TwoWay);
    }

    std::vector<RoadTilePlacement> placements;
    placements.reserve(512);
    Road road(roadTemplate);
    if (!road.appendStrokePlacements(roadStrokeCommand.startTile, roadStrokeCommand.cornerTile, roadStrokeCommand.endTile, kMapWidth, kMapHeight, placements)) {
        return;
    }

    std::vector<int> changedTileIndices;
    changedTileIndices.reserve(placements.size());
    std::size_t placementIndex = 0;
    for (; placementIndex < placements.size(); ++placementIndex) {
        const int tileLinearIndex = placements[placementIndex].tileIndex;
        if (tileLinearIndex < 0 || tileLinearIndex >= static_cast<int>(writeBuffer.tiles.size())) {
            continue;
        }

        removeZoningLotsIntersectingRect(RciRect(
            placements[placementIndex].tileX,
            placements[placementIndex].tileY,
            placements[placementIndex].tileX,
            placements[placementIndex].tileY));
        if (writeBuffer.tiles[tileLinearIndex].zoningType == TileZoningNone) {
            continue;
        }

        writeBuffer.tiles[tileLinearIndex].zoningType = TileZoningNone;
        changedTileIndices.push_back(tileLinearIndex);
    }

    if (changedTileIndices.empty()) {
        return;
    }

    std::sort(changedTileIndices.begin(), changedTileIndices.end());
    changedTileIndices.erase(std::unique(changedTileIndices.begin(), changedTileIndices.end()), changedTileIndices.end());
    markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, changedTileIndices);
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

bool SimulationRuntime::commuteRouteIsStillValid(const CommuteRouteRecord& route) const {
    if (!route.pathResult.success || route.transportLoad == 0u) {
        return false;
    }

    const TransportCostMap& costMap = transportNetwork_.costMap();
    std::size_t stepIndex = 0;
    for (; stepIndex < route.pathResult.steps.size(); ++stepIndex) {
        const TransportPathStep& step = route.pathResult.steps[stepIndex];
        if (step.kind == TransportPathStepKind::Movement) {
            const int directionIndex = RoadDirectionIndex(step.roadDirection);
            if (directionIndex < 0 || step.fromNodeId >= costMap.totalNodeCount()) {
                return false;
            }

            const TransportCostCell& cell = costMap.cell(costMap.nodeLayer(step.fromNodeId), costMap.nodeMode(step.fromNodeId), costMap.nodeTileIndex(step.fromNodeId));
            if (cell.costs[directionIndex] == kTransportNoCost || cell.capacities[directionIndex] == 0u) {
                return false;
            }
        }
    }

    return true;
}

// Validates a tile coordinate against the fixed map bounds.
bool SimulationRuntime::isTileInsideMap(int tileX, int tileY) const {
    return tileX >= 0 && tileX < kMapWidth && tileY >= 0 && tileY < kMapHeight;
}

// Converts a tile coordinate to row-major storage index.
int SimulationRuntime::tileIndex(int tileX, int tileY) const {
    return (tileY * kMapWidth) + tileX;
}
