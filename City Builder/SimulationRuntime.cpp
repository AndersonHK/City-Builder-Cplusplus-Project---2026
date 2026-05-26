#include "SimulationRuntime.h"

#include "AssetLoader.h"
#include "CrashLogger.h"
#include "LotAutoLayoutResolver.h"
#include "LotModulePlacementGeometry.h"
#include "RendererPayload.h"
#include "RuntimePaths.h"
#include "RoadTemplateDefinition.h"
#include "SimulationTime.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {
// Converts a steady-clock span to microseconds for lightweight profiling.
long long DurationMicros(const std::chrono::steady_clock::time_point& startTime, const std::chrono::steady_clock::time_point& endTime) {
    return std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
}

const float kTransportCostUnitsPerSecond = 1000.0f;
const float kMaximumCommuteTime = 600.0f;
const float kMaximumCommuteCost = kMaximumCommuteTime * kTransportCostUnitsPerSecond;
const float kLongCommuteComplaintCost = kMaximumCommuteCost * 0.5f;
const int kRciRedevelopmentGraceDays = 30;
const int kRciConstructorBlockLimit = 8;
const float kDirtyIndustryDemandPerLowWealthWorker = 1.05f;
const int kRciRoadFacingNorth = 0;
const int kRciRoadFacingSouth = 1;
const int kRciRoadFacingWest = 2;
const int kRciRoadFacingEast = 3;

std::string ToLowerAsciiLocal(const std::string& value) {
    std::string lowered = value;
    std::size_t characterIndex = 0;
    for (; characterIndex < lowered.size(); ++characterIndex) {
        if (lowered[characterIndex] >= 'A' && lowered[characterIndex] <= 'Z') {
            lowered[characterIndex] = static_cast<char>(lowered[characterIndex] - 'A' + 'a');
        }
    }
    return lowered;
}

bool IsNoneModuleAlternativeId(const std::string& moduleId) {
    const std::string normalized = ToLowerAsciiLocal(moduleId);
    return normalized == "none" || normalized == "empty" || normalized == "null" || normalized == "skip";
}
const std::size_t kInvalidLotReportLimit = 8u;

int ApplyEnvironmentalDecay(int value, int decayRate) {
    if (value == 0) {
        return 0;
    }

    const int magnitude = std::abs(value);
    const int decayStep = std::max(1, magnitude / decayRate);
    if (value > 0) {
        return std::max(0, value - decayStep);
    }

    return std::min(0, value + decayStep);
}

int RoundToNearestInt(float value) {
    return static_cast<int>(std::floor(value + 0.5f));
}

float RciCapacityScaleForDesirability(int desirability) {
    const float normalized = static_cast<float>(std::max(0, std::min(kRciDesirabilityDisplayCap, desirability))) /
        static_cast<float>(kRciDesirabilityDisplayCap);
    if (normalized <= 0.4f) {
        return std::max(0.0f, normalized * 1.25f);
    }
    if (normalized < 0.6f) {
        return 0.5f + ((normalized - 0.4f) / 0.2f) * 0.5f;
    }
    return 1.0f;
}

int RciActualCapacityFromDesirability(int maximumCapacity, int desirability) {
    if (maximumCapacity <= 0) {
        return 0;
    }

    const int actualCapacity = RoundToNearestInt(static_cast<float>(maximumCapacity) * RciCapacityScaleForDesirability(desirability));
    return std::max(0, std::min(maximumCapacity, actualCapacity));
}

std::uint64_t RciRedevelopmentGraceTicks() {
    return SimulationTime::daysToTicks(static_cast<std::uint64_t>(kRciRedevelopmentGraceDays));
}

CommuteCategory CommuteCategoryForPath(const TransportPathResult& pathResult) {
    if (!pathResult.success) {
        return CommuteCategory::None;
    }
    if (pathResult.totalCost >= kMaximumCommuteCost) {
        return CommuteCategory::Long;
    }
    if (pathResult.totalCost >= kLongCommuteComplaintCost) {
        return CommuteCategory::Medium;
    }
    return CommuteCategory::Short;
}

void AccumulatePublishedCommuteCategory(PublishedLotInfo& publishedLotInfo, const CommuteRouteRecord& route) {
    if (route.demand <= 0) {
        return;
    }

    const CommuteCategory morningCategory = CommuteCategoryForPath(route.morningPathResult);
    const CommuteCategory eveningCategory = CommuteCategoryForPath(route.eveningPathResult);
    const CommuteCategory routeCategory = static_cast<int>(morningCategory) > static_cast<int>(eveningCategory)
        ? morningCategory
        : eveningCategory;
    if (static_cast<int>(routeCategory) > static_cast<int>(publishedLotInfo.worstCommuteCategory)) {
        publishedLotInfo.worstCommuteCategory = routeCategory;
    }
}

bool IsRciZoningType(std::uint16_t zoningType) {
    return zoningType == TileZoningResidentialLow ||
        zoningType == TileZoningResidentialHigh ||
        zoningType == TileZoningIndustrial;
}

std::uint16_t RciZoningFamily(std::uint16_t zoningType) {
    if (zoningType == TileZoningResidentialLow || zoningType == TileZoningResidentialHigh) {
        return TileZoningResidentialHigh;
    }

    return zoningType;
}

int ClampTileCoordinate(int value, int maximum) {
    return std::max(0, std::min(value, maximum));
}

RciRect NormalizeRciBounds(int startTileX, int startTileY, int endTileX, int endTileY, int mapWidth, int mapHeight) {
    if (mapWidth <= 0 || mapHeight <= 0) {
        return RciRect();
    }

    return RciRect(
        ClampTileCoordinate(std::min(startTileX, endTileX), mapWidth - 1),
        ClampTileCoordinate(std::min(startTileY, endTileY), mapHeight - 1),
        ClampTileCoordinate(std::max(startTileX, endTileX), mapWidth - 1),
        ClampTileCoordinate(std::max(startTileY, endTileY), mapHeight - 1));
}

std::uint8_t RoadAxisMaskForResolvedCell(const ResolvedRoadCell& roadCell) {
    std::uint8_t axisMask = 0u;
    if (HasHorizontalLane(roadCell.travelMask)) {
        axisMask |= AxisMaskFor(RoadAxis::Horizontal);
    }
    if (HasVerticalLane(roadCell.travelMask)) {
        axisMask |= AxisMaskFor(RoadAxis::Vertical);
    }
    if (axisMask == 0u && roadCell.family != static_cast<std::uint8_t>(RoadFamily::None)) {
        axisMask = AxisMaskFor(RoadAxis::Horizontal) | AxisMaskFor(RoadAxis::Vertical);
    }
    return axisMask;
}

const char* RciNameForZoningType(std::uint16_t zoningType) {
    if (zoningType == TileZoningResidentialLow) {
        return "Low Density Residence";
    }
    if (zoningType == TileZoningResidentialHigh) {
        return "High Density Residence";
    }
    if (zoningType == TileZoningIndustrial) {
        return "Industry";
    }
    return "Unzoned";
}

float RciStarterDensityFloor(std::uint16_t zoningType) {
    if (zoningType == TileZoningResidentialLow || zoningType == TileZoningResidentialHigh) {
        return 0.65f;
    }

    if (zoningType == TileZoningIndustrial) {
        return 0.75f;
    }

    return 0.0f;
}

int ClampDesirability(int desirability) {
    return std::max(kRciDesirabilityDisplayMinimum, std::min(desirability, kRciDesirabilityDisplayCap));
}

int RciDesirabilityFieldRawValue(RciDesirabilityField field, const Tile& tile) {
    if (field == RciDesirabilityField::ParkEffect) {
        return std::max(0, tile.parkEffect);
    }

    return std::max(0, tile.airPollution);
}

int EvaluateDesirabilitySensitivity(const RciDesirabilitySensitivity& sensitivity, const Tile& tile) {
    if (sensitivity.points.empty()) {
        return 0;
    }

    const float normalizedValue = static_cast<float>(RciDesirabilityFieldRawValue(sensitivity.field, tile)) /
        static_cast<float>(std::max(1, sensitivity.normalizer));
    if (normalizedValue <= sensitivity.points.front().value || sensitivity.points.size() == 1u) {
        return sensitivity.points.front().desirabilityDelta;
    }

    std::size_t pointIndex = 1;
    for (; pointIndex < sensitivity.points.size(); ++pointIndex) {
        if (normalizedValue > sensitivity.points[pointIndex].value) {
            continue;
        }

        const RciDesirabilityPoint& lower = sensitivity.points[pointIndex - 1u];
        const RciDesirabilityPoint& upper = sensitivity.points[pointIndex];
        const float pointSpan = upper.value - lower.value;
        if (pointSpan <= 0.0f) {
            return upper.desirabilityDelta;
        }

        const float interpolation = (normalizedValue - lower.value) / pointSpan;
        return static_cast<int>(lower.desirabilityDelta + ((upper.desirabilityDelta - lower.desirabilityDelta) * interpolation));
    }

    return sensitivity.points.back().desirabilityDelta;
}

const char* RciToolIdForZoningType(std::uint16_t zoningType) {
    if (zoningType == TileZoningResidentialLow) {
        return "residential_low";
    }
    if (zoningType == TileZoningResidentialHigh) {
        return "residential_high";
    }
    if (zoningType == TileZoningIndustrial) {
        return "industrial";
    }
    return "rci";
}

std::string DefaultRciTypeIdForZoningType(std::uint16_t zoningType) {
    if (zoningType == TileZoningResidentialLow || zoningType == TileZoningResidentialHigh) {
        return "low_wealth_residential";
    }
    if (zoningType == TileZoningIndustrial) {
        return "dirty_industry";
    }
    return std::string();
}

RciColor RciColorForZoningType(std::uint16_t zoningType) {
    if (zoningType == TileZoningResidentialLow) {
        return RciColor(0.44f, 0.92f, 0.46f, 0.50f);
    }
    if (zoningType == TileZoningResidentialHigh) {
        return RciColor(0.10f, 0.48f, 0.20f, 0.50f);
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

bool RciRectContainsRect(const RciRect& outer, const RciRect& inner) {
    return outer.isValid() && inner.isValid() &&
        inner.minTileX >= outer.minTileX &&
        inner.maxTileX <= outer.maxTileX &&
        inner.minTileY >= outer.minTileY &&
        inner.maxTileY <= outer.maxTileY;
}

bool RciRectContainsTile(const RciRect& rect, int tileX, int tileY) {
    return rect.isValid() &&
        tileX >= rect.minTileX &&
        tileX <= rect.maxTileX &&
        tileY >= rect.minTileY &&
        tileY <= rect.maxTileY;
}

RciRect RciUnionRect(const RciRect& first, const RciRect& second) {
    if (!first.isValid()) {
        return second;
    }
    if (!second.isValid()) {
        return first;
    }

    return RciRect(
        std::min(first.minTileX, second.minTileX),
        std::min(first.minTileY, second.minTileY),
        std::max(first.maxTileX, second.maxTileX),
        std::max(first.maxTileY, second.maxTileY));
}

bool RciRectsAreEdgeAdjacent(const RciRect& first, const RciRect& second) {
    if (!first.isValid() || !second.isValid()) {
        return false;
    }

    const bool verticalOverlap = first.minTileY <= second.maxTileY && first.maxTileY >= second.minTileY;
    const bool horizontalOverlap = first.minTileX <= second.maxTileX && first.maxTileX >= second.minTileX;
    return (verticalOverlap && (first.maxTileX + 1 == second.minTileX || second.maxTileX + 1 == first.minTileX)) ||
        (horizontalOverlap && (first.maxTileY + 1 == second.minTileY || second.maxTileY + 1 == first.minTileY));
}

void AddUniqueBoundary(std::vector<int>& boundaries, int value) {
    if (std::find(boundaries.begin(), boundaries.end(), value) == boundaries.end()) {
        boundaries.push_back(value);
    }
}

void MixRciHash(std::uint32_t& hash, std::uint32_t value) {
    hash ^= value;
    hash *= 16777619u;
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

std::string JoinTruncatedReports(const std::vector<std::string>& reports, const std::string& source) {
    std::ostringstream message;
    message << reports.size() << " " << source << " could not load and were removed from runtime memory.";
    const std::size_t reportLimit = std::min(kInvalidLotReportLimit, reports.size());
    std::size_t reportIndex = 0;
    for (; reportIndex < reportLimit; ++reportIndex) {
        message << "\n- " << reports[reportIndex];
    }
    if (reports.size() > reportLimit) {
        message << "\n- ... and " << (reports.size() - reportLimit) << " more";
    }
    return message.str();
}

void ShowNonFatalAssetWarning(const std::string& title, const std::string& message, const RuntimeOptions& runtimeOptions) {
    LogWarning(title, message);
    if (runtimeOptions.showNonFatalAssetWarningDialogs && runtimeOptions.nonFatalAssetWarningHandler) {
        runtimeOptions.nonFatalAssetWarningHandler(title, message);
    }
}
}

// Allocates the triple-buffered world and derives chunk/work scheduling config.
SimulationRuntime::SimulationRuntime(const RuntimeOptions& runtimeOptions)
    : runtimeOptions_(runtimeOptions),
      mapWidth_(std::max(1, runtimeOptions.mapWidth)),
      mapHeight_(std::max(1, runtimeOptions.mapHeight)),
      livePreviewValidationWaiters_(0),
      simulationReadBufferIndex_(0),
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
      rciConstructorMergeCapacityDiscount_(0.2f),
      rciConstructorRedevelopmentCapacityIncrease_(0.2f),
      rciBaselineLandValue_(0),
      commuteRevision_(0),
      commutesDirty_(true),
      commuteRebalanceCursor_(0),
      keepRunning_(false),
      gameSpeed_(runtimeOptions.fastForward ? GameSpeed::FastForward : GameSpeed::Fast),
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
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    const int workerThreadCount = hardwareThreads > 2u ? static_cast<int>(hardwareThreads) - 2 : 1;
    chunkConfig_ = CalculateChunkConfig(mapWidth_, mapHeight_, sizeof(Tile), workerThreadCount, runtimeOptions_.manualL2BytesPerLogicalThread, runtimeOptions_.detectL2CacheSize, runtimeOptions_.usableL2Fraction, kMinimumJobsPerWorkerMultiplier);
    chunkLayout_ = BuildChunkLayout(mapWidth_, mapHeight_, chunkConfig_.chunkWidth, chunkConfig_.chunkHeight);
    transportNetwork_.initialize(mapWidth_, mapHeight_, chunkLayout_);

    loadAssets();

    tileBuffers_.resize(3);
    const std::size_t totalTileCount = static_cast<std::size_t>(mapWidth_) * static_cast<std::size_t>(mapHeight_);
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
        tileBuffers_[bufferIndex].publishedTileOverlayState.assign(totalTileCount, 0u);
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
    speedCv_.notify_all();
    livePreviewValidationCv_.notify_all();

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

void SimulationRuntime::setGameSpeed(GameSpeed gameSpeed) {
    gameSpeed_.store(gameSpeed);
    if (gameSpeed == GameSpeed::Paused) {
        updatesPerSecond_.store(0);
    }
    speedCv_.notify_all();
    renderCv_.notify_all();
}

GameSpeed SimulationRuntime::gameSpeed() const {
    return gameSpeed_.load();
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
    playerCommand.tileX = std::max(0, std::min(startTileX, mapWidth_ - 1));
    playerCommand.tileY = std::max(0, std::min(startTileY, mapHeight_ - 1));
    playerCommand.endTileX = std::max(0, std::min(endTileX, mapWidth_ - 1));
    playerCommand.endTileY = std::max(0, std::min(endTileY, mapHeight_ - 1));
    enqueueCommand(playerCommand);
}

void SimulationRuntime::queueZoneArea(int startTileX, int startTileY, int endTileX, int endTileY, std::uint16_t zoningType) {
    if (!isTileInsideMap(startTileX, startTileY) && !isTileInsideMap(endTileX, endTileY)) {
        return;
    }

    PlayerCommand playerCommand;
    playerCommand.type = PlayerCommandType::ZoneArea;
    playerCommand.tileX = std::max(0, std::min(startTileX, mapWidth_ - 1));
    playerCommand.tileY = std::max(0, std::min(startTileY, mapHeight_ - 1));
    playerCommand.endTileX = std::max(0, std::min(endTileX, mapWidth_ - 1));
    playerCommand.endTileY = std::max(0, std::min(endTileY, mapHeight_ - 1));
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
    playerCommand.tileX = std::max(0, std::min(zoningLot.rect.minTileX, mapWidth_ - 1));
    playerCommand.tileY = std::max(0, std::min(zoningLot.rect.minTileY, mapHeight_ - 1));
    playerCommand.endTileX = std::max(0, std::min(zoningLot.rect.maxTileX, mapWidth_ - 1));
    playerCommand.endTileY = std::max(0, std::min(zoningLot.rect.maxTileY, mapHeight_ - 1));
    playerCommand.zoningLot = zoningLot;
    playerCommand.zoningLot.rect = RciRect(playerCommand.tileX, playerCommand.tileY, playerCommand.endTileX, playerCommand.endTileY);
    enqueueCommand(playerCommand);
}

void SimulationRuntime::queueRciPlan(const RciPlan& plan) {
    if (plan.zoningType == TileZoningNone || !plan.bounds.isValid()) {
        return;
    }

    if (!isTileInsideMap(plan.bounds.minTileX, plan.bounds.minTileY) &&
        !isTileInsideMap(plan.bounds.maxTileX, plan.bounds.maxTileY)) {
        return;
    }

    PlayerCommand playerCommand;
    playerCommand.type = PlayerCommandType::ApplyRciPlan;
    playerCommand.tileX = ClampTileCoordinate(plan.bounds.minTileX, mapWidth_ - 1);
    playerCommand.tileY = ClampTileCoordinate(plan.bounds.minTileY, mapHeight_ - 1);
    playerCommand.endTileX = ClampTileCoordinate(plan.bounds.maxTileX, mapWidth_ - 1);
    playerCommand.endTileY = ClampTileCoordinate(plan.bounds.maxTileY, mapHeight_ - 1);
    playerCommand.rciPlan = plan;
    playerCommand.rciPlan.bounds = RciRect(playerCommand.tileX, playerCommand.tileY, playerCommand.endTileX, playerCommand.endTileY);
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
    roadStrokeCommand.templateKind = RoadTemplateKind::Street;
    roadStrokeCommand.family = RoadFamily::LocalStreet;
    roadStrokeCommand.layer = TransportLayerId::Ground;
    roadStrokeCommand.roadTemplate = TransportNetwork::makeRoadTemplate(roadStrokeCommand.templateKind, RoadTrafficSide::RightHand, RoadDirectionMode::TwoWay);
    queuePlaceRoadStroke(roadStrokeCommand);
}

// Queues an elevated highway stroke.
void SimulationRuntime::queuePlaceHighwayRoad(const Int2& startTile, const Int2& cornerTile, const Int2& endTile) {
    RoadStrokeCommand roadStrokeCommand;
    roadStrokeCommand.startTile = startTile;
    roadStrokeCommand.cornerTile = cornerTile;
    roadStrokeCommand.endTile = endTile;
    roadStrokeCommand.templateKind = RoadTemplateKind::Highway;
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
    std::unique_lock<std::mutex> liveStateLock = acquireLiveStateForPreviewValidation();
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

    std::unique_lock<std::mutex> liveStateLock = acquireLiveStateForPreviewValidation();
    return transportNetwork_.canPlaceRoadStroke(roadStrokeCommand, lotOccupancy_, kInvalidLotId);
}

bool SimulationRuntime::buildRciPlan(const RciTool& tool, int startTileX, int startTileY, int endTileX, int endTileY, RciPlanMode mode, RciPlan& plan) const {
    return buildRciPlanWithLiveStateLock(tool, startTileX, startTileY, endTileX, endTileY, mode, plan);
}

bool SimulationRuntime::buildRciPlanWithLiveStateLock(const RciTool& tool, int startTileX, int startTileY, int endTileX, int endTileY, RciPlanMode mode, RciPlan& plan) const {
    plan = RciPlan();
    const RciRect bounds = NormalizeRciBounds(startTileX, startTileY, endTileX, endTileY, mapWidth_, mapHeight_);
    if (!bounds.isValid()) {
        return false;
    }

    const std::size_t totalTiles = static_cast<std::size_t>(mapWidth_) * static_cast<std::size_t>(mapHeight_);
    RciPlanningContext context;
    context.mapWidth = mapWidth_;
    context.mapHeight = mapHeight_;
    context.bounds = bounds;
    context.mode = mode;
    context.paintableTiles.assign(totalTiles, 0u);
    context.groundRoadTiles.assign(totalTiles, 0u);
    context.groundRoadAxisMasks.assign(totalTiles, 0u);

    {
        std::unique_lock<std::mutex> liveStateLock = acquireLiveStateForPreviewValidation();
        const TileBuffer& readBuffer = tileBuffers_[simulationReadBufferIndex_];
        const std::vector<ResolvedRoadCell>& resolvedRoads = transportNetwork_.resolvedCells();
        std::size_t tileIndexValue = 0u;
        for (; tileIndexValue < totalTiles; ++tileIndexValue) {
            if (!transportNetwork_.hasGroundOccupancy(static_cast<int>(tileIndexValue))) {
                continue;
            }

            context.groundRoadTiles[tileIndexValue] = 1u;
            const std::size_t roadSlot = TransportNetwork::slotIndex(TransportLayerId::Ground, static_cast<int>(tileIndexValue), transportNetwork_.totalTileCount());
            if (roadSlot < resolvedRoads.size()) {
                context.groundRoadAxisMasks[tileIndexValue] = RoadAxisMaskForResolvedCell(resolvedRoads[roadSlot]);
            }
            if (context.groundRoadAxisMasks[tileIndexValue] == 0u) {
                context.groundRoadAxisMasks[tileIndexValue] = AxisMaskFor(RoadAxis::Horizontal) | AxisMaskFor(RoadAxis::Vertical);
            }
        }

        int tileY = bounds.minTileY;
        for (; tileY <= bounds.maxTileY; ++tileY) {
            int tileX = bounds.minTileX;
            for (; tileX <= bounds.maxTileX; ++tileX) {
                const int linearIndex = tileIndex(tileX, tileY);
                if (linearIndex < 0 ||
                    linearIndex >= static_cast<int>(totalTiles) ||
                    linearIndex >= static_cast<int>(readBuffer.tiles.size()) ||
                    transportNetwork_.hasGroundOccupancy(linearIndex) ||
                    lotOccupancy_[static_cast<std::size_t>(linearIndex)] != kInvalidLotId ||
                    !readBuffer.tiles[static_cast<std::size_t>(linearIndex)].isVacant) {
                    continue;
                }

                context.paintableTiles[static_cast<std::size_t>(linearIndex)] = 1u;
            }
        }
    }

    return tool.buildPlan(context, plan);
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
                queryResult.worstCommuteCategory = publishedLotInfo->worstCommuteCategory;
                queryResult.residentsLowWealthCurrent = publishedLotInfo->residentsLowWealthCurrent;
                queryResult.residentsLowWealthTotal = publishedLotInfo->residentsLowWealthTotal;
                queryResult.jobsLowWealthCurrent = publishedLotInfo->jobsLowWealthCurrent;
                queryResult.jobsLowWealthTotal = publishedLotInfo->jobsLowWealthTotal;
                queryResult.rciCapacityCurrent = publishedLotInfo->rciCapacityCurrent;
                queryResult.rciCapacityMaximum = publishedLotInfo->rciCapacityMaximum;
                queryResult.complaintSummary = publishedLotInfo->complaintSummary;
                queryResult.commuteRouteSegments = publishedLotInfo->commuteRouteSegments;
                if (!publishedLotInfo->isEmpty && publishedLotInfo->zoningType != TileZoningNone) {
                    const LotAsset* lotAsset = findLotAssetById(publishedLotInfo->assetId);
                    if (lotAsset != 0) {
                        queryResult.rciLandValueLevel = rciLandValueLevelForLot(*publishedLotInfo, *lotAsset, publishedBuffer);
                    }
                }
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
            const std::size_t slot = TransportNetwork::slotIndex(static_cast<TransportLayerId>(layerIndex), queriedTileIndex, static_cast<std::size_t>(mapWidth_) * static_cast<std::size_t>(mapHeight_));
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
    std::lock_guard<std::mutex> liveStateLock(liveStateMutex_);
    CitySaveState saveState;
    saveState.width = mapWidth_;
    saveState.height = mapHeight_;
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
            moduleSaveState.footprintWidth = modules[moduleIndex].footprintWidth;
            moduleSaveState.footprintHeight = modules[moduleIndex].footprintHeight;
            moduleSaveState.renderOffsetX = modules[moduleIndex].renderOffsetX;
            moduleSaveState.renderOffsetY = modules[moduleIndex].renderOffsetY;
            moduleSaveState.renderWidth = modules[moduleIndex].renderWidth;
            moduleSaveState.renderHeight = modules[moduleIndex].renderHeight;
            moduleSaveState.affectsSimulation = modules[moduleIndex].affectsSimulation;
            moduleSaveState.claimsFootprint = modules[moduleIndex].claimsFootprint;
            lotSaveState.modules.push_back(moduleSaveState);
        }

        saveState.lots.push_back(lotSaveState);
        lot.buildRenderInstances(saveState.previewLots);
    }

    return saveState;
}

void SimulationRuntime::importCitySaveState(const CitySaveState& saveState, bool reportInvalidSavedLots) {
    CrashScope crashScope("SimulationRuntime::importCitySaveState");
    std::lock_guard<std::mutex> liveStateLock(liveStateMutex_);
    const std::size_t totalTileCount = static_cast<std::size_t>(mapWidth_) * static_cast<std::size_t>(mapHeight_);
    if (saveState.width != mapWidth_ || saveState.height != mapHeight_ || saveState.tiles.size() != totalTileCount) {
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
    rciConstructorSourceCursors_.clear();
    nextLotId_ = std::max(1, saveState.nextLotId);
    simulationTick_ = saveState.simulationTick;

    std::vector<std::string> invalidSavedLotReports;
    std::size_t savedLotIndex = 0;
    for (; savedLotIndex < saveState.lots.size(); ++savedLotIndex) {
        const CitySaveLotState& lotSaveState = saveState.lots[savedLotIndex];
        const LotAsset* lotAsset = findLotAssetById(lotSaveState.assetId);
        const std::string fallbackLotLabel = lotSaveState.assetId.empty()
            ? ("lot id " + std::to_string(lotSaveState.lotId))
            : lotSaveState.assetId;
        if (lotAsset == 0) {
            invalidSavedLotReports.push_back(fallbackLotLabel + ": missing lot asset");
            continue;
        }
        const std::string lotLabel = lotAsset->name.empty() ? fallbackLotLabel : lotAsset->name;

        Lot lot(lotSaveState.lotId, lotSaveState.assetId, lotSaveState.anchorTileX, lotSaveState.anchorTileY, lotSaveState.rotationSteps);
        if (lotAsset->footprintWidth > 0 && lotAsset->footprintHeight > 0) {
            Int2 footprintMinimum;
            Int2 footprintMaximum;
            RotatedRectangleBounds(lotAsset->footprintOrigin, lotAsset->footprintWidth, lotAsset->footprintHeight, lotSaveState.rotationSteps, footprintMinimum, footprintMaximum);
            lot.setExplicitFootprint(
                footprintMinimum,
                footprintMaximum.x - footprintMinimum.x + 1,
                footprintMaximum.y - footprintMinimum.y + 1,
                mapWidth_);
        }

        bool hasInvalidModule = false;
        std::size_t moduleIndex = 0;
        for (; moduleIndex < lotSaveState.modules.size(); ++moduleIndex) {
            const LotModule* moduleAsset = findModuleAssetById(lotSaveState.modules[moduleIndex].moduleAssetId);
            if (moduleAsset == 0) {
                invalidSavedLotReports.push_back(lotLabel + ": missing module " + lotSaveState.modules[moduleIndex].moduleAssetId);
                hasInvalidModule = true;
                break;
            }

            int rotatedModuleWidth = 0;
            int rotatedModuleHeight = 0;
            float rotatedRenderOffsetX = 0.0f;
            float rotatedRenderOffsetY = 0.0f;
            float rotatedRenderWidth = 0.0f;
            float rotatedRenderHeight = 0.0f;
            if (lotSaveState.modules[moduleIndex].footprintWidth > 0 &&
                lotSaveState.modules[moduleIndex].footprintHeight > 0 &&
                lotSaveState.modules[moduleIndex].renderWidth > 0.0f &&
                lotSaveState.modules[moduleIndex].renderHeight > 0.0f) {
                rotatedModuleWidth = lotSaveState.modules[moduleIndex].footprintWidth;
                rotatedModuleHeight = lotSaveState.modules[moduleIndex].footprintHeight;
                rotatedRenderOffsetX = lotSaveState.modules[moduleIndex].renderOffsetX;
                rotatedRenderOffsetY = lotSaveState.modules[moduleIndex].renderOffsetY;
                rotatedRenderWidth = lotSaveState.modules[moduleIndex].renderWidth;
                rotatedRenderHeight = lotSaveState.modules[moduleIndex].renderHeight;
            } else if (moduleIndex < lotAsset->initialModules.size()) {
                const LotModulePlacementGeometry unrotatedGeometry = ResolveLotModulePlacementGeometry(lotAsset->initialModules[moduleIndex], *moduleAsset);
                const LotModulePlacementGeometry rotatedGeometry = RotateLotModulePlacementGeometry(unrotatedGeometry, lotSaveState.rotationSteps);
                rotatedModuleWidth = rotatedGeometry.footprintWidth;
                rotatedModuleHeight = rotatedGeometry.footprintHeight;
                rotatedRenderOffsetX = rotatedGeometry.renderOffsetX;
                rotatedRenderOffsetY = rotatedGeometry.renderOffsetY;
                rotatedRenderWidth = rotatedGeometry.renderWidth;
                rotatedRenderHeight = rotatedGeometry.renderHeight;
            } else {
                RotatedRectangleDimensions(Int2(0, 0), moduleAsset->width, moduleAsset->height, lotSaveState.rotationSteps, rotatedModuleWidth, rotatedModuleHeight);
            }
            lot.addModule(
                *moduleAsset,
                lotSaveState.modules[moduleIndex].localOrigin,
                mapWidth_,
                rotatedModuleWidth,
                rotatedModuleHeight,
                rotatedRenderOffsetX,
                rotatedRenderOffsetY,
                rotatedRenderWidth,
                rotatedRenderHeight,
                lotSaveState.modules[moduleIndex].affectsSimulation,
                lotSaveState.modules[moduleIndex].claimsFootprint);
        }
        if (hasInvalidModule) {
            continue;
        }
        if (lot.modules().empty()) {
            invalidSavedLotReports.push_back(lotLabel + ": no modules");
            continue;
        }
        lot.setConstructionState(lotSaveState.constructionTotalTicks, lotSaveState.constructionRemainingTicks, mapWidth_);

        bool isInsideMap = true;
        bool overlapsExistingLot = false;
        const std::vector<int>& occupiedTileIndices = lot.occupiedTileIndices();
        std::size_t occupiedIndex = 0;
        for (; occupiedIndex < occupiedTileIndices.size(); ++occupiedIndex) {
            const int tileLinearIndex = occupiedTileIndices[occupiedIndex];
            if (tileLinearIndex < 0 || tileLinearIndex >= static_cast<int>(totalTileCount)) {
                isInsideMap = false;
                break;
            }
            if (lotOccupancy_[static_cast<std::size_t>(tileLinearIndex)] != kInvalidLotId) {
                overlapsExistingLot = true;
                break;
            }
        }

        if (!isInsideMap) {
            invalidSavedLotReports.push_back(lotLabel + ": outside map bounds");
            continue;
        }
        if (overlapsExistingLot) {
            invalidSavedLotReports.push_back(lotLabel + ": overlaps another saved lot");
            continue;
        }

        lots_.push_back(lot);
        setLotOccupancy(lot.id(), lots_.back().occupiedTileIndices());
        nextLotId_ = std::max(nextLotId_, lot.id() + 1);
    }
    if (!invalidSavedLotReports.empty() && reportInvalidSavedLots) {
        ShowNonFatalAssetWarning(
            "City Builder Save Warning",
            JoinTruncatedReports(invalidSavedLotReports, "saved lots"),
            runtimeOptions_);
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
        std::size_t importedTileIndex = 0;
        for (; importedTileIndex < tileBuffers_[bufferIndex].tiles.size(); ++importedTileIndex) {
            Tile& tile = tileBuffers_[bufferIndex].tiles[importedTileIndex];
            tile.landValue = std::max(kLandValueDisplayMinimum, std::min(tile.landValue, kLandValueDisplayCap));
            tile.parkEffect = std::max(0, tile.parkEffect);
        }
        tileBuffers_[bufferIndex].chunkRevisions.assign(chunkLayout_.size(), 2);
        tileBuffers_[bufferIndex].publishedLots.clear();
        tileBuffers_[bufferIndex].publishedLotInfos.clear();
        tileBuffers_[bufferIndex].publishedZoningLots.clear();
        tileBuffers_[bufferIndex].publishedCommuteRouteSegments.clear();
        tileBuffers_[bufferIndex].publishedLotOccupancy.assign(totalTileCount, kInvalidLotId);
        tileBuffers_[bufferIndex].publishedRoads.assign(totalTileCount * TransportNetwork::layerCount(), ResolvedRoadCell());
        tileBuffers_[bufferIndex].publishedGroundRoadRenderState.assign(totalTileCount * kGroundRoadRenderChannelsPerTile, 0);
        tileBuffers_[bufferIndex].publishedTileOverlayState.assign(totalTileCount, 0u);
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

    simulationReadBufferIndex_ = 0;
    simulationWriteBufferIndex_ = 1;
    parcelizeAllUnparcelledRciTiles(tileBuffers_[simulationReadBufferIndex_]);

    ++lotsRevision_;
    ++zoningLotsRevision_;
    ++commuteRevision_;
    commutesDirty_ = true;
    forcedCommuteLotIds_.clear();
    commuteRebalanceCursor_ = 0u;
    runCommuteAssignment(tileBuffers_[simulationReadBufferIndex_]);
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

bool SimulationRuntime::selectRciConstructorLotAssetForDiagnostics(
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
    int& capacity) const {
    lotAssetId.clear();
    rotationSteps = 0;
    capacity = 0;

    const LotAsset* lotAsset = findRciConstructorLotAsset(
        zoningType,
        rciTypeId,
        width,
        height,
        demandBudget,
        maxDensityPerTile,
        frontDirection,
        variationSeed,
        rotationSteps,
        capacity);
    if (lotAsset == 0) {
        return false;
    }

    lotAssetId = lotAsset->id;
    return true;
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
    snapshot.renderMeshBindings = &renderMeshBindings_;
    snapshot.groundRoadChunkRevisions = &tileBuffers_[publishedBufferIndex_].publishedGroundRoadChunkRevisions;
    snapshot.elevatedRoadChunkRevisions = &tileBuffers_[publishedBufferIndex_].publishedElevatedRoadChunkRevisions;
    snapshot.tileOverlayChunkRevisions = &tileBuffers_[publishedBufferIndex_].publishedTileOverlayChunkRevisions;
    snapshot.width = mapWidth_;
    snapshot.height = mapHeight_;
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

bool SimulationRuntime::fillRciDesirabilityOverlayChunkValues(const std::string& rciTypeId, const PublishedWorldSnapshot& snapshot, const ChunkRect& chunkRect, std::vector<RendererScalarPayload>& textureValues) const {
    const RciType* rciType = rciTools_.findRciType(rciTypeId);
    if (rciType == 0 || snapshot.tiles == 0) {
        return false;
    }

    const std::size_t chunkTileCount = static_cast<std::size_t>(chunkRect.width) * static_cast<std::size_t>(chunkRect.height);
    if (textureValues.size() != chunkTileCount) {
        textureValues.resize(chunkTileCount, 0u);
    }

    std::size_t writeIndex = 0;
    int tileY = chunkRect.startY;
    for (; tileY < chunkRect.startY + chunkRect.height; ++tileY) {
        int tileX = chunkRect.startX;
        for (; tileX < chunkRect.startX + chunkRect.width; ++tileX) {
            const std::size_t sourceIndex = static_cast<std::size_t>(tileY) * static_cast<std::size_t>(snapshot.width) + static_cast<std::size_t>(tileX);
            const int desirability = rciDesirabilityForTile(rciType->id(), (*snapshot.tiles)[sourceIndex]);
            textureValues[writeIndex++] = RendererPackCappedStatToScalarPayload(desirability, kRciDesirabilityDisplayCap);
        }
    }

    return true;
}

// Returns the fixed map width for this milestone.
int SimulationRuntime::mapWidth() const {
    return mapWidth_;
}

// Returns the fixed map height for this milestone.
int SimulationRuntime::mapHeight() const {
    return mapHeight_;
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
    if (!LoadGameAssets(RuntimeDataDirectory(), cityParameterRegistry_, loadedAssets, errorMessage)) {
        LogError("SimulationRuntime::loadAssets", errorMessage);
        throw std::runtime_error(errorMessage);
    }

    moduleAssets_ = loadedAssets.modules;
    lotAssets_ = loadedAssets.lots;
    renderMeshBindings_ = loadedAssets.renderMeshBindings;
    if (!loadedAssets.invalidLotReports.empty()) {
        LogWarning(
            "City Builder Asset Warning",
            JoinTruncatedReports(loadedAssets.invalidLotReports, "lot assets"));
    }
    rciGrowthRules_ = loadedAssets.rciGrowthRules;
    rciDesirabilityRules_ = loadedAssets.rciDesirabilityRules;
    initialCityDemands_ = loadedAssets.initialDemands;
    if (initialCityDemands_.size() < cityParameterRegistry_.count()) {
        initialCityDemands_.resize(cityParameterRegistry_.count(), 0.0f);
    }
    rciConstructorAttemptsPerTick_ = std::max(1, loadedAssets.rciConstructorAttemptsPerTick);
    rciConstructorOverbuildMultiplier_ = std::max(0.0f, loadedAssets.rciConstructorOverbuildMultiplier);
    rciConstructorMergeCapacityDiscount_ = std::max(0.0f, std::min(0.95f, loadedAssets.rciConstructorMergeCapacityDiscount));
    rciConstructorRedevelopmentCapacityIncrease_ = std::max(0.0f, std::min(4.0f, loadedAssets.rciConstructorRedevelopmentCapacityIncrease));
    rciBaselineLandValue_ = std::max(0, std::min(loadedAssets.rciBaselineLandValue, kLandValueDisplayCap));
    const std::string rciToolsPath = RuntimeDataPath("RCI\\rci_tools.xml");
    if (!rciTools_.loadFromXmlFile(rciToolsPath)) {
        throw std::runtime_error("SimulationRuntime::loadAssets: failed to load " + rciToolsPath);
    }
    std::size_t rciTypeIndex = 0;
    for (; rciTypeIndex < rciTools_.rciTypes().size(); ++rciTypeIndex) {
        const RciType& rciType = rciTools_.rciTypes()[rciTypeIndex];
        if (rciDemandParameterId(rciType.id()) < 0) {
            throw std::runtime_error("RCI type '" + rciType.id() + "' references unknown demand parameter '" + rciType.demandParameterId() + "'.");
        }
        if (findRciDesirabilityRule(rciType.id()) == 0) {
            throw std::runtime_error("RCI type '" + rciType.id() + "' is missing an rciDesirability rule.");
        }
    }
    std::size_t lotTypeIndex = 0;
    for (; lotTypeIndex < lotAssets_.size(); ++lotTypeIndex) {
        const LotAsset& lotAsset = lotAssets_[lotTypeIndex];
        if (lotAsset.zoningType != TileZoningNone && rciTools_.findRciType(lotAsset.rciTypeId) == 0) {
            throw std::runtime_error("RCI lot '" + lotAsset.id + "' references unknown RCI type '" + lotAsset.rciTypeId + "'.");
        }
    }
    transportNetwork_.setCongestionCurve(loadedAssets.congestionCurve);
    RoadTemplateDefinition::setLaneCapacityConfig(loadedAssets.roadLaneCapacities);

    moduleAssetIndexById_.clear();
    std::size_t moduleIndex = 0;
    for (; moduleIndex < moduleAssets_.size(); ++moduleIndex) {
        moduleAssetIndexById_[moduleAssets_[moduleIndex].id] = moduleIndex;
    }
    for (moduleIndex = 0; moduleIndex < moduleAssets_.size(); ++moduleIndex) {
        LotModule& module = moduleAssets_[moduleIndex];
        std::size_t propIndex = 0;
        for (; propIndex < module.props.size(); ++propIndex) {
            LotModulePropDefinition& prop = module.props[propIndex];
            prop.module = findModuleAssetById(prop.moduleId);
            prop.alternativeModules.clear();
            std::size_t alternativeIndex = 0;
            for (; alternativeIndex < prop.alternatives.size(); ++alternativeIndex) {
                if (IsNoneModuleAlternativeId(prop.alternatives[alternativeIndex].moduleId)) {
                    prop.alternativeModules.push_back(0);
                    continue;
                }

                prop.alternativeModules.push_back(findModuleAssetById(prop.alternatives[alternativeIndex].moduleId));
            }
        }
    }

    lotAssetIndexById_.clear();
    std::size_t lotIndex = 0;
    for (; lotIndex < lotAssets_.size(); ++lotIndex) {
        lotAssetIndexById_[lotAssets_[lotIndex].id] = lotIndex;
    }
}

// Seeds the three tile buffers and the initial published snapshot.
void SimulationRuntime::initializeWorld() {
    std::vector<Tile>& bootstrapBuffer = tileBuffers_[simulationReadBufferIndex_].tiles;
    int tileY = 0;
    for (; tileY < mapHeight_; ++tileY) {
        int tileX = 0;
        for (; tileX < mapWidth_; ++tileX) {
            Tile& tile = bootstrapBuffer[tileIndex(tileX, tileY)];
            tile.landValue = rciBaselineLandValue_;
            tile.airPollution = 0;
            tile.parkEffect = 0;
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
        tileBuffers_[bufferIndex].publishedLotOccupancy.assign(static_cast<std::size_t>(mapWidth_) * static_cast<std::size_t>(mapHeight_), kInvalidLotId);
        tileBuffers_[bufferIndex].publishedRoads.assign(static_cast<std::size_t>(mapWidth_) * static_cast<std::size_t>(mapHeight_) * TransportNetwork::layerCount(), ResolvedRoadCell());
        tileBuffers_[bufferIndex].publishedGroundRoadRenderState.assign(static_cast<std::size_t>(mapWidth_) * static_cast<std::size_t>(mapHeight_) * kGroundRoadRenderChannelsPerTile, 0);
        tileBuffers_[bufferIndex].publishedTileOverlayState.assign(static_cast<std::size_t>(mapWidth_) * static_cast<std::size_t>(mapHeight_), 0u);
        tileBuffers_[bufferIndex].publishedGroundRoadChunkRevisions.assign(chunkLayout_.size(), 1);
        tileBuffers_[bufferIndex].publishedElevatedRoadChunkRevisions.assign(chunkLayout_.size(), 1);
        tileBuffers_[bufferIndex].publishedTileOverlayChunkRevisions.assign(chunkLayout_.size(), 1);
        tileBuffers_[bufferIndex].lotRenderRevision = 0;
        tileBuffers_[bufferIndex].zoningLotRenderRevision = 0;
        tileBuffers_[bufferIndex].roadRenderRevision = 0;
        tileBuffers_[bufferIndex].overlayRenderRevision = 0;
        tileBuffers_[bufferIndex].commuteRenderRevision = 0;
    }

    lotOccupancy_.assign(static_cast<std::size_t>(mapWidth_) * static_cast<std::size_t>(mapHeight_), kInvalidLotId);
    lots_.clear();
    zoningLots_.clear();
    rciConstructorSourceCursors_.clear();
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

bool SimulationRuntime::waitForTickPermission(GameSpeed& activeGameSpeed, std::chrono::steady_clock::time_point& nextPlayTick, bool& commandOnlyFrame) {
    commandOnlyFrame = false;
    for (;;) {
        if (!keepRunning_.load()) {
            return false;
        }

        activeGameSpeed = gameSpeed_.load();
        if (activeGameSpeed == GameSpeed::Paused) {
            updatesPerSecond_.store(0);
            std::unique_lock<std::mutex> speedLock(speedMutex_);
            speedCv_.wait(speedLock, [this]() {
                return !keepRunning_.load() ||
                    gameSpeed_.load() != GameSpeed::Paused ||
                    hasPendingCommands();
            });
            nextPlayTick = std::chrono::steady_clock::now();
            if (keepRunning_.load() && gameSpeed_.load() == GameSpeed::Paused && hasPendingCommands()) {
                commandOnlyFrame = true;
                return true;
            }
            continue;
        }

        if (activeGameSpeed == GameSpeed::Play) {
            const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            if (now < nextPlayTick) {
                std::unique_lock<std::mutex> speedLock(speedMutex_);
                speedCv_.wait_until(speedLock, nextPlayTick, [this]() {
                    return !keepRunning_.load() ||
                        gameSpeed_.load() != GameSpeed::Play ||
                        hasPendingCommands();
                });
                if (keepRunning_.load() && gameSpeed_.load() == GameSpeed::Play && hasPendingCommands()) {
                    commandOnlyFrame = true;
                    return true;
                }
                continue;
            }
        } else {
            nextPlayTick = std::chrono::steady_clock::now();
        }

        return true;
    }
}

bool SimulationRuntime::hasPendingCommands() const {
    std::lock_guard<std::mutex> commandLock(commandMutex_);
    return !pendingCommands_.empty();
}

std::unique_lock<std::mutex> SimulationRuntime::acquireLiveStateForPreviewValidation() const {
    {
        std::lock_guard<std::mutex> previewValidationLock(livePreviewValidationMutex_);
        ++livePreviewValidationWaiters_;
    }
    livePreviewValidationCv_.notify_all();

    std::unique_lock<std::mutex> liveStateLock(liveStateMutex_);

    {
        std::lock_guard<std::mutex> previewValidationLock(livePreviewValidationMutex_);
        --livePreviewValidationWaiters_;
    }
    livePreviewValidationCv_.notify_all();
    return liveStateLock;
}

void SimulationRuntime::waitForPreviewValidationPriority() const {
    if (livePreviewValidationWaiters_.load() <= 0) {
        return;
    }

    std::unique_lock<std::mutex> previewValidationLock(livePreviewValidationMutex_);
    livePreviewValidationCv_.wait(previewValidationLock, [this]() {
        return !keepRunning_.load() ||
            livePreviewValidationWaiters_.load() <= 0;
    });
}

void SimulationRuntime::publishPausedCommandFrame() {
    CrashScope crashScope("SimulationRuntime::publishPausedCommandFrame");
    waitForPreviewValidationPriority();
    std::lock_guard<std::mutex> liveStateLock(liveStateMutex_);
    copyChunkRevisionsForWriteBuffer();
    TileBuffer& writeBuffer = tileBuffers_[simulationWriteBufferIndex_];
    writeBuffer.tiles = tileBuffers_[simulationReadBufferIndex_].tiles;

    const std::chrono::steady_clock::time_point commandStart = std::chrono::steady_clock::now();
    applyQueuedCommands(writeBuffer);
    commandPassMicros_.store(DurationMicros(commandStart, std::chrono::steady_clock::now()));

    std::chrono::steady_clock::time_point publishStart = std::chrono::steady_clock::now();
    publishCompletedBuffer();
    const long long commandPublishMicros = DurationMicros(publishStart, std::chrono::steady_clock::now());

    copyChunkRevisionsForWriteBuffer();
    TileBuffer& commuteWriteBuffer = tileBuffers_[simulationWriteBufferIndex_];
    commuteWriteBuffer.tiles = tileBuffers_[simulationReadBufferIndex_].tiles;

    const std::chrono::steady_clock::time_point lotEffectsStart = std::chrono::steady_clock::now();
    runCommuteAssignment(commuteWriteBuffer);
    lotEffectsMicros_.store(DurationMicros(lotEffectsStart, std::chrono::steady_clock::now()));

    publishStart = std::chrono::steady_clock::now();
    publishCompletedBuffer();
    publishMicros_.store(commandPublishMicros + DurationMicros(publishStart, std::chrono::steady_clock::now()));
}

// Runs the ordered simulation passes and publishes completed write buffers.
void SimulationRuntime::simulationLoop() {
    CrashScope crashScope("SimulationRuntime::simulationLoop");
    int updatesThisSecond = 0;
    std::chrono::steady_clock::time_point secondWindowStart = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point nextPlayTick = secondWindowStart;

    while (keepRunning_.load()) {
        GameSpeed activeGameSpeed = GameSpeed::Paused;
        bool commandOnlyFrame = false;
        if (!waitForTickPermission(activeGameSpeed, nextPlayTick, commandOnlyFrame)) {
            break;
        }
        if (commandOnlyFrame) {
            publishPausedCommandFrame();
            continue;
        }

        waitForPreviewValidationPriority();
        {
            std::lock_guard<std::mutex> liveStateLock(liveStateMutex_);
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
            runCommuteAssignment(tileBuffers_[simulationWriteBufferIndex_]);
            runRciConstructor(tileBuffers_[simulationWriteBufferIndex_]);
            lotEffectsMicros_.store(DurationMicros(lotEffectsStart, std::chrono::steady_clock::now()));

            const std::chrono::steady_clock::time_point localStart = std::chrono::steady_clock::now();
            runLocalTilePass(tileBuffers_[simulationWriteBufferIndex_].tiles);
            localPassMicros_.store(DurationMicros(localStart, std::chrono::steady_clock::now()));

            ++simulationTick_;

            const std::chrono::steady_clock::time_point publishStart = std::chrono::steady_clock::now();
            publishCompletedBuffer();
            publishMicros_.store(DurationMicros(publishStart, std::chrono::steady_clock::now()));
        }

        ++updatesThisSecond;
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - secondWindowStart).count() >= 1) {
            updatesPerSecond_.store(updatesThisSecond);
            updatesThisSecond = 0;
            secondWindowStart = now;
        }

        if (activeGameSpeed == GameSpeed::Play) {
            nextPlayTick += std::chrono::seconds(1);
            const std::chrono::steady_clock::time_point tickFinished = std::chrono::steady_clock::now();
            if (nextPlayTick < tickFinished) {
                nextPlayTick = tickFinished + std::chrono::seconds(1);
            }
        }

        if (activeGameSpeed == GameSpeed::Fast) {
            std::unique_lock<std::mutex> renderLock(renderMutex_);
            const std::uint64_t generationToWaitFor = publishedGeneration_;
            renderCv_.wait(renderLock, [this, generationToWaitFor]() {
                return !keepRunning_.load() ||
                    gameSpeed_.load() != GameSpeed::Fast ||
                    lastRenderedGeneration_.load() >= generationToWaitFor;
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
    CrashScope crashScope("SimulationRuntime::workerMain");
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
            int parkEffectDelta = 0;
            int landValueDelta = 0;

            if (tileX < mapWidth_ - 1) {
                const Tile& neighborTile = readTiles[tileIndex(tileX + 1, tileY)];
                airPollutionDelta += (neighborTile.airPollution - sourceTile.airPollution) / kAirPollutionSpreadRate;
                parkEffectDelta += (neighborTile.parkEffect - sourceTile.parkEffect) / kParkEffectSpreadRate;
                landValueDelta += (neighborTile.landValue - sourceTile.landValue) / kLandValueSpreadRate;
            }

            if (tileX > 0) {
                const Tile& neighborTile = readTiles[tileIndex(tileX - 1, tileY)];
                airPollutionDelta += (neighborTile.airPollution - sourceTile.airPollution) / kAirPollutionSpreadRate;
                parkEffectDelta += (neighborTile.parkEffect - sourceTile.parkEffect) / kParkEffectSpreadRate;
                landValueDelta += (neighborTile.landValue - sourceTile.landValue) / kLandValueSpreadRate;
            }

            if (tileY < mapHeight_ - 1) {
                const Tile& neighborTile = readTiles[tileIndex(tileX, tileY + 1)];
                airPollutionDelta += (neighborTile.airPollution - sourceTile.airPollution) / kAirPollutionSpreadRate;
                parkEffectDelta += (neighborTile.parkEffect - sourceTile.parkEffect) / kParkEffectSpreadRate;
                landValueDelta += (neighborTile.landValue - sourceTile.landValue) / kLandValueSpreadRate;
            }

            if (tileY > 0) {
                const Tile& neighborTile = readTiles[tileIndex(tileX, tileY - 1)];
                airPollutionDelta += (neighborTile.airPollution - sourceTile.airPollution) / kAirPollutionSpreadRate;
                parkEffectDelta += (neighborTile.parkEffect - sourceTile.parkEffect) / kParkEffectSpreadRate;
                landValueDelta += (neighborTile.landValue - sourceTile.landValue) / kLandValueSpreadRate;
            }

            Tile nextTile = sourceTile;
            nextTile.airPollution = sourceTile.airPollution + airPollutionDelta;
            nextTile.parkEffect = sourceTile.parkEffect + parkEffectDelta;
            nextTile.landValue = sourceTile.landValue + landValueDelta;
            writeTiles[currentIndex] = nextTile;
        }
    }
}

// Applies per-tile decay and clamps land value to the authored display range.
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
            tile.landValue = std::max(kLandValueDisplayMinimum, std::min(tile.landValue, kLandValueDisplayCap));
            tile.airPollution = ApplyEnvironmentalDecay(
                tile.airPollution,
                kAirPollutionDecayRate);
            tile.parkEffect = ApplyEnvironmentalDecay(
                tile.parkEffect,
                kParkEffectDecayRate);
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

            case PlayerCommandType::PlaceRoadStroke: {
                std::vector<int> dirtyRoadTopologyTiles;
                if (transportNetwork_.placeRoadStroke(playerCommand.roadStroke, lotOccupancy_, kInvalidLotId, &dirtyRoadTopologyTiles)) {
                    clearZoningForRoadStroke(playerCommand.roadStroke, writeBuffer);
                    queueCommuteRecalculationForRoadTopologyChange(dirtyRoadTopologyTiles);
                }
                break;
            }

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

            case PlayerCommandType::ApplyRciPlan:
                tryApplyRciPlan(playerCommand.rciPlan, writeBuffer);
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
        if (!lot.advanceConstructionTick(mapWidth_)) {
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

void SimulationRuntime::rebuildCityParameters(const TileBuffer& writeBuffer) {
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
        const Lot& lot = lots_[lotIndex];
        const LotAsset* lotAsset = findLotAssetById(lot.assetId());
        const std::uint16_t lotZoningType = lotAsset == 0 ? TileZoningNone : zoningTypeForLotInBuffer(lot, writeBuffer, lotAsset->zoningType);
        const bool scalesByDesirability = lotAsset != 0 && IsRciZoningType(lotZoningType);
        const int desirability = scalesByDesirability ? rciDesirabilityForCandidate(lot, *lotAsset, writeBuffer) : kRciDesirabilityDisplayCap;
        const std::vector<CityParameterContribution>& contributions = lot.parameterContributions();
        std::size_t contributionIndex = 0;
        for (; contributionIndex < contributions.size(); ++contributionIndex) {
            const CityParameterContribution& contribution = contributions[contributionIndex];
            if (contribution.parameterId >= 0 && contribution.parameterId < static_cast<int>(parameterCount)) {
                const int amount = scalesByDesirability
                    ? RciActualCapacityFromDesirability(contribution.amount, desirability)
                    : contribution.amount;
                deltaBuffer[contribution.parameterId] += static_cast<float>(amount);
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

void SimulationRuntime::queueCommuteRecalculationForRoadTopologyChange(const std::vector<int>& dirtyTileIndices) {
    if (dirtyTileIndices.empty()) {
        return;
    }

    std::vector<int> sortedTileIndices = dirtyTileIndices;
    std::sort(sortedTileIndices.begin(), sortedTileIndices.end());
    sortedTileIndices.erase(std::unique(sortedTileIndices.begin(), sortedTileIndices.end()), sortedTileIndices.end());

    std::size_t lotIndex = 0;
    for (; lotIndex < lots_.size(); ++lotIndex) {
        Lot& lot = lots_[lotIndex];
        bool shouldRecalculate = lotAccessMayTouchTiles(lot, sortedTileIndices);
        if (!shouldRecalculate) {
            const std::vector<CommuteRouteRecord>& routes = lot.commuteRoutes();
            std::size_t routeIndex = 0;
            for (; routeIndex < routes.size(); ++routeIndex) {
                if (commuteRouteTouchesTiles(routes[routeIndex], sortedTileIndices)) {
                    shouldRecalculate = true;
                    break;
                }
            }
        }

        if (!shouldRecalculate) {
            continue;
        }

        queueCommuteRecalculationForLot(lot.id());
        queueCommuteSourcesForDestination(lot.id());
    }
}

void SimulationRuntime::removeCommuteLoadsForLot(const Lot& lot) {
    const std::vector<CommuteRouteRecord>& routes = lot.commuteRoutes();
    if (routes.empty()) {
        return;
    }

    transportNetwork_.beginTrafficAssignmentFromOldLoad(CommuteTimeOfDay::Morning);
    transportNetwork_.beginTrafficAssignmentFromOldLoad(CommuteTimeOfDay::Evening);
    std::size_t routeIndex = 0;
    for (; routeIndex < routes.size(); ++routeIndex) {
        transportNetwork_.applyTrafficPathLoad(CommuteTimeOfDay::Morning, routes[routeIndex].morningPathResult, routes[routeIndex].transportLoad, false);
        transportNetwork_.applyTrafficPathLoad(CommuteTimeOfDay::Evening, routes[routeIndex].eveningPathResult, routes[routeIndex].transportLoad, false);
    }
    transportNetwork_.commitTrafficAssignment(CommuteTimeOfDay::Morning);
    transportNetwork_.commitTrafficAssignment(CommuteTimeOfDay::Evening);
    ++commuteRevision_;
}

void SimulationRuntime::runCommuteAssignment(const TileBuffer& writeBuffer) {
    rebuildCityParameters(writeBuffer);

    enum class CommuteCostClass {
        Invalid = 0,
        Short,
        Medium,
        Long
    };

    struct CommuteSource {
        std::size_t lotIndex;
        int lotId;
        int demand;
        std::vector<std::uint32_t> accessNodes;

        CommuteSource()
            : lotIndex(0u),
              lotId(-1),
              demand(0),
              accessNodes() {
        }
    };

    struct JobDestination {
        std::size_t lotIndex;
        int lotId;
        int remainingCapacity;
        std::vector<std::uint32_t> accessNodes;

        JobDestination()
            : lotIndex(0u),
              lotId(-1),
              remainingCapacity(0),
              accessNodes() {
        }
    };

    const CommuteTimeOfDay activeCommuteTime = (simulationTick_ % SimulationTime::ticksPerDay()) == 0u
        ? CommuteTimeOfDay::Morning
        : CommuteTimeOfDay::Evening;
    const TransportCostMap& costMap = transportNetwork_.costMap();
    std::vector<CommuteSource> sources;
    std::vector<JobDestination> destinations;
    std::unordered_map<int, std::size_t> lotIndexById;
    std::unordered_map<int, std::size_t> destinationIndexByLotId;
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

        const int residentDemand = lotActualParameterAmount(lot, lotAsset, cityParameterRegistry_.residentsLowWealthId(), writeBuffer);
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

        const int lowWealthJobCapacity = lotActualDerivedParameterAmount(lot, lotAsset, cityParameterRegistry_.jobsLowWealthId(), writeBuffer);
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
            destinationIndexByLotId[destination.lotId] = destinations.size();
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
        transportNetwork_.beginTrafficAssignmentFromZero(CommuteTimeOfDay::Morning);
        transportNetwork_.beginTrafficAssignmentFromZero(CommuteTimeOfDay::Evening);
    } else {
        transportNetwork_.beginTrafficAssignmentFromOldLoad(CommuteTimeOfDay::Morning);
        transportNetwork_.beginTrafficAssignmentFromOldLoad(CommuteTimeOfDay::Evening);
    }

    std::unordered_map<int, std::vector<CommuteRouteRecord> > selectedExistingRoutesByLotId;
    if (!commutesDirty_) {
        std::size_t selectedIndex = 0;
        for (; selectedIndex < selectedLotIndices.size(); ++selectedIndex) {
            Lot& sourceLot = lots_[selectedLotIndices[selectedIndex]];
            const std::vector<CommuteRouteRecord>& existingRoutes = sourceLot.commuteRoutes();
            if (!existingRoutes.empty()) {
                selectedExistingRoutesByLotId[sourceLot.id()] = existingRoutes;
            }

            std::size_t routeIndex = 0;
            for (; routeIndex < existingRoutes.size(); ++routeIndex) {
                transportNetwork_.applyTrafficPathLoad(CommuteTimeOfDay::Morning, existingRoutes[routeIndex].morningPathResult, existingRoutes[routeIndex].transportLoad, false);
                transportNetwork_.applyTrafficPathLoad(CommuteTimeOfDay::Evening, existingRoutes[routeIndex].eveningPathResult, existingRoutes[routeIndex].transportLoad, false);
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

    const auto classifyPath = [](const TransportPathResult& pathResult) {
        if (!pathResult.success) {
            return CommuteCostClass::Invalid;
        }

        if (pathResult.totalCost > kMaximumCommuteCost) {
            return CommuteCostClass::Long;
        }

        if (pathResult.totalCost >= kLongCommuteComplaintCost) {
            return CommuteCostClass::Medium;
        }

        return CommuteCostClass::Short;
    };

    const auto routeHasComplaint = [](const TransportPathResult& morningPathResult, const TransportPathResult& eveningPathResult) {
        return morningPathResult.totalCost >= kLongCommuteComplaintCost ||
            eveningPathResult.totalCost >= kLongCommuteComplaintCost;
    };

    const auto applyRouteLoads = [this](const CommuteRouteRecord& route, bool addLoad) {
        transportNetwork_.applyTrafficPathLoad(CommuteTimeOfDay::Morning, route.morningPathResult, route.transportLoad, addLoad);
        transportNetwork_.applyTrafficPathLoad(CommuteTimeOfDay::Evening, route.eveningPathResult, route.transportLoad, addLoad);
    };

    const auto buildPathRequest = [](CommuteTimeOfDay commuteTimeOfDay, const std::vector<std::uint32_t>& startNodeIds, const std::vector<std::uint32_t>& goalNodeIds, int lotId, int demand, std::uint64_t salt) {
        TransportPathRequest request;
        request.startNodeIds = startNodeIds;
        request.goalNodeIds = goalNodeIds;
        request.routeSeed = static_cast<std::uint32_t>((lotId * 73856093) ^ (demand * 19349663) ^ static_cast<int>(salt));
        request.demand = static_cast<std::uint16_t>(std::min(demand, static_cast<int>(kTransportMaxLoad)));
        request.maximumCost = kMaximumCommuteCost;
        request.useCongestion = true;
        request.commuteTimeOfDay = commuteTimeOfDay;
        return request;
    };

    const auto findPathForDirection = [this, &costMap, &buildPathRequest](CommuteTimeOfDay commuteTimeOfDay, const std::vector<std::uint32_t>& startNodeIds, const std::vector<std::uint32_t>& goalNodeIds, int lotId, int demand, std::uint64_t salt, TransportPathResult& pathResult) {
        TransportPathRequest request = buildPathRequest(commuteTimeOfDay, startNodeIds, goalNodeIds, lotId, demand, salt);
        return costMap.findPath(request, commutePathScratch_, pathResult);
    };

    const auto rerouteDirectionToSameDestination = [
        this,
        &findPathForDirection,
        &classifyPath,
        &routeHasComplaint
    ](CommuteTimeOfDay commuteTimeOfDay, const CommuteSource& source, const JobDestination& destination, CommuteRouteRecord& route, std::uint64_t salt) {
        TransportPathResult pathResult;
        const bool found = commuteTimeOfDay == CommuteTimeOfDay::Morning
            ? findPathForDirection(commuteTimeOfDay, source.accessNodes, destination.accessNodes, source.lotId, route.demand, salt, pathResult)
            : findPathForDirection(commuteTimeOfDay, destination.accessNodes, source.accessNodes, source.lotId, route.demand, salt, pathResult);
        if (!found || classifyPath(pathResult) == CommuteCostClass::Invalid || classifyPath(pathResult) == CommuteCostClass::Long) {
            return false;
        }

        if (commuteTimeOfDay == CommuteTimeOfDay::Morning) {
            route.morningPathResult = pathResult;
            route.morningSegments = this->buildCommuteRouteSegments(pathResult, route.transportLoad, CommuteTimeOfDay::Morning);
            route.morningMediumRetry = classifyPath(pathResult) == CommuteCostClass::Medium;
        } else {
            route.eveningPathResult = pathResult;
            route.eveningSegments = this->buildCommuteRouteSegments(pathResult, route.transportLoad, CommuteTimeOfDay::Evening);
            route.eveningMediumRetry = classifyPath(pathResult) == CommuteCostClass::Medium;
        }

        route.longCommute = routeHasComplaint(route.morningPathResult, route.eveningPathResult);
        return true;
    };

    const auto tryMaintainExistingRoute = [
        &activeCommuteTime,
        &classifyPath,
        &rerouteDirectionToSameDestination,
        &routeHasComplaint
    ](const CommuteSource& source, const JobDestination& destination, const CommuteRouteRecord& existingRoute, CommuteRouteRecord& maintainedRoute, std::uint64_t salt) {
        maintainedRoute = existingRoute;

        CommuteCostClass morningClass = classifyPath(maintainedRoute.morningPathResult);
        CommuteCostClass eveningClass = classifyPath(maintainedRoute.eveningPathResult);
        if (morningClass == CommuteCostClass::Invalid || morningClass == CommuteCostClass::Long ||
            eveningClass == CommuteCostClass::Invalid || eveningClass == CommuteCostClass::Long) {
            return false;
        }
        if ((morningClass == CommuteCostClass::Medium && maintainedRoute.morningMediumRetry) ||
            (eveningClass == CommuteCostClass::Medium && maintainedRoute.eveningMediumRetry)) {
            return false;
        }

        if (morningClass == CommuteCostClass::Short) {
            maintainedRoute.morningMediumRetry = false;
        } else if (morningClass == CommuteCostClass::Medium && activeCommuteTime == CommuteTimeOfDay::Morning) {
            if (maintainedRoute.morningMediumRetry) {
                return false;
            }
            if (!rerouteDirectionToSameDestination(CommuteTimeOfDay::Morning, source, destination, maintainedRoute, salt ^ 0xA501u)) {
                return false;
            }
            morningClass = classifyPath(maintainedRoute.morningPathResult);
            if (morningClass == CommuteCostClass::Medium && maintainedRoute.morningMediumRetry) {
                return false;
            }
        }

        if (eveningClass == CommuteCostClass::Short) {
            maintainedRoute.eveningMediumRetry = false;
        } else if (eveningClass == CommuteCostClass::Medium && activeCommuteTime == CommuteTimeOfDay::Evening) {
            if (maintainedRoute.eveningMediumRetry) {
                return false;
            }
            if (!rerouteDirectionToSameDestination(CommuteTimeOfDay::Evening, source, destination, maintainedRoute, salt ^ 0xE701u)) {
                return false;
            }
            eveningClass = classifyPath(maintainedRoute.eveningPathResult);
            if (eveningClass == CommuteCostClass::Medium && maintainedRoute.eveningMediumRetry) {
                return false;
            }
        }

        if (morningClass == CommuteCostClass::Invalid || morningClass == CommuteCostClass::Long ||
            eveningClass == CommuteCostClass::Invalid || eveningClass == CommuteCostClass::Long) {
            return false;
        }

        maintainedRoute.longCommute = routeHasComplaint(maintainedRoute.morningPathResult, maintainedRoute.eveningPathResult);
        return true;
    };

    std::size_t selectedIndex = 0;
    for (; selectedIndex < selectedSourceIndices.size(); ++selectedIndex) {
        CommuteSource& source = sources[selectedSourceIndices[selectedIndex]];
        if (source.accessNodes.empty()) {
            continue;
        }

        int remainingDemand = source.demand;
        const std::unordered_map<int, std::vector<CommuteRouteRecord> >::const_iterator existingRouteIterator = selectedExistingRoutesByLotId.find(source.lotId);
        if (existingRouteIterator != selectedExistingRoutesByLotId.end()) {
            const std::vector<CommuteRouteRecord>& existingRoutes = existingRouteIterator->second;
            std::size_t routeIndex = 0;
            for (; routeIndex < existingRoutes.size() && remainingDemand > 0; ++routeIndex) {
                const CommuteRouteRecord& existingRoute = existingRoutes[routeIndex];
                if (existingRoute.demand > remainingDemand) {
                    continue;
                }

                const std::unordered_map<int, std::size_t>::const_iterator destinationIterator = destinationIndexByLotId.find(existingRoute.destinationLotId);
                if (destinationIterator == destinationIndexByLotId.end()) {
                    continue;
                }

                JobDestination& destination = destinations[destinationIterator->second];
                if (destination.remainingCapacity < existingRoute.demand ||
                    destination.accessNodes.empty() ||
                    destination.lotIndex == source.lotIndex) {
                    continue;
                }

                CommuteRouteRecord maintainedRoute;
                if (!commuteRouteIsStillValid(existingRoute) ||
                    !tryMaintainExistingRoute(source, destination, existingRoute, maintainedRoute, simulationTick_ + commuteRevision_ + routeIndex)) {
                    continue;
                }

                lots_[source.lotIndex].addCommuteRoute(
                    maintainedRoute.destinationLotId,
                    maintainedRoute.demand,
                    maintainedRoute.transportLoad,
                    maintainedRoute.longCommute,
                    maintainedRoute.morningMediumRetry,
                    maintainedRoute.eveningMediumRetry,
                    maintainedRoute.morningPathResult,
                    maintainedRoute.eveningPathResult,
                    maintainedRoute.morningSegments,
                    maintainedRoute.eveningSegments);
                applyRouteLoads(maintainedRoute, true);
                destination.remainingCapacity -= maintainedRoute.demand;
                remainingDemand -= maintainedRoute.demand;
                lots_[destination.lotIndex].addLowWealthJobsFilled(maintainedRoute.demand);
                commuteStateChanged = true;
            }
        }

        while (remainingDemand > 0) {
            std::vector<bool> excludedDestinations(destinations.size(), false);
            bool acceptedRoute = false;

            for (;;) {
                TransportPathRequest morningRequest;
                morningRequest.startNodeIds = source.accessNodes;
                morningRequest.routeSeed = static_cast<std::uint32_t>((source.lotId * 73856093) ^ (remainingDemand * 19349663) ^ static_cast<int>(commuteRevision_ + simulationTick_ + 1u));
                morningRequest.demand = static_cast<std::uint16_t>(std::min(remainingDemand, static_cast<int>(kTransportMaxLoad)));
                morningRequest.maximumCost = kMaximumCommuteCost;
                morningRequest.useCongestion = true;
                morningRequest.commuteTimeOfDay = CommuteTimeOfDay::Morning;

                std::vector<int> goalDestinationIndices;
                destinationIndex = 0;
                for (; destinationIndex < destinations.size(); ++destinationIndex) {
                    const JobDestination& destination = destinations[destinationIndex];
                    if (excludedDestinations[destinationIndex] ||
                        destination.remainingCapacity <= 0 ||
                        destination.accessNodes.empty() ||
                        destination.lotIndex == source.lotIndex) {
                        continue;
                    }

                    std::size_t nodeIndex = 0;
                    for (; nodeIndex < destination.accessNodes.size(); ++nodeIndex) {
                        morningRequest.goalNodeIds.push_back(destination.accessNodes[nodeIndex]);
                        goalDestinationIndices.push_back(static_cast<int>(destinationIndex));
                    }
                }

                if (morningRequest.goalNodeIds.empty()) {
                    break;
                }

                TransportPathResult morningPathResult;
                if (!costMap.findPath(morningRequest, commutePathScratch_, morningPathResult)) {
                    break;
                }

                int reachedDestinationIndex = -1;
                std::size_t goalIndex = 0;
                for (; goalIndex < morningRequest.goalNodeIds.size(); ++goalIndex) {
                    if (morningRequest.goalNodeIds[goalIndex] == morningPathResult.reachedNodeId) {
                        reachedDestinationIndex = goalDestinationIndices[goalIndex];
                        break;
                    }
                }

                if (reachedDestinationIndex < 0 || reachedDestinationIndex >= static_cast<int>(destinations.size())) {
                    break;
                }

                JobDestination& reachedDestination = destinations[static_cast<std::size_t>(reachedDestinationIndex)];
                TransportPathResult eveningPathResult;
                if (!findPathForDirection(
                        CommuteTimeOfDay::Evening,
                        reachedDestination.accessNodes,
                        source.accessNodes,
                        source.lotId,
                        remainingDemand,
                        simulationTick_ + commuteRevision_ + 0xE001u,
                        eveningPathResult)) {
                    excludedDestinations[static_cast<std::size_t>(reachedDestinationIndex)] = true;
                    continue;
                }

                if (classifyPath(morningPathResult) == CommuteCostClass::Invalid ||
                    classifyPath(morningPathResult) == CommuteCostClass::Long ||
                    classifyPath(eveningPathResult) == CommuteCostClass::Invalid ||
                    classifyPath(eveningPathResult) == CommuteCostClass::Long) {
                    excludedDestinations[static_cast<std::size_t>(reachedDestinationIndex)] = true;
                    continue;
                }

                const int acceptedDemand = std::min(remainingDemand, reachedDestination.remainingCapacity);
                if (acceptedDemand <= 0) {
                    break;
                }

                reachedDestination.remainingCapacity -= acceptedDemand;
                remainingDemand -= acceptedDemand;

                const std::uint16_t clampedDemand = static_cast<std::uint16_t>(std::min(acceptedDemand, static_cast<int>(kTransportMaxLoad)));
                const bool longCommute = routeHasComplaint(morningPathResult, eveningPathResult);
                lots_[source.lotIndex].addCommuteRoute(
                    reachedDestination.lotId,
                    acceptedDemand,
                    clampedDemand,
                    longCommute,
                    false,
                    false,
                    morningPathResult,
                    eveningPathResult,
                    buildCommuteRouteSegments(morningPathResult, clampedDemand, CommuteTimeOfDay::Morning),
                    buildCommuteRouteSegments(eveningPathResult, clampedDemand, CommuteTimeOfDay::Evening));
                lots_[reachedDestination.lotIndex].addLowWealthJobsFilled(acceptedDemand);
                transportNetwork_.applyTrafficPathLoad(CommuteTimeOfDay::Morning, morningPathResult, clampedDemand, true);
                transportNetwork_.applyTrafficPathLoad(CommuteTimeOfDay::Evening, eveningPathResult, clampedDemand, true);
                commuteStateChanged = true;
                acceptedRoute = true;
                break;
            }

            if (!acceptedRoute) {
                break;
            }
        }
    }

    transportNetwork_.commitTrafficAssignment(CommuteTimeOfDay::Morning);
    transportNetwork_.commitTrafficAssignment(CommuteTimeOfDay::Evening);

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
    {
        std::lock_guard<std::mutex> commandLock(commandMutex_);
        pendingCommands_.push_back(playerCommand);
    }
    speedCv_.notify_all();
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
            publishedLotInfo.zoningType = zoningTypeForLotInBuffer(lots_[lotIndex], completedBuffer, lotAsset->zoningType);
        }
        publishedLotInfo.minimumTileX = lots_[lotIndex].minimumTileX();
        publishedLotInfo.minimumTileY = lots_[lotIndex].minimumTileY();
        publishedLotInfo.footprintWidth = lots_[lotIndex].footprintWidth();
        publishedLotInfo.footprintHeight = lots_[lotIndex].footprintHeight();
        publishedLotInfo.isEmpty = lots_[lotIndex].modules().empty();
        publishedLotInfo.moduleSummary = lots_[lotIndex].moduleSummary();
        publishedLotInfo.parameterSummary = lots_[lotIndex].parameterSummary(cityParameterRegistry_);
        publishedLotInfo.commuteDemand = lots_[lotIndex].commuteDemand();
        publishedLotInfo.commuteSatisfied = lots_[lotIndex].commuteSatisfied();
        publishedLotInfo.residentsLowWealthCurrent = lots_[lotIndex].commuteSatisfied();
        publishedLotInfo.residentsLowWealthTotal = lots_[lotIndex].lowWealthResidentsTotal();
        publishedLotInfo.jobsLowWealthCurrent = lots_[lotIndex].lowWealthJobsFilled();
        publishedLotInfo.jobsLowWealthTotal = lots_[lotIndex].lowWealthJobsTotal();
        if (lotAsset != 0 && !publishedLotInfo.isEmpty && IsRciZoningType(publishedLotInfo.zoningType)) {
            const std::string lotRciTypeId = lotAsset->rciTypeId.empty()
                ? DefaultRciTypeIdForZoningType(publishedLotInfo.zoningType)
                : lotAsset->rciTypeId;
            const int capacityParameterId = rciDemandParameterId(lotRciTypeId);
            publishedLotInfo.rciCapacityMaximum = rciCapacityForLot(lots_[lotIndex], lotRciTypeId);
            publishedLotInfo.rciCapacityCurrent = lotActualParameterAmount(lots_[lotIndex], lotAsset, capacityParameterId, completedBuffer);
        }
        publishedLotInfo.complaintSummary = lots_[lotIndex].complaintSummary();
        completedBuffer.publishedLotInfos.push_back(publishedLotInfo);
        const std::size_t firstRenderInstanceIndex = completedBuffer.publishedLots.size();
        lots_[lotIndex].buildRenderInstances(completedBuffer.publishedLots);
        for (std::size_t renderInstanceIndex = firstRenderInstanceIndex; renderInstanceIndex < completedBuffer.publishedLots.size(); ++renderInstanceIndex) {
            completedBuffer.publishedLots[renderInstanceIndex].zoningType = publishedLotInfo.zoningType;
        }
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
                AccumulatePublishedCommuteCategory(sourceInfo, routes[routeIndex]);
                sourceInfo.commuteRouteSegments.insert(sourceInfo.commuteRouteSegments.end(), routes[routeIndex].morningSegments.begin(), routes[routeIndex].morningSegments.end());
                completedBuffer.publishedCommuteRouteSegments.insert(completedBuffer.publishedCommuteRouteSegments.end(), routes[routeIndex].morningSegments.begin(), routes[routeIndex].morningSegments.end());
                completedBuffer.publishedCommuteRouteSegments.insert(completedBuffer.publishedCommuteRouteSegments.end(), routes[routeIndex].eveningSegments.begin(), routes[routeIndex].eveningSegments.end());
            }
        }

        std::size_t routeIndex = 0;
        for (; routeIndex < routes.size(); ++routeIndex) {
            const std::unordered_map<int, std::size_t>::const_iterator destinationIterator = publishedLotIndexById.find(routes[routeIndex].destinationLotId);
            if (destinationIterator == publishedLotIndexById.end()) {
                continue;
            }

            PublishedLotInfo& destinationInfo = completedBuffer.publishedLotInfos[destinationIterator->second];
            AccumulatePublishedCommuteCategory(destinationInfo, routes[routeIndex]);
            destinationInfo.commuteRouteSegments.insert(destinationInfo.commuteRouteSegments.end(), routes[routeIndex].morningSegments.begin(), routes[routeIndex].morningSegments.end());
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
        const int tileY = tileLinearIndex / mapWidth_;
        const int tileX = tileLinearIndex - (tileY * mapWidth_);
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

    const int chunkColumnCount = mapWidth_ / chunkConfig_.chunkWidth;
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

const LotModule* SimulationRuntime::resolveModulePlacement(const LotAsset& lotAsset, const LotModulePlacementDefinition& placement, std::size_t placementIndex, int anchorTileX, int anchorTileY, int rotationSteps, int lotId) const {
    if (placement.alternatives.empty()) {
        return findModuleAssetById(placement.moduleId);
    }

    int totalWeight = 0;
    std::size_t alternativeIndex = 0;
    for (; alternativeIndex < placement.alternatives.size(); ++alternativeIndex) {
        totalWeight += std::max(0, placement.alternatives[alternativeIndex].weight);
    }
    if (totalWeight <= 0) {
        return findModuleAssetById(placement.moduleId);
    }

    std::uint32_t hash = 2166136261u;
    const auto mixByte = [&hash](std::uint8_t value) {
        hash ^= static_cast<std::uint32_t>(value);
        hash *= 16777619u;
    };
    const auto mixInt = [&mixByte](int value) {
        std::uint32_t bits = static_cast<std::uint32_t>(value);
        mixByte(static_cast<std::uint8_t>(bits & 0xffu));
        mixByte(static_cast<std::uint8_t>((bits >> 8) & 0xffu));
        mixByte(static_cast<std::uint8_t>((bits >> 16) & 0xffu));
        mixByte(static_cast<std::uint8_t>((bits >> 24) & 0xffu));
    };
    std::size_t characterIndex = 0;
    for (; characterIndex < lotAsset.id.size(); ++characterIndex) {
        mixByte(static_cast<std::uint8_t>(lotAsset.id[characterIndex]));
    }
    mixInt(static_cast<int>(placementIndex));
    mixInt(anchorTileX);
    mixInt(anchorTileY);
    mixInt(rotationSteps);
    mixInt(lotId);

    int selectedWeight = static_cast<int>(hash % static_cast<std::uint32_t>(totalWeight));
    for (alternativeIndex = 0; alternativeIndex < placement.alternatives.size(); ++alternativeIndex) {
        selectedWeight -= std::max(0, placement.alternatives[alternativeIndex].weight);
        if (selectedWeight < 0) {
            if (IsNoneModuleAlternativeId(placement.alternatives[alternativeIndex].moduleId)) {
                return 0;
            }
            const LotModule* moduleAsset = findModuleAssetById(placement.alternatives[alternativeIndex].moduleId);
            return moduleAsset == 0 ? findModuleAssetById(placement.moduleId) : moduleAsset;
        }
    }

    return findModuleAssetById(placement.moduleId);
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

std::uint16_t SimulationRuntime::zoningTypeForLotInBuffer(const Lot& lot, const TileBuffer& buffer, std::uint16_t fallbackZoningType) const {
    const std::vector<int>& occupiedTiles = lot.occupiedTileIndices();
    std::size_t occupiedIndex = 0;
    for (; occupiedIndex < occupiedTiles.size(); ++occupiedIndex) {
        const int tileLinearIndex = occupiedTiles[occupiedIndex];
        if (tileLinearIndex < 0 || tileLinearIndex >= static_cast<int>(buffer.tiles.size())) {
            continue;
        }

        const std::uint16_t zoningType = buffer.tiles[static_cast<std::size_t>(tileLinearIndex)].zoningType;
        if (zoningType != TileZoningNone) {
            return zoningType;
        }
    }

    return fallbackZoningType;
}

// Builds a lot from an archetype so committed placement and ghost previews share geometry.
bool SimulationRuntime::buildLotCandidate(const LotAsset& lotAsset, int clickedTileX, int clickedTileY, int rotationSteps, int lotId, Lot& candidateLot) const {
    const int normalizedRotation = NormalizeRotationSteps(rotationSteps);
    int parcelWidth = 0;
    int parcelHeight = 0;
    RotatedRectangleDimensions(Int2(0, 0), lotAsset.footprintWidth, lotAsset.footprintHeight, normalizedRotation, parcelWidth, parcelHeight);
    return buildLotCandidateForParcel(lotAsset, clickedTileX, clickedTileY, normalizedRotation, lotId, parcelWidth, parcelHeight, candidateLot);
}

bool SimulationRuntime::buildLotCandidateForParcel(const LotAsset& lotAsset, int clickedTileX, int clickedTileY, int rotationSteps, int lotId, int parcelWidth, int parcelHeight, Lot& candidateLot) const {
    const int normalizedRotation = NormalizeRotationSteps(rotationSteps);
    const int unrotatedWidth = (normalizedRotation % 2) == 0 ? parcelWidth : parcelHeight;
    const int unrotatedHeight = (normalizedRotation % 2) == 0 ? parcelHeight : parcelWidth;
    if (unrotatedWidth <= 0 || unrotatedHeight <= 0) {
        return false;
    }

    candidateLot = Lot(lotId, lotAsset.id, clickedTileX, clickedTileY, normalizedRotation);

    Int2 footprintMinimum;
    Int2 footprintMaximum;
    const int footprintWidth = lotAsset.autoLayout.empty() ? lotAsset.footprintWidth : unrotatedWidth;
    const int footprintHeight = lotAsset.autoLayout.empty() ? lotAsset.footprintHeight : unrotatedHeight;
    RotatedRectangleBounds(lotAsset.footprintOrigin, footprintWidth, footprintHeight, normalizedRotation, footprintMinimum, footprintMaximum);
    candidateLot.setExplicitFootprint(
        footprintMinimum,
        footprintMaximum.x - footprintMinimum.x + 1,
        footprintMaximum.y - footprintMinimum.y + 1,
        mapWidth_);

    if (!lotAsset.autoLayout.empty()) {
        LotAutoPrimaryGeometry primary;
        return populateAutoLayoutLot(lotAsset, unrotatedWidth, unrotatedHeight, clickedTileX, clickedTileY, normalizedRotation, lotId, candidateLot, primary);
    }

    std::size_t placementIndex = 0;
    for (; placementIndex < lotAsset.initialModules.size(); ++placementIndex) {
        const LotModulePlacementDefinition& placement = lotAsset.initialModules[placementIndex];
        const LotModule* moduleAsset = resolveModulePlacement(lotAsset, placement, placementIndex, clickedTileX, clickedTileY, normalizedRotation, lotId);
        if (moduleAsset == 0) {
            return false;
        }

        const LotModulePlacementGeometry unrotatedGeometry = ResolveLotModulePlacementGeometry(placement, *moduleAsset);
        const LotModulePlacementGeometry rotatedGeometry = RotateLotModulePlacementGeometry(unrotatedGeometry, normalizedRotation);
        candidateLot.addModule(
            *moduleAsset,
            rotatedGeometry.localOrigin,
            mapWidth_,
            rotatedGeometry.footprintWidth,
            rotatedGeometry.footprintHeight,
            rotatedGeometry.renderOffsetX,
            rotatedGeometry.renderOffsetY,
            rotatedGeometry.renderWidth,
            rotatedGeometry.renderHeight,
            placement.affectsSimulation,
            placement.claimsFootprint);
    }

    return true;
}

bool SimulationRuntime::populateAutoLayoutLot(const LotAsset& lotAsset, int lotWidth, int lotHeight, int anchorTileX, int anchorTileY, int rotationSteps, int lotId, Lot& candidateLot, LotAutoPrimaryGeometry& primary) const {
    std::vector<std::uint8_t> claimedTiles(static_cast<std::size_t>(lotWidth * lotHeight), 0u);
    std::size_t placementSeedIndex = 0u;

    struct PlacedAutoLayoutModule {
        const LotModule* module;
        LotModulePlacementDefinition placement;
        LotModulePlacementGeometry geometry;
    };
    std::vector<PlacedAutoLayoutModule> placedModules;

    const auto geometryFitsLot = [lotWidth, lotHeight](const LotModulePlacementGeometry& geometry) -> bool {
        if (geometry.localOrigin.x < 0 ||
            geometry.localOrigin.y < 0 ||
            geometry.localOrigin.x + geometry.footprintWidth > lotWidth ||
            geometry.localOrigin.y + geometry.footprintHeight > lotHeight) {
            return false;
        }

        return true;
    };

    const auto claimGeometry = [&claimedTiles, &geometryFitsLot, lotWidth](const LotModulePlacementGeometry& geometry) -> bool {
        if (!geometryFitsLot(geometry)) {
            return false;
        }

        int tileY = 0;
        for (; tileY < geometry.footprintHeight; ++tileY) {
            int tileX = 0;
            for (; tileX < geometry.footprintWidth; ++tileX) {
                const int localX = geometry.localOrigin.x + tileX;
                const int localY = geometry.localOrigin.y + tileY;
                const std::size_t tileIndex = static_cast<std::size_t>(localY * lotWidth + localX);
                if (tileIndex >= claimedTiles.size() || claimedTiles[tileIndex] != 0u) {
                    return false;
                }
            }
        }

        for (tileY = 0; tileY < geometry.footprintHeight; ++tileY) {
            int tileX = 0;
            for (; tileX < geometry.footprintWidth; ++tileX) {
                const int localX = geometry.localOrigin.x + tileX;
                const int localY = geometry.localOrigin.y + tileY;
                claimedTiles[static_cast<std::size_t>(localY * lotWidth + localX)] = 1u;
            }
        }

        return true;
    };

    const auto addPlacement = [&](const LotModulePlacementDefinition& placement, const LotModule& module) {
        const LotModulePlacementGeometry unrotatedGeometry = ResolveLotModulePlacementGeometry(placement, module);
        const LotModulePlacementGeometry rotatedGeometry = RotateLotModulePlacementGeometry(unrotatedGeometry, rotationSteps);
        candidateLot.addModule(
            module,
            rotatedGeometry.localOrigin,
            mapWidth_,
            rotatedGeometry.footprintWidth,
            rotatedGeometry.footprintHeight,
            rotatedGeometry.renderOffsetX,
            rotatedGeometry.renderOffsetY,
            rotatedGeometry.renderWidth,
            rotatedGeometry.renderHeight,
            placement.affectsSimulation,
            placement.claimsFootprint);

        PlacedAutoLayoutModule placedModule;
        placedModule.module = &module;
        placedModule.placement = placement;
        placedModule.geometry = unrotatedGeometry;
        placedModules.push_back(placedModule);
    };

    std::size_t ruleIndex = 0;
    for (; ruleIndex < lotAsset.autoLayout.moduleRules.size(); ++ruleIndex) {
        const LotAutoModuleRule& rule = lotAsset.autoLayout.moduleRules[ruleIndex];
        if (!LotAutoSizeConditionMatches(rule.condition, lotWidth, lotHeight) ||
            (rule.isPrimary && primary.hasPrimary) ||
            !LotAutoPrimaryRequirementMatches(rule.primaryModuleIds, primary)) {
            continue;
        }

        const LotModule* primaryModule = findModuleAssetById(rule.moduleId);
        if (primaryModule == 0) {
            return false;
        }

        LotModulePlacementDefinition placement = BuildLotAutoModulePlacementDefinition(rule, *primaryModule, lotWidth, lotHeight, primary);
        const LotModule* module = resolveModulePlacement(lotAsset, placement, placementSeedIndex++, anchorTileX, anchorTileY, rotationSteps, lotId);
        if (module == 0) {
            if (rule.required) {
                return false;
            }
            continue;
        }

        placement = BuildLotAutoModulePlacementDefinition(rule, *module, lotWidth, lotHeight, primary);
        const LotModulePlacementGeometry geometry = ResolveLotModulePlacementGeometry(placement, *module);
        const bool placed = LotModulePlacementGeometryVisualFits(geometry) &&
            (rule.claimsFootprint ? claimGeometry(geometry) : geometryFitsLot(geometry));
        if (!placed) {
            if (rule.required) {
                return false;
            }
            continue;
        }

        addPlacement(placement, *module);
        if (rule.isPrimary) {
            primary.hasPrimary = true;
            primary.moduleId = module->id;
            primary.localOrigin = geometry.localOrigin;
            primary.footprintWidth = geometry.footprintWidth;
            primary.footprintHeight = geometry.footprintHeight;
        }
    }

    if (!primary.hasPrimary) {
        return false;
    }

    std::size_t lineIndex = 0;
    for (; lineIndex < lotAsset.autoLayout.lineRules.size(); ++lineIndex) {
        const LotAutoLineRule& rule = lotAsset.autoLayout.lineRules[lineIndex];
        if (!LotAutoSizeConditionMatches(rule.condition, lotWidth, lotHeight) ||
            !LotAutoPrimaryRequirementMatches(rule.primaryModuleIds, primary)) {
            continue;
        }

        const LotModule* module = findModuleAssetById(rule.moduleId);
        if (module == 0 || module->width != 1 || module->height != 1) {
            return false;
        }

        const int x = ResolveLotAutoCoordinateReference(rule.xReference, lotWidth, 1, primary.localOrigin.x, primary.footprintWidth, primary.hasPrimary) + rule.xOffset;
        const int startY = ResolveLotAutoCoordinateReference(rule.startYReference, lotHeight, 1, primary.localOrigin.y, primary.footprintHeight, primary.hasPrimary) + rule.startYOffset;
        const int endY = ResolveLotAutoCoordinateReference(rule.endYReference, lotHeight, 1, primary.localOrigin.y, primary.footprintHeight, primary.hasPrimary) + rule.endYOffset;
        if (endY < startY) {
            if (rule.required) {
                return false;
            }
            continue;
        }

        bool placedAnyLineTile = false;
        int y = startY;
        for (; y <= endY; ++y) {
            LotModulePlacementDefinition placement;
            placement.moduleId = rule.moduleId;
            placement.localOrigin = Int2(x, y);
            placement.footprintWidth = 1;
            placement.footprintHeight = 1;
            placement.renderOffsetX = rule.renderOffsetX;
            placement.renderOffsetY = rule.renderOffsetY;
            placement.renderWidth = rule.renderWidth;
            placement.renderHeight = rule.renderHeight;
            placement.hasRenderOffsetX = rule.hasRenderOffsetX;
            placement.hasRenderOffsetY = rule.hasRenderOffsetY;
            placement.hasRenderWidth = rule.hasRenderWidth;
            placement.hasRenderHeight = rule.hasRenderHeight;
            placement.renderAlignX = rule.renderAlignX;
            placement.renderAlignY = rule.renderAlignY;
            placement.affectsSimulation = rule.affectsSimulation;
            placement.claimsFootprint = rule.claimsFootprint;
            placement.alternatives = rule.alternatives;
            const LotModule* selectedModule = resolveModulePlacement(lotAsset, placement, placementSeedIndex++, anchorTileX, anchorTileY, rotationSteps, lotId);
            if (selectedModule == 0) {
                if (rule.required) {
                    return false;
                }
                continue;
            }

            const LotModulePlacementGeometry geometry = ResolveLotModulePlacementGeometry(placement, *selectedModule);
            const bool placed = LotModulePlacementGeometryVisualFits(geometry) &&
                (rule.claimsFootprint ? claimGeometry(geometry) : geometryFitsLot(geometry));
            if (!placed) {
                if (rule.required) {
                    return false;
                }
                continue;
            }

            addPlacement(placement, *selectedModule);
            placedAnyLineTile = true;
        }

        if (rule.required && !placedAnyLineTile) {
            return false;
        }
    }

    std::size_t fillIndex = 0;
    for (; fillIndex < lotAsset.autoLayout.fillRules.size(); ++fillIndex) {
        const LotAutoFillRule& rule = lotAsset.autoLayout.fillRules[fillIndex];
        if (!LotAutoSizeConditionMatches(rule.condition, lotWidth, lotHeight) ||
            !LotAutoPrimaryRequirementMatches(rule.primaryModuleIds, primary)) {
            continue;
        }

        std::size_t tileIndex = 0;
        for (; tileIndex < claimedTiles.size(); ++tileIndex) {
            if (claimedTiles[tileIndex] != 0u) {
                continue;
            }

            const LotModule* module = findModuleAssetById(rule.moduleId);
            if (module == 0 || module->width != 1 || module->height != 1) {
                return false;
            }

            const int localX = static_cast<int>(tileIndex % static_cast<std::size_t>(lotWidth));
            const int localY = static_cast<int>(tileIndex / static_cast<std::size_t>(lotWidth));
            LotModulePlacementDefinition placement;
            placement.moduleId = rule.moduleId;
            placement.localOrigin = Int2(localX, localY);
            placement.affectsSimulation = rule.affectsSimulation;
            placement.claimsFootprint = rule.claimsFootprint;
            placement.alternatives = rule.alternatives;
            const LotModule* selectedModule = resolveModulePlacement(lotAsset, placement, placementSeedIndex++, anchorTileX, anchorTileY, rotationSteps, lotId);
            if (selectedModule == 0) {
                if (rule.claimsFootprint) {
                    return false;
                }
                continue;
            }

            const LotModulePlacementGeometry geometry = ResolveLotModulePlacementGeometry(placement, *selectedModule);
            const bool placed = rule.claimsFootprint ? claimGeometry(geometry) : geometryFitsLot(geometry);
            if (!placed) {
                return false;
            }

            addPlacement(placement, *selectedModule);
        }
    }

    struct BoundaryEdgeDefinition {
        int side;
        bool matches;
        float offsetX;
        float offsetY;
        float width;
        float height;
    };

    std::set<std::string> emittedBoundaryEdgeKeys;
    std::size_t edgeIndex = 0;
    for (; edgeIndex < lotAsset.autoLayout.edgeRules.size(); ++edgeIndex) {
        const LotAutoEdgeRule& rule = lotAsset.autoLayout.edgeRules[edgeIndex];
        if (!LotAutoSizeConditionMatches(rule.condition, lotWidth, lotHeight) ||
            !LotAutoPrimaryRequirementMatches(rule.primaryModuleIds, primary)) {
            continue;
        }

        const LotModule* edgeModule = findModuleAssetById(rule.moduleId);
        if (edgeModule == 0 || edgeModule->width != 1 || edgeModule->height != 1) {
            return false;
        }

        std::size_t placedIndex = 0;
        for (; placedIndex < placedModules.size(); ++placedIndex) {
            const PlacedAutoLayoutModule& placedModule = placedModules[placedIndex];
            if (placedModule.module == 0 || placedModule.module->id != rule.sourceModuleId) {
                continue;
            }

            int tileY = 0;
            for (; tileY < placedModule.geometry.footprintHeight; ++tileY) {
                int tileX = 0;
                for (; tileX < placedModule.geometry.footprintWidth; ++tileX) {
                    const int localX = placedModule.geometry.localOrigin.x + tileX;
                    const int localY = placedModule.geometry.localOrigin.y + tileY;
                    const BoundaryEdgeDefinition edgeDefinitions[] = {
                        {0, localY == 0, 0.0f, 0.0f, 1.0f, 0.08f},
                        {1, localY == lotHeight - 1, 0.0f, 0.92f, 1.0f, 0.08f},
                        {2, localX == 0, 0.0f, 0.0f, 0.08f, 1.0f},
                        {3, localX == lotWidth - 1, 0.92f, 0.0f, 0.08f, 1.0f}
                    };

                    std::size_t edgeDefinitionIndex = 0;
                    for (; edgeDefinitionIndex < sizeof(edgeDefinitions) / sizeof(edgeDefinitions[0]); ++edgeDefinitionIndex) {
                        const BoundaryEdgeDefinition& edgeDefinition = edgeDefinitions[edgeDefinitionIndex];
                        if (!edgeDefinition.matches) {
                            continue;
                        }

                        std::ostringstream edgeKey;
                        edgeKey << edgeIndex << ":" << localX << ":" << localY << ":" << edgeDefinition.side;
                        if (emittedBoundaryEdgeKeys.find(edgeKey.str()) != emittedBoundaryEdgeKeys.end()) {
                            continue;
                        }
                        emittedBoundaryEdgeKeys.insert(edgeKey.str());

                        LotModulePlacementDefinition placement;
                        placement.moduleId = rule.moduleId;
                        placement.localOrigin = Int2(localX, localY);
                        placement.footprintWidth = 1;
                        placement.footprintHeight = 1;
                        placement.renderOffsetX = edgeDefinition.offsetX;
                        placement.renderOffsetY = edgeDefinition.offsetY;
                        placement.renderWidth = edgeDefinition.width;
                        placement.renderHeight = edgeDefinition.height;
                        placement.hasRenderOffsetX = true;
                        placement.hasRenderOffsetY = true;
                        placement.hasRenderWidth = true;
                        placement.hasRenderHeight = true;
                        placement.affectsSimulation = rule.affectsSimulation;
                        placement.claimsFootprint = rule.claimsFootprint;
                        addPlacement(placement, *edgeModule);
                    }
                }
            }
        }
    }

    std::size_t tileIndex = 0;
    for (; tileIndex < claimedTiles.size(); ++tileIndex) {
        if (claimedTiles[tileIndex] == 0u) {
            return false;
        }
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

    const RciRect footprintRect(
        candidateLot.minimumTileX(),
        candidateLot.minimumTileY(),
        candidateLot.minimumTileX() + candidateLot.footprintWidth() - 1,
        candidateLot.minimumTileY() + candidateLot.footprintHeight() - 1);
    const bool removedZoningLots = removeZoningLotsIntersectingRect(footprintRect);
    std::vector<int> clearedZoningTileIndices;
    const std::vector<int>& occupiedTileIndices = candidateLot.occupiedTileIndices();
    clearedZoningTileIndices.reserve(occupiedTileIndices.size());
    std::size_t occupiedIndex = 0;
    for (; occupiedIndex < occupiedTileIndices.size(); ++occupiedIndex) {
        const int tileLinearIndex = occupiedTileIndices[occupiedIndex];
        if (tileLinearIndex < 0 || tileLinearIndex >= static_cast<int>(writeBuffer.tiles.size())) {
            continue;
        }

        if (writeBuffer.tiles[tileLinearIndex].zoningType == TileZoningNone) {
            continue;
        }

        writeBuffer.tiles[tileLinearIndex].zoningType = TileZoningNone;
        clearedZoningTileIndices.push_back(tileLinearIndex);
    }

    lots_.push_back(candidateLot);
    setLotOccupancy(candidateLot.id(), occupiedTileIndices);
    markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, occupiedTileIndices);
    if (!clearedZoningTileIndices.empty()) {
        markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, clearedZoningTileIndices);
    }
    if (removedZoningLots || !clearedZoningTileIndices.empty()) {
        parcelizeAllUnparcelledRciTiles(writeBuffer);
    }
    ++nextLotId_;
    ++lotsRevision_;
    queueCommuteRecalculationForLot(candidateLot.id());
    return true;
}

void SimulationRuntime::runRciConstructor(TileBuffer& writeBuffer) {
    std::vector<std::uint16_t> zoningTypes;
    const std::vector<RciTool>& tools = rciTools_.tools();
    std::size_t toolIndex = 0;
    for (; toolIndex < tools.size(); ++toolIndex) {
        const std::uint16_t zoningType = tools[toolIndex].zoningType();
        if (std::find(zoningTypes.begin(), zoningTypes.end(), zoningType) == zoningTypes.end()) {
            zoningTypes.push_back(zoningType);
        }
    }

    std::size_t zoningIndex = 0;
    for (; zoningIndex < zoningTypes.size(); ++zoningIndex) {
        const std::uint16_t zoningType = zoningTypes[zoningIndex];
        bool triedKnownType = false;
        const std::vector<RciType>& rciTypes = rciTools_.rciTypes();
        std::size_t typeIndex = 0;
        for (; typeIndex < rciTypes.size(); ++typeIndex) {
            if (!rciTypes[typeIndex].allowsZoningType(zoningType)) {
                continue;
            }

            triedKnownType = true;
            tryConstructOneRciLot(zoningType, rciTypes[typeIndex].id(), writeBuffer);
        }

        if (!triedKnownType) {
            tryConstructOneRciLot(zoningType, DefaultRciTypeIdForZoningType(zoningType), writeBuffer);
        }
    }
}

bool SimulationRuntime::tryConstructOneRciLot(std::uint16_t zoningType, const std::string& rciTypeId, TileBuffer& writeBuffer) {
    if (zoningType == TileZoningNone || rciTypeId.empty() || findRciGrowthRule(zoningType) == 0) {
        return false;
    }

    const float demand = rciDemandForRciType(rciTypeId);
    if (demand <= 0.0f) {
        return false;
    }

    // Under-construction lots do not affect city parameters yet, but they still
    // reserve demand budget so the constructor does not keep filling the same demand.
    float remainingBudget = (demand * rciConstructorOverbuildMultiplier_) - rciPendingConstructionCapacity(rciTypeId);
    if (remainingBudget <= 0.0f) {
        return false;
    }

    bool constructedAny = false;
    int attempts = 0;
    RciConstructorSourceCursor& sourceCursor = rciConstructorSourceCursors_[rciConstructorCursorKey(zoningType, rciTypeId)];
    while (attempts < rciConstructorAttemptsPerTick_ && remainingBudget > 0.0f) {
        const std::vector<RciDevelopmentSource> sources = collectRciDevelopmentSources(zoningType, rciTypeId);
        if (sources.empty()) {
            sourceCursor = RciConstructorSourceCursor();
            break;
        }

        bool constructedThisPass = false;
        const std::size_t startSourceIndex = rciConstructorStartIndex(sourceCursor, sources);
        std::size_t checkedSourceCount = 0u;
        for (; checkedSourceCount < sources.size() && attempts < rciConstructorAttemptsPerTick_ && remainingBudget > 0.0f; ++checkedSourceCount) {
            const std::size_t sourceIndex = (startSourceIndex + checkedSourceCount) % sources.size();
            ++attempts;
            sourceCursor = rciConstructorCursorForSource(sources[sourceIndex]);
            int constructedCapacity = 0;
            if (!tryConstructRciDevelopmentFromSource(zoningType, rciTypeId, sourceIndex, sources, remainingBudget, writeBuffer, constructedCapacity)) {
                continue;
            }

            constructedAny = true;
            constructedThisPass = true;
            remainingBudget = std::max(0.0f, remainingBudget - constructedCapacity);
            break;
        }

        if (!constructedThisPass) {
            break;
        }
    }

    return constructedAny;
}

std::string SimulationRuntime::rciConstructorCursorKey(std::uint16_t zoningType, const std::string& rciTypeId) const {
    return std::to_string(static_cast<unsigned int>(zoningType)) + ":" + rciTypeId;
}

SimulationRuntime::RciConstructorSourceCursor SimulationRuntime::rciConstructorCursorForSource(const RciDevelopmentSource& source) const {
    RciConstructorSourceCursor cursor;
    cursor.hasValue = true;
    cursor.isBuilt = source.isBuilt;
    cursor.minTileX = source.rect.minTileX;
    cursor.minTileY = source.rect.minTileY;
    cursor.maxTileX = source.rect.maxTileX;
    cursor.maxTileY = source.rect.maxTileY;
    cursor.lotId = source.lotId;
    return cursor;
}

bool SimulationRuntime::rciConstructorCursorLess(const RciConstructorSourceCursor& left, const RciConstructorSourceCursor& right) const {
    if (!left.hasValue || !right.hasValue) {
        return !left.hasValue && right.hasValue;
    }
    if (left.isBuilt != right.isBuilt) {
        return !left.isBuilt && right.isBuilt;
    }
    if (left.minTileY != right.minTileY) {
        return left.minTileY < right.minTileY;
    }
    if (left.minTileX != right.minTileX) {
        return left.minTileX < right.minTileX;
    }
    if (left.maxTileY != right.maxTileY) {
        return left.maxTileY < right.maxTileY;
    }
    if (left.maxTileX != right.maxTileX) {
        return left.maxTileX < right.maxTileX;
    }
    return left.lotId < right.lotId;
}

std::size_t SimulationRuntime::rciConstructorStartIndex(const RciConstructorSourceCursor& cursor, const std::vector<RciDevelopmentSource>& sources) const {
    if (!cursor.hasValue || sources.empty()) {
        return 0u;
    }

    std::size_t sourceIndex = 0u;
    for (; sourceIndex < sources.size(); ++sourceIndex) {
        if (rciConstructorCursorLess(cursor, rciConstructorCursorForSource(sources[sourceIndex]))) {
            return sourceIndex;
        }
    }

    return 0u;
}

bool SimulationRuntime::tryConstructRciDevelopmentFromSource(std::uint16_t zoningType, const std::string& rciTypeId, std::size_t seedSourceIndex, const std::vector<RciDevelopmentSource>& sources, float demandBudget, TileBuffer& writeBuffer, int& constructedCapacity) {
    constructedCapacity = 0;
    if (seedSourceIndex >= sources.size() || demandBudget <= 0.0f) {
        return false;
    }

    const std::vector<std::size_t> blockSourceIndices = buildRciDevelopmentBlock(seedSourceIndex, sources);
    if (blockSourceIndices.empty()) {
        return false;
    }

    std::vector<int> xBoundaries;
    std::vector<int> yBoundaries;
    std::size_t blockIndex = 0;
    for (; blockIndex < blockSourceIndices.size(); ++blockIndex) {
        const RciRect& sourceRect = sources[blockSourceIndices[blockIndex]].rect;
        AddUniqueBoundary(xBoundaries, sourceRect.minTileX);
        AddUniqueBoundary(xBoundaries, sourceRect.maxTileX + 1);
        AddUniqueBoundary(yBoundaries, sourceRect.minTileY);
        AddUniqueBoundary(yBoundaries, sourceRect.maxTileY + 1);
    }
    std::sort(xBoundaries.begin(), xBoundaries.end());
    std::sort(yBoundaries.begin(), yBoundaries.end());

    RciConstructionCandidate bestCandidate;
    const float kCapacityEpsilon = 0.001f;
    std::size_t minXIndex = 0;
    for (; minXIndex < xBoundaries.size(); ++minXIndex) {
        std::size_t maxXIndex = minXIndex + 1u;
        for (; maxXIndex < xBoundaries.size(); ++maxXIndex) {
            const int candidateWidth = xBoundaries[maxXIndex] - xBoundaries[minXIndex];
            if (candidateWidth <= 0 || candidateWidth > kRciConstructorBlockLimit) {
                continue;
            }

            std::size_t minYIndex = 0;
            for (; minYIndex < yBoundaries.size(); ++minYIndex) {
                std::size_t maxYIndex = minYIndex + 1u;
                for (; maxYIndex < yBoundaries.size(); ++maxYIndex) {
                    const int candidateHeight = yBoundaries[maxYIndex] - yBoundaries[minYIndex];
                    if (candidateHeight <= 0 || candidateHeight > kRciConstructorBlockLimit) {
                        continue;
                    }

                    const RciRect candidateRect(
                        xBoundaries[minXIndex],
                        yBoundaries[minYIndex],
                        xBoundaries[maxXIndex] - 1,
                        yBoundaries[maxYIndex] - 1);

                    RciConstructionCandidate candidate;
                    if (!evaluateRciConstructionCandidate(zoningType, rciTypeId, candidateRect, seedSourceIndex, blockSourceIndices, sources, demandBudget, writeBuffer, candidate)) {
                        continue;
                    }

                    const int candidateArea = candidate.rect.width() * candidate.rect.height();
                    const int bestArea = bestCandidate.rect.width() * bestCandidate.rect.height();
                    if (!bestCandidate.isValid ||
                        candidate.selectionCapacity > bestCandidate.selectionCapacity + kCapacityEpsilon ||
                        (std::fabs(candidate.selectionCapacity - bestCandidate.selectionCapacity) <= kCapacityEpsilon &&
                            (candidate.netGrowth > bestCandidate.netGrowth + kCapacityEpsilon ||
                                (std::fabs(candidate.netGrowth - bestCandidate.netGrowth) <= kCapacityEpsilon &&
                                    (candidateArea < bestArea ||
                                        (candidateArea == bestArea &&
                                            (candidate.rect.minTileY < bestCandidate.rect.minTileY ||
                                                (candidate.rect.minTileY == bestCandidate.rect.minTileY && candidate.rect.minTileX < bestCandidate.rect.minTileX)))))))) {
                        bestCandidate = candidate;
                    }
                }
            }
        }
    }

    if (!bestCandidate.isValid || !commitRciConstructionCandidate(bestCandidate, writeBuffer)) {
        return false;
    }

    constructedCapacity = bestCandidate.netGrowth;
    return true;
}

std::vector<SimulationRuntime::RciDevelopmentSource> SimulationRuntime::collectRciDevelopmentSources(std::uint16_t zoningType, const std::string& rciTypeId) const {
    std::vector<RciDevelopmentSource> sources;
    std::size_t zoningLotIndex = 0;
    for (; zoningLotIndex < zoningLots_.size(); ++zoningLotIndex) {
        const RciLot& zoningLot = zoningLots_[zoningLotIndex];
        if (zoningLot.zoningType != zoningType || !zoningLot.rect.isValid() || zoningLot.availableAfterTick > simulationTick_) {
            continue;
        }

        RciDevelopmentSource source;
        source.rect = zoningLot.rect;
        source.rciTypeId = rciTypeId;
        source.isBuilt = false;
        source.sourceIndex = zoningLotIndex;
        source.lotId = kInvalidLotId;
        source.capacity = 0;
        source.frontDirection = zoningLot.frontDirection;
        sources.push_back(source);
    }

    const TileBuffer& readBuffer = tileBuffers_[simulationReadBufferIndex_];
    std::size_t lotIndex = 0;
    for (; lotIndex < lots_.size(); ++lotIndex) {
        const Lot& lot = lots_[lotIndex];
        if (lot.isUnderConstruction()) {
            continue;
        }

        const LotAsset* lotAsset = findLotAssetById(lot.assetId());
        const std::uint16_t lotZoningType = lotAsset == 0 ? TileZoningNone : zoningTypeForLotInBuffer(lot, readBuffer, lotAsset->zoningType);
        const std::string lotRciTypeId = lotAsset == 0
            ? std::string()
            : (lotAsset->rciTypeId.empty() ? DefaultRciTypeIdForZoningType(lotZoningType) : lotAsset->rciTypeId);
        if (lotAsset == 0 || lotZoningType != zoningType || lotRciTypeId != rciTypeId) {
            continue;
        }

        RciDevelopmentSource source;
        source.rect = RciRect(
            lot.minimumTileX(),
            lot.minimumTileY(),
            lot.minimumTileX() + lot.footprintWidth() - 1,
            lot.minimumTileY() + lot.footprintHeight() - 1);
        source.rciTypeId = lotRciTypeId;
        source.isBuilt = true;
        source.sourceIndex = lotIndex;
        source.lotId = lot.id();
        source.capacity = rciCapacityForLot(lot, lotRciTypeId);
        source.frontDirection = RotateRoadDirection(lotAsset->hasFrontDirection ? lotAsset->frontDirection : kRoadDirectionNorth, lot.rotationSteps());
        if (source.rect.isValid()) {
            sources.push_back(source);
        }
    }

    std::sort(sources.begin(), sources.end(), [](const RciDevelopmentSource& left, const RciDevelopmentSource& right) {
        if (left.isBuilt != right.isBuilt) {
            return !left.isBuilt;
        }
        if (left.rect.minTileY != right.rect.minTileY) {
            return left.rect.minTileY < right.rect.minTileY;
        }
        if (left.rect.minTileX != right.rect.minTileX) {
            return left.rect.minTileX < right.rect.minTileX;
        }
        return left.sourceIndex < right.sourceIndex;
    });

    return sources;
}

std::vector<std::size_t> SimulationRuntime::buildRciDevelopmentBlock(std::size_t seedSourceIndex, const std::vector<RciDevelopmentSource>& sources) const {
    std::vector<std::size_t> blockSourceIndices;
    if (seedSourceIndex >= sources.size()) {
        return blockSourceIndices;
    }

    blockSourceIndices.push_back(seedSourceIndex);
    RciRect blockBounds = sources[seedSourceIndex].rect;
    bool addedSource = true;
    while (addedSource) {
        addedSource = false;
        std::size_t sourceIndex = 0;
        for (; sourceIndex < sources.size(); ++sourceIndex) {
            if (std::find(blockSourceIndices.begin(), blockSourceIndices.end(), sourceIndex) != blockSourceIndices.end()) {
                continue;
            }

            bool touchesBlock = false;
            std::size_t blockIndex = 0;
            for (; blockIndex < blockSourceIndices.size(); ++blockIndex) {
                if (RciRectsAreEdgeAdjacent(sources[sourceIndex].rect, sources[blockSourceIndices[blockIndex]].rect)) {
                    touchesBlock = true;
                    break;
                }
            }

            if (!touchesBlock) {
                continue;
            }

            const RciRect expandedBounds = RciUnionRect(blockBounds, sources[sourceIndex].rect);
            if (expandedBounds.width() > kRciConstructorBlockLimit || expandedBounds.height() > kRciConstructorBlockLimit) {
                continue;
            }

            blockBounds = expandedBounds;
            blockSourceIndices.push_back(sourceIndex);
            addedSource = true;
        }
    }

    return blockSourceIndices;
}

bool SimulationRuntime::evaluateRciConstructionCandidate(std::uint16_t zoningType, const std::string& rciTypeId, const RciRect& candidateRect, std::size_t seedSourceIndex, const std::vector<std::size_t>& blockSourceIndices, const std::vector<RciDevelopmentSource>& sources, float demandBudget, const TileBuffer& writeBuffer, RciConstructionCandidate& candidate) const {
    candidate = RciConstructionCandidate();
    if (!candidateRect.isValid() ||
        candidateRect.width() > kRciConstructorBlockLimit ||
        candidateRect.height() > kRciConstructorBlockLimit ||
        seedSourceIndex >= sources.size() ||
        !RciRectContainsRect(candidateRect, sources[seedSourceIndex].rect)) {
        return false;
    }
    candidate.zoningType = zoningType;
    candidate.rciTypeId = rciTypeId;

    const std::uint8_t requiredFrontDirection = sources[seedSourceIndex].frontDirection;
    bool consumesSeed = false;
    std::size_t blockIndex = 0;
    for (; blockIndex < blockSourceIndices.size(); ++blockIndex) {
        const std::size_t sourceIndex = blockSourceIndices[blockIndex];
        if (sourceIndex >= sources.size()) {
            continue;
        }

        const RciDevelopmentSource& source = sources[sourceIndex];
        if (!source.rect.intersects(candidateRect)) {
            continue;
        }
        if (source.frontDirection != requiredFrontDirection) {
            return false;
        }
        if (!RciRectContainsRect(candidateRect, source.rect)) {
            return false;
        }

        candidate.sourceIndices.push_back(sourceIndex);
        candidate.consumedSources.push_back(source);
        consumesSeed = consumesSeed || sourceIndex == seedSourceIndex;
    }

    if (!consumesSeed || candidate.sourceIndices.empty()) {
        return false;
    }

    std::size_t sourceIndex = 0;
    for (; sourceIndex < sources.size(); ++sourceIndex) {
        if (std::find(candidate.sourceIndices.begin(), candidate.sourceIndices.end(), sourceIndex) != candidate.sourceIndices.end()) {
            continue;
        }
        if (sources[sourceIndex].rect.intersects(candidateRect)) {
            return false;
        }
    }

    int tileY = candidateRect.minTileY;
    for (; tileY <= candidateRect.maxTileY; ++tileY) {
        int tileX = candidateRect.minTileX;
        for (; tileX <= candidateRect.maxTileX; ++tileX) {
            bool coveredBySource = false;
            std::size_t consumedIndex = 0;
            for (; consumedIndex < candidate.sourceIndices.size(); ++consumedIndex) {
                if (RciRectContainsTile(sources[candidate.sourceIndices[consumedIndex]].rect, tileX, tileY)) {
                    coveredBySource = true;
                    break;
                }
            }
            if (!coveredBySource) {
                return false;
            }
        }
    }

    if (!rciCandidateTilesAreDevelopable(zoningType, candidateRect, candidate.sourceIndices, sources, writeBuffer)) {
        return false;
    }

    const float maxDensityPerTile = rciLocalMaxDensityPerTile(zoningType, candidateRect, writeBuffer);
    if (maxDensityPerTile <= 0.0f) {
        return false;
    }

    std::size_t consumedIndex = 0;
    for (; consumedIndex < candidate.sourceIndices.size(); ++consumedIndex) {
        const RciDevelopmentSource& source = sources[candidate.sourceIndices[consumedIndex]];
        if (source.isBuilt) {
            candidate.consumedBuiltCapacity += source.capacity;
            continue;
        }

        int standaloneRotationSteps = 0;
        int standaloneCapacity = 0;
        const LotAsset* standaloneAsset = findRciConstructorLotAsset(
            zoningType,
            rciTypeId,
            source.rect.width(),
            source.rect.height(),
            std::numeric_limits<float>::max(),
            rciLocalMaxDensityPerTile(zoningType, source.rect, writeBuffer),
            source.frontDirection,
            rciVariationSeedForRect(zoningType, source.rect),
            standaloneRotationSteps,
            standaloneCapacity);
        if (standaloneAsset != 0) {
            candidate.standaloneEmptyCapacity += standaloneCapacity;
        }
    }

    const float grossDemandBudget = demandBudget + candidate.consumedBuiltCapacity;
    candidate.lotAsset = findRciConstructorLotAsset(
        zoningType,
        rciTypeId,
        candidateRect.width(),
        candidateRect.height(),
        grossDemandBudget,
        maxDensityPerTile,
        sources[seedSourceIndex].frontDirection,
        rciVariationSeedForRect(zoningType, candidateRect),
        candidate.rotationSteps,
        candidate.capacity);
    if (candidate.lotAsset == 0) {
        return false;
    }

    const bool requiresCapacityImprovement = candidate.consumedBuiltCapacity > 0 || candidate.sourceIndices.size() > 1u;
    candidate.selectionCapacity = static_cast<float>(candidate.capacity);
    if (candidate.sourceIndices.size() > 1u) {
        candidate.selectionCapacity *= 1.0f - rciConstructorMergeCapacityDiscount_;
    }
    if (requiresCapacityImprovement &&
        candidate.selectionCapacity <= static_cast<float>(candidate.consumedBuiltCapacity + candidate.standaloneEmptyCapacity) + 0.001f) {
        return false;
    }
    if (candidate.consumedBuiltCapacity > 0) {
        const float requiredRedevelopmentCapacity = static_cast<float>(candidate.consumedBuiltCapacity) * (1.0f + rciConstructorRedevelopmentCapacityIncrease_);
        if (static_cast<float>(candidate.capacity) + 0.001f < requiredRedevelopmentCapacity) {
            return false;
        }
    }

    const int unrotatedWidth = (NormalizeRotationSteps(candidate.rotationSteps) % 2) == 0 ? candidateRect.width() : candidateRect.height();
    const int unrotatedHeight = (NormalizeRotationSteps(candidate.rotationSteps) % 2) == 0 ? candidateRect.height() : candidateRect.width();
    Int2 footprintMinimum;
    Int2 footprintMaximum;
    RotatedRectangleBounds(candidate.lotAsset->footprintOrigin, unrotatedWidth, unrotatedHeight, candidate.rotationSteps, footprintMinimum, footprintMaximum);

    const int anchorTileX = candidateRect.minTileX - footprintMinimum.x;
    const int anchorTileY = candidateRect.minTileY - footprintMinimum.y;
    if (!buildLotCandidateForParcel(*candidate.lotAsset, anchorTileX, anchorTileY, candidate.rotationSteps, nextLotId_, candidateRect.width(), candidateRect.height(), candidate.lot)) {
        return false;
    }

    if (candidate.lot.minimumTileX() != candidateRect.minTileX ||
        candidate.lot.minimumTileY() != candidateRect.minTileY ||
        candidate.lot.footprintWidth() != candidateRect.width() ||
        candidate.lot.footprintHeight() != candidateRect.height()) {
        return false;
    }

    const RciGrowthRule* growthRule = findRciGrowthRule(zoningType);
    if (growthRule == 0) {
        return false;
    }

    candidate.desirability = rciDesirabilityForCandidate(candidate.lot, *candidate.lotAsset, writeBuffer);
    if (candidate.desirability < growthRule->desirabilityThreshold) {
        return false;
    }

    candidate.rect = candidateRect;
    candidate.netGrowth = std::max(0, candidate.capacity - candidate.consumedBuiltCapacity);
    candidate.isValid = true;
    return true;
}

bool SimulationRuntime::commitRciConstructionCandidate(const RciConstructionCandidate& candidate, TileBuffer& writeBuffer) {
    if (!candidate.isValid || candidate.lotAsset == 0) {
        return false;
    }

    std::vector<std::size_t> builtLotIndices;
    std::vector<std::size_t> zoningLotIndices;
    std::size_t consumedIndex = 0;
    for (; consumedIndex < candidate.consumedSources.size(); ++consumedIndex) {
        const RciDevelopmentSource& source = candidate.consumedSources[consumedIndex];
        if (source.isBuilt) {
            builtLotIndices.push_back(source.sourceIndex);
        } else {
            zoningLotIndices.push_back(source.sourceIndex);
        }
    }

    std::sort(builtLotIndices.begin(), builtLotIndices.end());
    builtLotIndices.erase(std::unique(builtLotIndices.begin(), builtLotIndices.end()), builtLotIndices.end());
    std::sort(zoningLotIndices.begin(), zoningLotIndices.end());
    zoningLotIndices.erase(std::unique(zoningLotIndices.begin(), zoningLotIndices.end()), zoningLotIndices.end());

    std::vector<int> dirtyTiles;
    for (std::vector<std::size_t>::reverse_iterator builtIndex = builtLotIndices.rbegin(); builtIndex != builtLotIndices.rend(); ++builtIndex) {
        if (*builtIndex >= lots_.size()) {
            continue;
        }

        const int lotId = lots_[*builtIndex].id();
        const std::vector<int> occupiedTiles = lots_[*builtIndex].occupiedTileIndices();
        queueCommuteSourcesForDestination(lotId);
        removeCommuteLoadsForLot(lots_[*builtIndex]);
        clearLotOccupancy(occupiedTiles);
        dirtyTiles.insert(dirtyTiles.end(), occupiedTiles.begin(), occupiedTiles.end());

        std::size_t occupiedIndex = 0;
        for (; occupiedIndex < occupiedTiles.size(); ++occupiedIndex) {
            const int tileLinearIndex = occupiedTiles[occupiedIndex];
            if (tileLinearIndex < 0 || tileLinearIndex >= static_cast<int>(writeBuffer.tiles.size())) {
                continue;
            }
            writeBuffer.tiles[tileLinearIndex].zoningType = candidate.zoningType;
            writeBuffer.tiles[tileLinearIndex].isVacant = true;
        }

        lots_.erase(lots_.begin() + static_cast<std::ptrdiff_t>(*builtIndex));
    }

    for (std::vector<std::size_t>::reverse_iterator zoningIndex = zoningLotIndices.rbegin(); zoningIndex != zoningLotIndices.rend(); ++zoningIndex) {
        if (*zoningIndex < zoningLots_.size()) {
            zoningLots_.erase(zoningLots_.begin() + static_cast<std::ptrdiff_t>(*zoningIndex));
        }
    }

    Lot newLot = candidate.lot;
    newLot.startConstruction(candidate.lotAsset->constructionTicks, mapWidth_);
    lots_.push_back(newLot);
    setLotOccupancy(newLot.id(), newLot.occupiedTileIndices());
    dirtyTiles.insert(dirtyTiles.end(), newLot.occupiedTileIndices().begin(), newLot.occupiedTileIndices().end());
    markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, dirtyTiles);
    ++nextLotId_;
    ++lotsRevision_;
    if (!zoningLotIndices.empty()) {
        ++zoningLotsRevision_;
    }
    if (!newLot.isUnderConstruction()) {
        queueCommuteRecalculationForLot(newLot.id());
    }
    return true;
}

bool SimulationRuntime::rciCandidateTilesAreDevelopable(std::uint16_t zoningType, const RciRect& rect, const std::vector<std::size_t>& consumedSourceIndices, const std::vector<RciDevelopmentSource>& sources, const TileBuffer& writeBuffer) const {
    std::vector<int> allowedBuiltLotIds;
    std::size_t consumedIndex = 0;
    for (; consumedIndex < consumedSourceIndices.size(); ++consumedIndex) {
        const RciDevelopmentSource& source = sources[consumedSourceIndices[consumedIndex]];
        if (source.isBuilt) {
            allowedBuiltLotIds.push_back(source.lotId);
        }
    }

    int tileY = rect.minTileY;
    for (; tileY <= rect.maxTileY; ++tileY) {
        int tileX = rect.minTileX;
        for (; tileX <= rect.maxTileX; ++tileX) {
            if (!isTileInsideMap(tileX, tileY)) {
                return false;
            }

            const int tileLinearIndex = tileIndex(tileX, tileY);
            if (tileLinearIndex < 0 ||
                tileLinearIndex >= static_cast<int>(writeBuffer.tiles.size()) ||
                tileLinearIndex >= static_cast<int>(lotOccupancy_.size())) {
                return false;
            }

            const Tile& tile = writeBuffer.tiles[static_cast<std::size_t>(tileLinearIndex)];
            if (tile.zoningType != zoningType || transportNetwork_.hasGroundOccupancy(tileLinearIndex)) {
                return false;
            }

            const int lotId = lotOccupancy_[static_cast<std::size_t>(tileLinearIndex)];
            if (lotId == kInvalidLotId) {
                if (!isTileZoneableForRci(tileLinearIndex, tile)) {
                    return false;
                }
                continue;
            }

            if (std::find(allowedBuiltLotIds.begin(), allowedBuiltLotIds.end(), lotId) == allowedBuiltLotIds.end()) {
                return false;
            }
        }
    }

    return true;
}

const LotAsset* SimulationRuntime::findRciConstructorLotAsset(std::uint16_t zoningType, const std::string& rciTypeId, int width, int height, float demandBudget, float maxDensityPerTile, std::uint8_t frontDirection, std::uint32_t variationSeed, int& rotationSteps, int& capacity) const {
    rotationSteps = 0;
    capacity = 0;
    if (rciTypeId.empty() || width <= 0 || height <= 0 || demandBudget <= 0.0f || maxDensityPerTile <= 0.0f) {
        return 0;
    }

    std::vector<const LotAsset*> bestMatches;
    std::vector<int> bestRotations;
    const float densityCapacityLimit = maxDensityPerTile * static_cast<float>(width * height);
    std::size_t lotAssetIndex = 0;
    for (; lotAssetIndex < lotAssets_.size(); ++lotAssetIndex) {
        const LotAsset& lotAsset = lotAssets_[lotAssetIndex];
        const std::string lotRciTypeId = lotAsset.rciTypeId.empty() ? DefaultRciTypeIdForZoningType(zoningType) : lotAsset.rciTypeId;
        if (RciZoningFamily(lotAsset.zoningType) != RciZoningFamily(zoningType) || lotRciTypeId != rciTypeId) {
            continue;
        }

        int candidateRotationSteps = 0;
        for (; candidateRotationSteps < 4; ++candidateRotationSteps) {
            const std::uint8_t assetFrontDirection = lotAsset.hasFrontDirection ? lotAsset.frontDirection : kRoadDirectionNorth;
            if (RotateRoadDirection(assetFrontDirection, candidateRotationSteps) != frontDirection) {
                continue;
            }
            if (!lotTemplateSupportsParcelSize(lotAsset, width, height, candidateRotationSteps)) {
                continue;
            }

            const int candidateCapacity = rciCapacityForLotAsset(lotAsset, zoningType, width, height, candidateRotationSteps);
            if (candidateCapacity <= 0 ||
                static_cast<float>(candidateCapacity) > demandBudget + 0.001f ||
                static_cast<float>(candidateCapacity) > densityCapacityLimit + 0.001f) {
                continue;
            }

            if (bestMatches.empty() || candidateCapacity > capacity) {
                bestMatches.clear();
                bestRotations.clear();
                bestMatches.push_back(&lotAsset);
                bestRotations.push_back(candidateRotationSteps);
                capacity = candidateCapacity;
            } else if (candidateCapacity == capacity) {
                bestMatches.push_back(&lotAsset);
                bestRotations.push_back(candidateRotationSteps);
            }
        }
    }

    if (bestMatches.empty()) {
        return 0;
    }

    const std::size_t selectedIndex = static_cast<std::size_t>(variationSeed % static_cast<std::uint32_t>(bestMatches.size()));
    rotationSteps = bestRotations[selectedIndex];
    return bestMatches[selectedIndex];
}

bool SimulationRuntime::lotTemplateSupportsParcelSize(const LotAsset& lotAsset, int width, int depth, int rotationSteps) const {
    int minWidth = lotAsset.compatibility.minWidth;
    int maxWidth = lotAsset.compatibility.maxWidth;
    int minDepth = lotAsset.compatibility.minDepth;
    int maxDepth = lotAsset.compatibility.maxDepth;
    if ((NormalizeRotationSteps(rotationSteps) % 2) != 0) {
        std::swap(minWidth, minDepth);
        std::swap(maxWidth, maxDepth);
    }

    return width >= minWidth && width <= maxWidth &&
        depth >= minDepth && depth <= maxDepth;
}

const RciTool* SimulationRuntime::findRciToolByZoningType(std::uint16_t zoningType) const {
    const std::vector<RciTool>& tools = rciTools_.tools();
    std::size_t toolIndex = 0;
    for (; toolIndex < tools.size(); ++toolIndex) {
        if (tools[toolIndex].zoningType() == zoningType) {
            return &tools[toolIndex];
        }
    }

    return 0;
}

bool SimulationRuntime::hasRciConstructorLotAsset(std::uint16_t zoningType, int width, int height, std::uint8_t frontDirection) const {
    const std::vector<RciType>& rciTypes = rciTools_.rciTypes();
    std::size_t typeIndex = 0;
    for (; typeIndex < rciTypes.size(); ++typeIndex) {
        if (!rciTypes[typeIndex].allowsZoningType(zoningType)) {
            continue;
        }

        int rotationSteps = 0;
        int capacity = 0;
        if (findRciConstructorLotAsset(zoningType, rciTypes[typeIndex].id(), width, height, 1000000000.0f, 1000000.0f, frontDirection, 0u, rotationSteps, capacity) != 0) {
            return true;
        }
    }

    int rotationSteps = 0;
    int capacity = 0;
    return findRciConstructorLotAsset(zoningType, DefaultRciTypeIdForZoningType(zoningType), width, height, 1000000000.0f, 1000000.0f, frontDirection, 0u, rotationSteps, capacity) != 0;
}

const RciGrowthRule* SimulationRuntime::findRciGrowthRule(std::uint16_t zoningType) const {
    std::size_t ruleIndex = 0;
    for (; ruleIndex < rciGrowthRules_.size(); ++ruleIndex) {
        if (rciGrowthRules_[ruleIndex].zoningType == zoningType) {
            return &rciGrowthRules_[ruleIndex];
        }
    }

    return 0;
}

const RciDesirabilityRule* SimulationRuntime::findRciDesirabilityRule(const std::string& rciTypeId) const {
    std::size_t ruleIndex = 0;
    for (; ruleIndex < rciDesirabilityRules_.size(); ++ruleIndex) {
        if (rciDesirabilityRules_[ruleIndex].rciTypeId == rciTypeId) {
            return &rciDesirabilityRules_[ruleIndex];
        }
    }

    return 0;
}

float SimulationRuntime::rciMaxDensityPerTile(std::uint16_t zoningType) const {
    const RciGrowthRule* growthRule = findRciGrowthRule(zoningType);
    if (growthRule == 0 || growthRule->densityPoints.empty()) {
        return 0.0f;
    }

    const std::vector<RciDensityPoint>& points = growthRule->densityPoints;
    if (cityPopulation_ <= points.front().population || points.size() == 1u) {
        return points.front().maxDensityPerTile;
    }

    std::size_t pointIndex = 1;
    for (; pointIndex < points.size(); ++pointIndex) {
        if (cityPopulation_ > points[pointIndex].population) {
            continue;
        }

        const RciDensityPoint& lower = points[pointIndex - 1u];
        const RciDensityPoint& upper = points[pointIndex];
        const float populationSpan = static_cast<float>(upper.population - lower.population);
        if (populationSpan <= 0.0f) {
            return upper.maxDensityPerTile;
        }

        const float interpolation = static_cast<float>(cityPopulation_ - lower.population) / populationSpan;
        return lower.maxDensityPerTile + ((upper.maxDensityPerTile - lower.maxDensityPerTile) * interpolation);
    }

    return points.back().maxDensityPerTile;
}

int SimulationRuntime::averageLandValueForRect(const RciRect& rect, const TileBuffer& writeBuffer, bool& hasTiles) const {
    hasTiles = false;
    if (!rect.isValid() || rect.width() <= 0 || rect.height() <= 0 || writeBuffer.tiles.empty()) {
        return 0;
    }

    int landValueTotal = 0;
    int tileCount = 0;
    int tileY = rect.minTileY;
    for (; tileY <= rect.maxTileY; ++tileY) {
        int tileX = rect.minTileX;
        for (; tileX <= rect.maxTileX; ++tileX) {
            if (!isTileInsideMap(tileX, tileY)) {
                continue;
            }

            const int linearIndex = tileIndex(tileX, tileY);
            if (linearIndex < 0 || linearIndex >= static_cast<int>(writeBuffer.tiles.size())) {
                continue;
            }

            landValueTotal += std::max(0, writeBuffer.tiles[static_cast<std::size_t>(linearIndex)].landValue);
            ++tileCount;
        }
    }

    hasTiles = tileCount > 0;
    return tileCount > 0 ? landValueTotal / tileCount : 0;
}

std::string SimulationRuntime::rciLandValueLevelForLot(const PublishedLotInfo& publishedLotInfo, const LotAsset& lotAsset, const TileBuffer& writeBuffer) const {
    if (publishedLotInfo.isEmpty || publishedLotInfo.zoningType == TileZoningNone) {
        return std::string();
    }

    const int footprintArea = publishedLotInfo.footprintWidth * publishedLotInfo.footprintHeight;
    if (footprintArea <= 0) {
        return std::string();
    }

    const float cityMaxDensityPerTile = rciMaxDensityPerTile(publishedLotInfo.zoningType);
    const int capacity = rciCapacityForLotAsset(lotAsset, publishedLotInfo.zoningType, publishedLotInfo.footprintWidth, publishedLotInfo.footprintHeight, 0);
    if (cityMaxDensityPerTile <= 0.0f || capacity <= 0) {
        return std::string();
    }

    const RciRect lotRect(
        publishedLotInfo.minimumTileX,
        publishedLotInfo.minimumTileY,
        publishedLotInfo.minimumTileX + publishedLotInfo.footprintWidth - 1,
        publishedLotInfo.minimumTileY + publishedLotInfo.footprintHeight - 1);

    bool hasTiles = false;
    const int averageLandValue = averageLandValueForRect(lotRect, writeBuffer, hasTiles);
    if (!hasTiles) {
        return std::string();
    }

    const float densityPerTile = static_cast<float>(capacity) / static_cast<float>(footprintArea);
    const float starterFloor = RciStarterDensityFloor(publishedLotInfo.zoningType);
    float requiredLandValue = 0.0f;
    if (densityPerTile > starterFloor) {
        const float requiredLandValueMultiplier = densityPerTile / cityMaxDensityPerTile;
        requiredLandValue = static_cast<float>(kLandValueDisplayMinimum) +
            (requiredLandValueMultiplier * static_cast<float>(kLandValueDisplayCap - kLandValueDisplayMinimum));
    }

    if (requiredLandValue <= 0.001f) {
        return "medium";
    }

    const float averageLandValueFloat = static_cast<float>(averageLandValue);
    if (averageLandValueFloat <= requiredLandValue * 0.5f) {
        return "low";
    }

    if (averageLandValueFloat >= requiredLandValue * 2.0f) {
        return "high";
    }

    return "medium";
}

float SimulationRuntime::rciLandValueDensityMultiplier(const RciRect& rect, const TileBuffer& writeBuffer) const {
    if (!rect.isValid() || rect.width() <= 0 || rect.height() <= 0 || writeBuffer.tiles.empty()) {
        return 0.0f;
    }

    bool hasTiles = false;
    const int averageLandValueInt = averageLandValueForRect(rect, writeBuffer, hasTiles);

    if (!hasTiles || kLandValueDisplayCap <= kLandValueDisplayMinimum) {
        return 0.0f;
    }

    const float averageLandValue = static_cast<float>(averageLandValueInt);
    const float normalizedLandValue = std::max(
        0.0f,
        std::min(
            (averageLandValue - static_cast<float>(kLandValueDisplayMinimum)) /
                static_cast<float>(kLandValueDisplayCap - kLandValueDisplayMinimum),
            1.0f));
    return normalizedLandValue;
}

float SimulationRuntime::rciLocalMaxDensityPerTile(std::uint16_t zoningType, const RciRect& rect, const TileBuffer& writeBuffer) const {
    const float cityMaxDensityPerTile = rciMaxDensityPerTile(zoningType);
    if (cityMaxDensityPerTile <= 0.0f) {
        return 0.0f;
    }

    return std::max(
        RciStarterDensityFloor(zoningType),
        cityMaxDensityPerTile * rciLandValueDensityMultiplier(rect, writeBuffer));
}

int SimulationRuntime::rciDesirabilityForTile(const std::string& rciTypeId, const Tile& tile) const {
    const RciDesirabilityRule* rule = findRciDesirabilityRule(rciTypeId);
    if (rule == 0) {
        return 100;
    }

    int desirability = rule->baseline;
    std::size_t sensitivityIndex = 0;
    for (; sensitivityIndex < rule->sensitivities.size(); ++sensitivityIndex) {
        desirability += EvaluateDesirabilitySensitivity(rule->sensitivities[sensitivityIndex], tile);
    }

    return ClampDesirability(desirability);
}

int SimulationRuntime::rciDesirabilityForRect(const std::string& rciTypeId, const RciRect& rect, const TileBuffer& writeBuffer) const {
    if (!rect.isValid() || rect.width() <= 0 || rect.height() <= 0 || writeBuffer.tiles.empty()) {
        return 0;
    }

    int desirabilityTotal = 0;
    int tileCount = 0;
    int tileY = rect.minTileY;
    for (; tileY <= rect.maxTileY; ++tileY) {
        int tileX = rect.minTileX;
        for (; tileX <= rect.maxTileX; ++tileX) {
            if (!isTileInsideMap(tileX, tileY)) {
                continue;
            }

            const int linearIndex = tileIndex(tileX, tileY);
            if (linearIndex < 0 || linearIndex >= static_cast<int>(writeBuffer.tiles.size())) {
                continue;
            }

            desirabilityTotal += rciDesirabilityForTile(rciTypeId, writeBuffer.tiles[static_cast<std::size_t>(linearIndex)]);
            ++tileCount;
        }
    }

    return tileCount > 0 ? desirabilityTotal / tileCount : 0;
}

int SimulationRuntime::rciDesirabilityForCandidate(const Lot& lot, const LotAsset& lotAsset, const TileBuffer& writeBuffer) const {
    std::vector<std::uint32_t> accessNodes;
    collectLotAccessNodes(lot, lotAsset, static_cast<std::uint8_t>(kTransportModeCar | kTransportModePedestrian), accessNodes);
    if (accessNodes.empty()) {
        return 0;
    }

    const RciRect lotRect(
        lot.minimumTileX(),
        lot.minimumTileY(),
        lot.minimumTileX() + lot.footprintWidth() - 1,
        lot.minimumTileY() + lot.footprintHeight() - 1);
    const std::string rciTypeId = lotAsset.rciTypeId.empty() ? DefaultRciTypeIdForZoningType(lotAsset.zoningType) : lotAsset.rciTypeId;
    return rciDesirabilityForRect(rciTypeId, lotRect, writeBuffer);
}

std::uint32_t SimulationRuntime::rciVariationSeedForRect(std::uint16_t zoningType, const RciRect& rect) const {
    std::uint32_t hash = 2166136261u;
    MixRciHash(hash, static_cast<std::uint32_t>(zoningType));
    MixRciHash(hash, static_cast<std::uint32_t>(rect.minTileX));
    MixRciHash(hash, static_cast<std::uint32_t>(rect.minTileY));
    MixRciHash(hash, static_cast<std::uint32_t>(rect.maxTileX));
    MixRciHash(hash, static_cast<std::uint32_t>(rect.maxTileY));
    return hash;
}

bool SimulationRuntime::rciParcelTileIsAvailable(int tileX, int tileY, std::uint16_t zoningType, const TileBuffer& writeBuffer, const std::vector<std::uint8_t>& blockedTiles) const {
    if (!isTileInsideMap(tileX, tileY)) {
        return false;
    }

    const int tileLinearIndex = tileIndex(tileX, tileY);
    if (tileLinearIndex < 0 ||
        tileLinearIndex >= static_cast<int>(writeBuffer.tiles.size()) ||
        tileLinearIndex >= static_cast<int>(lotOccupancy_.size()) ||
        tileLinearIndex >= static_cast<int>(blockedTiles.size()) ||
        blockedTiles[static_cast<std::size_t>(tileLinearIndex)] != 0) {
        return false;
    }

    const Tile& tile = writeBuffer.tiles[static_cast<std::size_t>(tileLinearIndex)];
    if (tile.zoningType != zoningType || lotOccupancy_[static_cast<std::size_t>(tileLinearIndex)] != kInvalidLotId) {
        return false;
    }

    return isTileZoneableForRci(tileLinearIndex, tile);
}

bool SimulationRuntime::rciParcelRectIsAvailable(const RciRect& rect, std::uint16_t zoningType, const TileBuffer& writeBuffer, const std::vector<std::uint8_t>& blockedTiles) const {
    if (!rect.isValid()) {
        return false;
    }

    int tileY = rect.minTileY;
    for (; tileY <= rect.maxTileY; ++tileY) {
        int tileX = rect.minTileX;
        for (; tileX <= rect.maxTileX; ++tileX) {
            if (!rciParcelTileIsAvailable(tileX, tileY, zoningType, writeBuffer, blockedTiles)) {
                return false;
            }
        }
    }

    return true;
}

void SimulationRuntime::markRciParcelBlocked(const RciRect& rect, std::vector<std::uint8_t>& blockedTiles) const {
    if (!rect.isValid()) {
        return;
    }

    const int minTileX = std::max(0, rect.minTileX);
    const int minTileY = std::max(0, rect.minTileY);
    const int maxTileX = std::min(mapWidth_ - 1, rect.maxTileX);
    const int maxTileY = std::min(mapHeight_ - 1, rect.maxTileY);
    int tileY = minTileY;
    for (; tileY <= maxTileY; ++tileY) {
        int tileX = minTileX;
        for (; tileX <= maxTileX; ++tileX) {
            const int tileLinearIndex = tileIndex(tileX, tileY);
            if (tileLinearIndex >= 0 && tileLinearIndex < static_cast<int>(blockedTiles.size())) {
                blockedTiles[static_cast<std::size_t>(tileLinearIndex)] = 1u;
            }
        }
    }
}

bool SimulationRuntime::tryAddRciParcel(const RciTool& tool, const RciRect& rect, std::uint8_t frontDirection, TileBuffer& writeBuffer, std::vector<std::uint8_t>& blockedTiles) {
    if (!rect.isValid() ||
        !rciParcelRectIsAvailable(rect, tool.zoningType(), writeBuffer, blockedTiles) ||
        !hasRciConstructorLotAsset(tool.zoningType(), rect.width(), rect.height(), frontDirection)) {
        return false;
    }

    RciLot zoningLot;
    zoningLot.toolId = tool.id();
    zoningLot.name = tool.name();
    zoningLot.zoningType = tool.zoningType();
    zoningLot.frontDirection = frontDirection;
    zoningLot.color = tool.color();
    zoningLot.rect = rect;
    zoningLots_.push_back(zoningLot);
    markRciParcelBlocked(rect, blockedTiles);
    return true;
}

int SimulationRuntime::rciRoadFacingDepthAtTile(int tileX, int tileY, int deltaX, int deltaY, std::uint16_t zoningType, const TileBuffer& writeBuffer, const std::vector<std::uint8_t>& blockedTiles, int maximumDepth) const {
    int depth = 0;
    while (depth < maximumDepth) {
        const int testTileX = tileX + (deltaX * depth);
        const int testTileY = tileY + (deltaY * depth);
        if (!rciParcelTileIsAvailable(testTileX, testTileY, zoningType, writeBuffer, blockedTiles)) {
            break;
        }

        ++depth;
    }

    return depth;
}

RciRect SimulationRuntime::mapRoadFacingRciLotRect(int roadFacingDirection, int frontageStartX, int frontageStartY, const RciRect& localRect) const {
    if (roadFacingDirection == kRciRoadFacingSouth) {
        return RciRect(
            frontageStartX + localRect.minTileX,
            frontageStartY - localRect.maxTileY,
            frontageStartX + localRect.maxTileX,
            frontageStartY - localRect.minTileY);
    }

    if (roadFacingDirection == kRciRoadFacingWest) {
        return RciRect(
            frontageStartX + localRect.minTileY,
            frontageStartY + localRect.minTileX,
            frontageStartX + localRect.maxTileY,
            frontageStartY + localRect.maxTileX);
    }

    if (roadFacingDirection == kRciRoadFacingEast) {
        return RciRect(
            frontageStartX - localRect.maxTileY,
            frontageStartY + localRect.minTileX,
            frontageStartX - localRect.minTileY,
            frontageStartY + localRect.maxTileX);
    }

    return RciRect(
        frontageStartX + localRect.minTileX,
        frontageStartY + localRect.minTileY,
        frontageStartX + localRect.maxTileX,
        frontageStartY + localRect.maxTileY);
}

std::size_t SimulationRuntime::parcelizeRoadFacingRciRun(const RciTool& tool, int roadFacingDirection, int frontageStartX, int frontageStartY, const std::vector<int>& frontageDepths, TileBuffer& writeBuffer, std::vector<std::uint8_t>& blockedTiles) {
    std::size_t addedParcels = 0u;
    int cursor = 0;
    while (cursor < static_cast<int>(frontageDepths.size())) {
        while (cursor < static_cast<int>(frontageDepths.size()) && frontageDepths[static_cast<std::size_t>(cursor)] < tool.minDepth()) {
            ++cursor;
        }
        if (cursor >= static_cast<int>(frontageDepths.size())) {
            break;
        }

        const int availableDepth = std::min(frontageDepths[static_cast<std::size_t>(cursor)], tool.maxDepth());
        int targetDepth = availableDepth >= tool.preferredDepth() ? tool.preferredDepth() : availableDepth;
        targetDepth = std::max(tool.minDepth(), targetDepth);

        int segmentEnd = cursor;
        while (segmentEnd < static_cast<int>(frontageDepths.size()) && frontageDepths[static_cast<std::size_t>(segmentEnd)] >= targetDepth) {
            ++segmentEnd;
        }

        if (segmentEnd - cursor < tool.minWidth() && targetDepth > tool.minDepth()) {
            targetDepth = tool.minDepth();
            segmentEnd = cursor;
            while (segmentEnd < static_cast<int>(frontageDepths.size()) && frontageDepths[static_cast<std::size_t>(segmentEnd)] >= targetDepth) {
                ++segmentEnd;
            }
        }

        const int frontageWidth = segmentEnd - cursor;
        if (frontageWidth >= tool.minWidth() && targetDepth >= tool.minDepth()) {
            RciPlan localPlan;
            if (tool.buildPlan(0, 0, frontageWidth - 1, targetDepth - 1, RciPlanMode::Lots, frontageWidth, targetDepth, localPlan)) {
                std::size_t lotIndex = 0;
                for (; lotIndex < localPlan.lots.size(); ++lotIndex) {
                    const RciRect localRect(
                        localPlan.lots[lotIndex].rect.minTileX + cursor,
                        localPlan.lots[lotIndex].rect.minTileY,
                        localPlan.lots[lotIndex].rect.maxTileX + cursor,
                        localPlan.lots[lotIndex].rect.maxTileY);
                    const RciRect worldRect = mapRoadFacingRciLotRect(roadFacingDirection, frontageStartX, frontageStartY, localRect);
                    std::uint8_t frontDirection = kRoadDirectionNorth;
                    if (roadFacingDirection == kRciRoadFacingSouth) {
                        frontDirection = kRoadDirectionSouth;
                    } else if (roadFacingDirection == kRciRoadFacingWest) {
                        frontDirection = kRoadDirectionWest;
                    } else if (roadFacingDirection == kRciRoadFacingEast) {
                        frontDirection = kRoadDirectionEast;
                    }
                    if (tryAddRciParcel(tool, worldRect, frontDirection, writeBuffer, blockedTiles)) {
                        ++addedParcels;
                    }
                }
            }
        }

        cursor = std::max(cursor + 1, segmentEnd);
    }

    return addedParcels;
}

std::size_t SimulationRuntime::parcelizeRoadFacingRciTiles(std::uint16_t zoningType, const RciTool& tool, TileBuffer& writeBuffer, std::vector<std::uint8_t>& blockedTiles) {
    const auto hasGroundRoad = [this](int tileX, int tileY) -> bool {
        return isTileInsideMap(tileX, tileY) &&
            transportNetwork_.hasGroundOccupancy(tileIndex(tileX, tileY));
    };

    std::size_t addedParcels = 0u;
    int tileY = 0;
    for (; tileY < mapHeight_; ++tileY) {
        int tileX = 0;
        while (tileX < mapWidth_) {
            if (!rciParcelTileIsAvailable(tileX, tileY, zoningType, writeBuffer, blockedTiles) || !hasGroundRoad(tileX, tileY - 1)) {
                ++tileX;
                continue;
            }

            const int runStartX = tileX;
            std::vector<int> frontageDepths;
            while (tileX < mapWidth_ &&
                rciParcelTileIsAvailable(tileX, tileY, zoningType, writeBuffer, blockedTiles) &&
                hasGroundRoad(tileX, tileY - 1)) {
                frontageDepths.push_back(rciRoadFacingDepthAtTile(tileX, tileY, 0, 1, zoningType, writeBuffer, blockedTiles, tool.maxDepth()));
                ++tileX;
            }
            addedParcels += parcelizeRoadFacingRciRun(tool, kRciRoadFacingNorth, runStartX, tileY, frontageDepths, writeBuffer, blockedTiles);
        }
    }

    for (tileY = mapHeight_ - 1; tileY >= 0; --tileY) {
        int tileX = 0;
        while (tileX < mapWidth_) {
            if (!rciParcelTileIsAvailable(tileX, tileY, zoningType, writeBuffer, blockedTiles) || !hasGroundRoad(tileX, tileY + 1)) {
                ++tileX;
                continue;
            }

            const int runStartX = tileX;
            std::vector<int> frontageDepths;
            while (tileX < mapWidth_ &&
                rciParcelTileIsAvailable(tileX, tileY, zoningType, writeBuffer, blockedTiles) &&
                hasGroundRoad(tileX, tileY + 1)) {
                frontageDepths.push_back(rciRoadFacingDepthAtTile(tileX, tileY, 0, -1, zoningType, writeBuffer, blockedTiles, tool.maxDepth()));
                ++tileX;
            }
            addedParcels += parcelizeRoadFacingRciRun(tool, kRciRoadFacingSouth, runStartX, tileY, frontageDepths, writeBuffer, blockedTiles);
        }
    }

    int tileX = 0;
    for (; tileX < mapWidth_; ++tileX) {
        tileY = 0;
        while (tileY < mapHeight_) {
            if (!rciParcelTileIsAvailable(tileX, tileY, zoningType, writeBuffer, blockedTiles) || !hasGroundRoad(tileX - 1, tileY)) {
                ++tileY;
                continue;
            }

            const int runStartY = tileY;
            std::vector<int> frontageDepths;
            while (tileY < mapHeight_ &&
                rciParcelTileIsAvailable(tileX, tileY, zoningType, writeBuffer, blockedTiles) &&
                hasGroundRoad(tileX - 1, tileY)) {
                frontageDepths.push_back(rciRoadFacingDepthAtTile(tileX, tileY, 1, 0, zoningType, writeBuffer, blockedTiles, tool.maxDepth()));
                ++tileY;
            }
            addedParcels += parcelizeRoadFacingRciRun(tool, kRciRoadFacingWest, tileX, runStartY, frontageDepths, writeBuffer, blockedTiles);
        }
    }

    for (tileX = mapWidth_ - 1; tileX >= 0; --tileX) {
        tileY = 0;
        while (tileY < mapHeight_) {
            if (!rciParcelTileIsAvailable(tileX, tileY, zoningType, writeBuffer, blockedTiles) || !hasGroundRoad(tileX + 1, tileY)) {
                ++tileY;
                continue;
            }

            const int runStartY = tileY;
            std::vector<int> frontageDepths;
            while (tileY < mapHeight_ &&
                rciParcelTileIsAvailable(tileX, tileY, zoningType, writeBuffer, blockedTiles) &&
                hasGroundRoad(tileX + 1, tileY)) {
                frontageDepths.push_back(rciRoadFacingDepthAtTile(tileX, tileY, -1, 0, zoningType, writeBuffer, blockedTiles, tool.maxDepth()));
                ++tileY;
            }
            addedParcels += parcelizeRoadFacingRciRun(tool, kRciRoadFacingEast, tileX, runStartY, frontageDepths, writeBuffer, blockedTiles);
        }
    }

    return addedParcels;
}

std::size_t SimulationRuntime::parcelizeRemainingRciTiles(std::uint16_t zoningType, const RciTool& tool, TileBuffer& writeBuffer, std::vector<std::uint8_t>& blockedTiles) {
    std::size_t addedParcels = 0u;
    int tileY = 0;
    for (; tileY < mapHeight_; ++tileY) {
        int tileX = 0;
        for (; tileX < mapWidth_; ++tileX) {
            if (!rciParcelTileIsAvailable(tileX, tileY, zoningType, writeBuffer, blockedTiles)) {
                continue;
            }

            int rectangleWidth = 0;
            while (tileX + rectangleWidth < mapWidth_ &&
                rciParcelTileIsAvailable(tileX + rectangleWidth, tileY, zoningType, writeBuffer, blockedTiles)) {
                ++rectangleWidth;
            }

            int rectangleHeight = 0;
            bool rowIsAvailable = true;
            while (tileY + rectangleHeight < mapHeight_ && rowIsAvailable) {
                int offsetX = 0;
                for (; offsetX < rectangleWidth; ++offsetX) {
                    if (!rciParcelTileIsAvailable(tileX + offsetX, tileY + rectangleHeight, zoningType, writeBuffer, blockedTiles)) {
                        rowIsAvailable = false;
                        break;
                    }
                }

                if (rowIsAvailable) {
                    ++rectangleHeight;
                }
            }

            if (rectangleWidth < tool.minWidth() || rectangleHeight < tool.minDepth()) {
                continue;
            }

            RciPlan plan;
            if (!tool.buildPlan(tileX, tileY, tileX + rectangleWidth - 1, tileY + rectangleHeight - 1, RciPlanMode::Lots, mapWidth_, mapHeight_, plan)) {
                continue;
            }

            std::size_t lotIndex = 0;
            for (; lotIndex < plan.lots.size(); ++lotIndex) {
                if (tryAddRciParcel(tool, plan.lots[lotIndex].rect, plan.lots[lotIndex].frontDirection, writeBuffer, blockedTiles)) {
                    ++addedParcels;
                }
            }
        }
    }

    return addedParcels;
}

std::size_t SimulationRuntime::parcelizeUnparcelledRciTiles(std::uint16_t zoningType, TileBuffer& writeBuffer) {
    if (!IsRciZoningType(zoningType)) {
        return 0u;
    }

    const RciTool* tool = findRciToolByZoningType(zoningType);
    if (tool == 0) {
        return 0u;
    }

    const std::size_t totalTileCount = static_cast<std::size_t>(mapWidth_) * static_cast<std::size_t>(mapHeight_);
    std::vector<std::uint8_t> blockedTiles(totalTileCount, 0u);
    std::size_t zoningLotIndex = 0;
    for (; zoningLotIndex < zoningLots_.size(); ++zoningLotIndex) {
        markRciParcelBlocked(zoningLots_[zoningLotIndex].rect, blockedTiles);
    }

    const std::size_t addedParcels =
        parcelizeRoadFacingRciTiles(zoningType, *tool, writeBuffer, blockedTiles) +
        parcelizeRemainingRciTiles(zoningType, *tool, writeBuffer, blockedTiles);
    if (addedParcels > 0u) {
        ++zoningLotsRevision_;
    }

    return addedParcels;
}

std::size_t SimulationRuntime::parcelizeAllUnparcelledRciTiles(TileBuffer& writeBuffer) {
    std::size_t addedParcels = 0u;
    std::vector<std::uint16_t> zoningTypes;
    const std::vector<RciTool>& tools = rciTools_.tools();
    std::size_t toolIndex = 0;
    for (; toolIndex < tools.size(); ++toolIndex) {
        const std::uint16_t zoningType = tools[toolIndex].zoningType();
        if (std::find(zoningTypes.begin(), zoningTypes.end(), zoningType) == zoningTypes.end()) {
            zoningTypes.push_back(zoningType);
        }
    }

    std::size_t zoningIndex = 0;
    for (; zoningIndex < zoningTypes.size(); ++zoningIndex) {
        addedParcels += parcelizeUnparcelledRciTiles(zoningTypes[zoningIndex], writeBuffer);
    }

    return addedParcels;
}

float SimulationRuntime::rciDemandForRciType(const std::string& rciTypeId) const {
    const int residentsLowWealthId = cityParameterRegistry_.residentsLowWealthId();
    const int jobsLowWealthId = cityParameterRegistry_.jobsLowWealthId();
    const int jobsDirtyIndustryId = cityParameterRegistry_.jobsDirtyIndustryId();
    const int demandParameterId = rciDemandParameterId(rciTypeId);

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

    if (demandParameterId == residentsLowWealthId) {
        return std::max(0.0f, initialDemand(residentsLowWealthId) + parameterValue(jobsLowWealthId) - parameterValue(residentsLowWealthId));
    }

    if (demandParameterId == jobsDirtyIndustryId) {
        return std::max(0.0f, initialDemand(jobsDirtyIndustryId) + (parameterValue(residentsLowWealthId) * kDirtyIndustryDemandPerLowWealthWorker) - parameterValue(jobsDirtyIndustryId));
    }

    return demandParameterId < 0 ? 0.0f : std::max(0.0f, initialDemand(demandParameterId) - parameterValue(demandParameterId));
}

int SimulationRuntime::rciPendingConstructionCapacity(const std::string& rciTypeId) const {
    int capacity = 0;
    const TileBuffer& readBuffer = tileBuffers_[simulationReadBufferIndex_];
    std::size_t lotIndex = 0;
    for (; lotIndex < lots_.size(); ++lotIndex) {
        const Lot& lot = lots_[lotIndex];
        if (!lot.isUnderConstruction()) {
            continue;
        }

        const LotAsset* lotAsset = findLotAssetById(lot.assetId());
        const std::uint16_t lotZoningType = lotAsset == 0 ? TileZoningNone : zoningTypeForLotInBuffer(lot, readBuffer, lotAsset->zoningType);
        const std::string lotRciTypeId = lotAsset == 0
            ? std::string()
            : (lotAsset->rciTypeId.empty() ? DefaultRciTypeIdForZoningType(lotZoningType) : lotAsset->rciTypeId);
        if (lotAsset == 0 || lotRciTypeId != rciTypeId) {
            continue;
        }

        capacity += rciCapacityForLot(lot, lotRciTypeId);
    }

    return capacity;
}

int SimulationRuntime::rciCapacityForLotAsset(const LotAsset& lotAsset, std::uint16_t zoningType) const {
    return rciCapacityForLotAsset(lotAsset, zoningType, lotAsset.footprintWidth, lotAsset.footprintHeight, 0);
}

int SimulationRuntime::rciCapacityForLotAsset(const LotAsset& lotAsset, std::uint16_t zoningType, int width, int height, int rotationSteps) const {
    if (!lotAsset.autoLayout.empty()) {
        Lot candidateLot;
        if (!buildLotCandidateForParcel(lotAsset, 0, 0, rotationSteps, kInvalidLotId, width, height, candidateLot)) {
            return 0;
        }

        const std::string resolvedRciTypeId = lotAsset.rciTypeId.empty() ? DefaultRciTypeIdForZoningType(zoningType) : lotAsset.rciTypeId;
        return rciCapacityForLot(candidateLot, resolvedRciTypeId);
    }

    const std::string rciTypeId = lotAsset.rciTypeId.empty() ? DefaultRciTypeIdForZoningType(zoningType) : lotAsset.rciTypeId;
    const int parameterId = rciDemandParameterId(rciTypeId);
    if (parameterId < 0) {
        return 0;
    }

    int capacity = 0;
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

int SimulationRuntime::rciCapacityForLot(const Lot& lot, const std::string& rciTypeId) const {
    const int parameterId = rciDemandParameterId(rciTypeId);
    if (parameterId < 0) {
        return 0;
    }

    int capacity = 0;
    const std::vector<CityParameterContribution>& contributions = lot.parameterContributions();
    std::size_t contributionIndex = 0;
    for (; contributionIndex < contributions.size(); ++contributionIndex) {
        if (contributions[contributionIndex].parameterId == parameterId) {
            capacity += contributions[contributionIndex].amount;
        }
    }

    return capacity;
}

int SimulationRuntime::rciDemandParameterId(const std::string& rciTypeId) const {
    const RciType* rciType = rciTools_.findRciType(rciTypeId);
    if (rciType != 0 && !rciType->demandParameterId().empty()) {
        return cityParameterRegistry_.parameterId(rciType->demandParameterId());
    }

    if (rciTypeId == "low_wealth_residential") {
        return cityParameterRegistry_.residentsLowWealthId();
    }

    if (rciTypeId == "dirty_industry") {
        return cityParameterRegistry_.jobsDirtyIndustryId();
    }

    return -1;
}

int SimulationRuntime::rciDemandParameterId(std::uint16_t zoningType) const {
    return rciDemandParameterId(DefaultRciTypeIdForZoningType(zoningType));
}

RciLot SimulationRuntime::buildRedevelopmentRciLot(const Lot& lot, const LotAsset& lotAsset, std::uint16_t zoningType, std::uint64_t availableAfterTick) const {
    RciLot redevelopmentLot;
    redevelopmentLot.toolId = RciToolIdForZoningType(zoningType);
    redevelopmentLot.name = RciNameForZoningType(zoningType);
    redevelopmentLot.zoningType = zoningType;
    redevelopmentLot.frontDirection = RotateRoadDirection(lotAsset.hasFrontDirection ? lotAsset.frontDirection : kRoadDirectionNorth, lot.rotationSteps());
    redevelopmentLot.color = RciColorForZoningType(zoningType);
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
    const std::uint16_t zoningType = zoningTypeForLotInBuffer(lots_[lotIndex], writeBuffer, lotAsset.zoningType);
    const RciLot redevelopmentLot = buildRedevelopmentRciLot(lots_[lotIndex], lotAsset, zoningType, availableAfterTick);

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

        writeBuffer.tiles[tileLinearIndex].zoningType = zoningType;
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
    targetLot->addModule(
        moduleAsset,
        localOrigin,
        mapWidth_,
        rotatedModuleWidth,
        rotatedModuleHeight,
        0.0f,
        0.0f,
        static_cast<float>(rotatedModuleWidth),
        static_cast<float>(rotatedModuleHeight));

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

    if (!targetLot->removeModule(moduleInstanceId, mapWidth_)) {
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
                    return exposeRciLotForRedevelopment(lotIndex, *targetLotAsset, writeBuffer, simulationTick_ + RciRedevelopmentGraceTicks());
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
        targetLot->rebaseAnchorToMinimumTile(mapWidth_);
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

                return exposeRciLotForRedevelopment(lotIndex, *lotAsset, writeBuffer, simulationTick_ + RciRedevelopmentGraceTicks());
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

    std::vector<int> dirtyRoadTopologyTiles;
    if (transportNetwork_.removeRoadAtTile(clickedTileX, clickedTileY, &dirtyRoadTopologyTiles)) {
        queueCommuteRecalculationForRoadTopologyChange(dirtyRoadTopologyTiles);
        return true;
    }

    return false;
}

bool SimulationRuntime::tryBulldozeArea(int startTileX, int startTileY, int endTileX, int endTileY, TileBuffer& writeBuffer) {
    const int minTileX = std::max(0, std::min(startTileX, endTileX));
    const int maxTileX = std::min(mapWidth_ - 1, std::max(startTileX, endTileX));
    const int minTileY = std::max(0, std::min(startTileY, endTileY));
    const int maxTileY = std::min(mapHeight_ - 1, std::max(startTileY, endTileY));

    bool removedAny = false;
    std::vector<int> roadTileIndices;
    roadTileIndices.reserve(static_cast<std::size_t>(maxTileX - minTileX + 1) * static_cast<std::size_t>(maxTileY - minTileY + 1));
    int tileY = minTileY;
    for (; tileY <= maxTileY; ++tileY) {
        int tileX = minTileX;
        for (; tileX <= maxTileX; ++tileX) {
            const int tileLinearIndex = tileIndex(tileX, tileY);
            if (lotOccupancy_[tileLinearIndex] != kInvalidLotId) {
                removedAny = tryBulldozeAtTile(tileX, tileY, writeBuffer) || removedAny;
            } else {
                roadTileIndices.push_back(tileLinearIndex);
            }
        }
    }

    std::vector<int> dirtyRoadTopologyTiles;
    if (transportNetwork_.removeRoadsAtTiles(roadTileIndices, &dirtyRoadTopologyTiles)) {
        queueCommuteRecalculationForRoadTopologyChange(dirtyRoadTopologyTiles);
        removedAny = true;
    }

    return removedAny;
}

bool SimulationRuntime::tryZoneArea(int startTileX, int startTileY, int endTileX, int endTileY, std::uint16_t zoningType, TileBuffer& writeBuffer) {
    if (zoningType == TileZoningNone) {
        return tryClearZoningArea(startTileX, startTileY, endTileX, endTileY, writeBuffer);
    }

    const RciRect rect(
        std::max(0, std::min(startTileX, endTileX)),
        std::max(0, std::min(startTileY, endTileY)),
        std::min(mapWidth_ - 1, std::max(startTileX, endTileX)),
        std::min(mapHeight_ - 1, std::max(startTileY, endTileY)));

    std::vector<int> changedTileIndices;
    bool hasZoneableTile = false;
    const bool changedTiles = applyZoningRect(rect, zoningType, writeBuffer, changedTileIndices, hasZoneableTile);
    if (!hasZoneableTile) {
        return false;
    }

    if (!changedTileIndices.empty()) {
        removeZoningLotsIntersectingRect(rect);
        markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, changedTileIndices);
    }

    const bool addedParcels = parcelizeUnparcelledRciTiles(zoningType, writeBuffer) > 0u;
    return changedTiles || addedParcels;
}

bool SimulationRuntime::tryClearZoningArea(int startTileX, int startTileY, int endTileX, int endTileY, TileBuffer& writeBuffer) {
    const RciRect rect(
        std::max(0, std::min(startTileX, endTileX)),
        std::max(0, std::min(startTileY, endTileY)),
        std::min(mapWidth_ - 1, std::max(startTileX, endTileX)),
        std::min(mapHeight_ - 1, std::max(startTileY, endTileY)));
    if (!rect.isValid()) {
        return false;
    }

    std::vector<int> changedTileIndices;
    int tileY = rect.minTileY;
    for (; tileY <= rect.maxTileY; ++tileY) {
        int tileX = rect.minTileX;
        for (; tileX <= rect.maxTileX; ++tileX) {
            const int tileLinearIndex = tileIndex(tileX, tileY);
            if (tileLinearIndex < 0 ||
                tileLinearIndex >= static_cast<int>(lotOccupancy_.size()) ||
                lotOccupancy_[tileLinearIndex] != kInvalidLotId) {
                continue;
            }

            Tile& tile = writeBuffer.tiles[tileLinearIndex];
            if (tile.zoningType == TileZoningNone) {
                continue;
            }

            tile.zoningType = TileZoningNone;
            changedTileIndices.push_back(tileLinearIndex);
        }
    }

    const bool removedZoningLots = removeZoningLotsIntersectingRect(rect);
    if (!changedTileIndices.empty()) {
        markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, changedTileIndices);
    }
    if (removedZoningLots || !changedTileIndices.empty()) {
        parcelizeAllUnparcelledRciTiles(writeBuffer);
    }
    return removedZoningLots || !changedTileIndices.empty();
}

bool SimulationRuntime::tryZoneLot(const RciLot& zoningLot, TileBuffer& writeBuffer) {
    if (zoningLot.zoningType == TileZoningNone || !zoningLot.rect.isValid()) {
        return false;
    }

    const RciRect rect(
        std::max(0, zoningLot.rect.minTileX),
        std::max(0, zoningLot.rect.minTileY),
        std::min(mapWidth_ - 1, zoningLot.rect.maxTileX),
        std::min(mapHeight_ - 1, zoningLot.rect.maxTileY));
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

bool SimulationRuntime::tryApplyRciPlan(const RciPlan& plan, TileBuffer& writeBuffer) {
    if (plan.zoningType == TileZoningNone || !plan.bounds.isValid()) {
        return false;
    }

    std::vector<RciRect> paintRects = plan.paintRects;
    if (paintRects.empty()) {
        paintRects = plan.zoneRects;
    }
    if (paintRects.empty()) {
        paintRects.push_back(plan.bounds);
    }

    bool changed = false;
    std::size_t paintRectIndex = 0u;
    for (; paintRectIndex < paintRects.size(); ++paintRectIndex) {
        changed = removeZoningLotsIntersectingRect(paintRects[paintRectIndex]) || changed;
    }

    std::vector<int> allChangedTileIndices;
    for (paintRectIndex = 0u; paintRectIndex < paintRects.size(); ++paintRectIndex) {
        std::vector<int> changedTileIndices;
        bool hasZoneableTile = false;
        const bool changedTiles = applyZoningRect(paintRects[paintRectIndex], plan.zoningType, writeBuffer, changedTileIndices, hasZoneableTile);
        if (changedTiles) {
            allChangedTileIndices.insert(allChangedTileIndices.end(), changedTileIndices.begin(), changedTileIndices.end());
            changed = true;
        }
    }

    if (!allChangedTileIndices.empty()) {
        std::sort(allChangedTileIndices.begin(), allChangedTileIndices.end());
        allChangedTileIndices.erase(std::unique(allChangedTileIndices.begin(), allChangedTileIndices.end()), allChangedTileIndices.end());
        markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, allChangedTileIndices);
    }

    std::size_t roadIndex = 0u;
    for (; roadIndex < plan.roadPlans.size(); ++roadIndex) {
        const RoadStrokeCommand roadStrokeCommand = BuildRciRoadStrokeCommand(plan.roadPlans[roadIndex]);
        std::vector<int> dirtyRoadTopologyTiles;
        if (transportNetwork_.placeRoadStroke(roadStrokeCommand, lotOccupancy_, kInvalidLotId, &dirtyRoadTopologyTiles)) {
            clearZoningForRoadStroke(roadStrokeCommand, writeBuffer, false);
            queueCommuteRecalculationForRoadTopologyChange(dirtyRoadTopologyTiles);
            changed = true;
        }
    }

    std::size_t lotIndex = 0u;
    for (; lotIndex < plan.lots.size(); ++lotIndex) {
        changed = tryZoneLot(plan.lots[lotIndex], writeBuffer) || changed;
    }

    return changed;
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

void SimulationRuntime::clearZoningForRoadStroke(const RoadStrokeCommand& roadStrokeCommand, TileBuffer& writeBuffer, bool parcelizeAfterClear) {
    RoadTemplate roadTemplate = roadStrokeCommand.roadTemplate;
    if (roadTemplate.elements.empty()) {
        RoadTemplateKind templateKind = roadStrokeCommand.templateKind;
        if (roadStrokeCommand.family == RoadFamily::Highway || roadStrokeCommand.layer == TransportLayerId::Elevated) {
            templateKind = RoadTemplateKind::Highway;
        }
        roadTemplate = Road::makeTemplate(templateKind, RoadTrafficSide::RightHand, RoadDirectionMode::TwoWay);
    } else {
        roadTemplate.family = roadStrokeCommand.family;
        roadTemplate.layer = roadStrokeCommand.layer;
    }

    std::vector<RoadTilePlacement> placements;
    placements.reserve(512);
    Road road(roadTemplate);
    if (!road.appendStrokePlacements(roadStrokeCommand.startTile, roadStrokeCommand.cornerTile, roadStrokeCommand.endTile, mapWidth_, mapHeight_, placements)) {
        return;
    }

    std::vector<int> changedTileIndices;
    changedTileIndices.reserve(placements.size());
    bool removedZoningLots = false;
    std::size_t placementIndex = 0;
    for (; placementIndex < placements.size(); ++placementIndex) {
        const int tileLinearIndex = placements[placementIndex].tileIndex;
        if (tileLinearIndex < 0 || tileLinearIndex >= static_cast<int>(writeBuffer.tiles.size())) {
            continue;
        }

        removedZoningLots = removeZoningLotsIntersectingRect(RciRect(
            placements[placementIndex].tileX,
            placements[placementIndex].tileY,
            placements[placementIndex].tileX,
            placements[placementIndex].tileY)) || removedZoningLots;
        if (writeBuffer.tiles[tileLinearIndex].zoningType == TileZoningNone) {
            continue;
        }

        writeBuffer.tiles[tileLinearIndex].zoningType = TileZoningNone;
        changedTileIndices.push_back(tileLinearIndex);
    }

    if (!changedTileIndices.empty()) {
        std::sort(changedTileIndices.begin(), changedTileIndices.end());
        changedTileIndices.erase(std::unique(changedTileIndices.begin(), changedTileIndices.end()), changedTileIndices.end());
        markChunksDirtyByTileIndices(writeBuffer.chunkRevisions, changedTileIndices);
    }

    if (parcelizeAfterClear && (removedZoningLots || !changedTileIndices.empty())) {
        parcelizeAllUnparcelledRciTiles(writeBuffer);
    }
}

// Checks occupancy and ground-road conflicts for a candidate lot footprint.
bool SimulationRuntime::canPlaceLot(const Lot& candidateLot) const {
    const std::vector<int>& occupiedTileIndices = candidateLot.occupiedTileIndices();
    std::size_t tileIndexValue = 0;
    for (; tileIndexValue < occupiedTileIndices.size(); ++tileIndexValue) {
        const int tileLinearIndex = occupiedTileIndices[tileIndexValue];
        const int tileY = tileLinearIndex / mapWidth_;
        const int tileX = tileLinearIndex - (tileY * mapWidth_);
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
        const int candidateTileY = candidateTileIndex / mapWidth_;
        const int candidateTileX = candidateTileIndex - (candidateTileY * mapWidth_);

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

int SimulationRuntime::lotParameterAmount(const Lot& lot, int parameterId) const {
    if (parameterId < 0) {
        return 0;
    }

    int amount = 0;
    const std::vector<CityParameterContribution>& contributions = lot.parameterContributions();
    std::size_t contributionIndex = 0;
    for (; contributionIndex < contributions.size(); ++contributionIndex) {
        if (contributions[contributionIndex].parameterId == parameterId) {
            amount += contributions[contributionIndex].amount;
        }
    }

    return amount;
}

int SimulationRuntime::lotDerivedParameterAmount(const Lot& lot, int parameterId) const {
    float amount = static_cast<float>(lotParameterAmount(lot, parameterId));
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
                amount += static_cast<float>(contribution.amount) * impact.multiplier;
            }
        }
    }

    return RoundToNearestInt(amount);
}

int SimulationRuntime::lotActualParameterAmount(const Lot& lot, const LotAsset* lotAsset, int parameterId, const TileBuffer& writeBuffer) const {
    const int maximumAmount = lotParameterAmount(lot, parameterId);
    if (maximumAmount <= 0 || lotAsset == 0) {
        return maximumAmount;
    }

    const std::uint16_t lotZoningType = zoningTypeForLotInBuffer(lot, writeBuffer, lotAsset->zoningType);
    if (!IsRciZoningType(lotZoningType)) {
        return maximumAmount;
    }

    return RciActualCapacityFromDesirability(maximumAmount, rciDesirabilityForCandidate(lot, *lotAsset, writeBuffer));
}

int SimulationRuntime::lotActualDerivedParameterAmount(const Lot& lot, const LotAsset* lotAsset, int parameterId, const TileBuffer& writeBuffer) const {
    if (parameterId < 0) {
        return 0;
    }

    const bool scalesByDesirability = lotAsset != 0 && IsRciZoningType(zoningTypeForLotInBuffer(lot, writeBuffer, lotAsset->zoningType));
    const int desirability = scalesByDesirability ? rciDesirabilityForCandidate(lot, *lotAsset, writeBuffer) : kRciDesirabilityDisplayCap;
    float amount = 0.0f;
    const std::vector<CityParameterContribution>& contributions = lot.parameterContributions();
    std::size_t contributionIndex = 0;
    for (; contributionIndex < contributions.size(); ++contributionIndex) {
        const CityParameterContribution& contribution = contributions[contributionIndex];
        if (contribution.parameterId < 0 || contribution.parameterId >= static_cast<int>(cityParameterRegistry_.count())) {
            continue;
        }

        const int contributionAmount = scalesByDesirability
            ? RciActualCapacityFromDesirability(contribution.amount, desirability)
            : contribution.amount;
        if (contribution.parameterId == parameterId) {
            amount += static_cast<float>(contributionAmount);
        }

        const CityParameterDefinition& definition = cityParameterRegistry_.definition(contribution.parameterId);
        std::size_t impactIndex = 0;
        for (; impactIndex < definition.impacts.size(); ++impactIndex) {
            const CityParameterImpact& impact = definition.impacts[impactIndex];
            if (impact.targetParameterId == parameterId) {
                amount += static_cast<float>(contributionAmount) * impact.multiplier;
            }
        }
    }

    return RoundToNearestInt(amount);
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

        Int2 localTile = accessDefinition.localTile;
        if (accessDefinition.isDynamic) {
            const int normalizedRotation = NormalizeRotationSteps(lot.rotationSteps());
            const int unrotatedWidth = (normalizedRotation % 2) == 0 ? lot.footprintWidth() : lot.footprintHeight();
            const int unrotatedHeight = (normalizedRotation % 2) == 0 ? lot.footprintHeight() : lot.footprintWidth();
            LotAutoPrimaryGeometry primary;
            localTile = ResolveLotAutoAccessTile(accessDefinition, unrotatedWidth, unrotatedHeight, primary);
        }

        const Int2 rotatedLocalTile = RotateLocalTile(localTile, lot.rotationSteps());
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

std::vector<CommuteRouteSegment> SimulationRuntime::buildCommuteRouteSegments(const TransportPathResult& pathResult, std::uint16_t demand, CommuteTimeOfDay timeOfDay) const {
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
        const int fromTileY = fromTileIndex / mapWidth_;
        const int fromTileX = fromTileIndex - (fromTileY * mapWidth_);
        const int toTileY = toTileIndex / mapWidth_;
        const int toTileX = toTileIndex - (toTileY * mapWidth_);

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
        segment.timeOfDay = timeOfDay;
        segment.direction = step.roadDirection;
        segment.demand = demand;
        segments.push_back(segment);
    }

    return segments;
}

bool SimulationRuntime::commuteRouteIsStillValid(const CommuteRouteRecord& route) const {
    if (!route.morningPathResult.success || !route.eveningPathResult.success || route.transportLoad == 0u) {
        return false;
    }

    const TransportCostMap& costMap = transportNetwork_.costMap();
    const TransportPathResult* pathResults[] = {
        &route.morningPathResult,
        &route.eveningPathResult
    };

    std::size_t pathIndex = 0;
    for (; pathIndex < sizeof(pathResults) / sizeof(pathResults[0]); ++pathIndex) {
        const TransportPathResult& pathResult = *pathResults[pathIndex];
        std::size_t stepIndex = 0;
        for (; stepIndex < pathResult.steps.size(); ++stepIndex) {
            const TransportPathStep& step = pathResult.steps[stepIndex];
            if (step.kind == TransportPathStepKind::Movement) {
                const int directionIndex = RoadDirectionIndex(step.roadDirection);
                if (directionIndex < 0 || step.fromNodeId >= costMap.totalNodeCount()) {
                    return false;
                }

                const TransportCostCell& cell = costMap.cell(costMap.nodeLayer(step.fromNodeId), costMap.nodeMode(step.fromNodeId), costMap.nodeTileIndex(step.fromNodeId));
                if (cell.costs[directionIndex] == kTransportNoCost || cell.capacities[directionIndex] == 0u) {
                    return false;
                }
            } else if (step.transferEdgeIndex >= costMap.trafficLoadState(CommuteTimeOfDay::Morning).transferLoads.size()) {
                return false;
            }
        }
    }

    return true;
}

bool SimulationRuntime::commuteRouteTouchesTiles(const CommuteRouteRecord& route, const std::vector<int>& sortedTileIndices) const {
    return commutePathTouchesTiles(route.morningPathResult, sortedTileIndices) ||
        commutePathTouchesTiles(route.eveningPathResult, sortedTileIndices);
}

bool SimulationRuntime::commutePathTouchesTiles(const TransportPathResult& pathResult, const std::vector<int>& sortedTileIndices) const {
    if (!pathResult.success || sortedTileIndices.empty()) {
        return false;
    }

    const TransportCostMap& costMap = transportNetwork_.costMap();
    const std::size_t totalNodeCount = costMap.totalNodeCount();
    std::size_t stepIndex = 0;
    for (; stepIndex < pathResult.steps.size(); ++stepIndex) {
        const TransportPathStep& step = pathResult.steps[stepIndex];
        const std::uint32_t nodeIds[] = {
            step.fromNodeId,
            step.toNodeId
        };

        std::size_t nodeIndex = 0;
        for (; nodeIndex < sizeof(nodeIds) / sizeof(nodeIds[0]); ++nodeIndex) {
            if (nodeIds[nodeIndex] >= totalNodeCount) {
                continue;
            }

            const int tileIndexValue = costMap.nodeTileIndex(nodeIds[nodeIndex]);
            if (std::binary_search(sortedTileIndices.begin(), sortedTileIndices.end(), tileIndexValue)) {
                return true;
            }
        }
    }

    return false;
}

bool SimulationRuntime::lotAccessMayTouchTiles(const Lot& lot, const std::vector<int>& sortedTileIndices) const {
    if (sortedTileIndices.empty()) {
        return false;
    }

    const std::vector<int>& occupiedTileIndices = lot.occupiedTileIndices();
    std::size_t occupiedIndex = 0;
    for (; occupiedIndex < occupiedTileIndices.size(); ++occupiedIndex) {
        const int occupiedTileIndex = occupiedTileIndices[occupiedIndex];
        if (occupiedTileIndex < 0 || occupiedTileIndex >= static_cast<int>(transportNetwork_.totalTileCount())) {
            continue;
        }

        const int tileY = occupiedTileIndex / mapWidth_;
        const int tileX = occupiedTileIndex - (tileY * mapWidth_);
        const int adjacentTileIndices[] = {
            occupiedTileIndex,
            isTileInsideMap(tileX, tileY - 1) ? tileIndex(tileX, tileY - 1) : -1,
            isTileInsideMap(tileX, tileY + 1) ? tileIndex(tileX, tileY + 1) : -1,
            isTileInsideMap(tileX - 1, tileY) ? tileIndex(tileX - 1, tileY) : -1,
            isTileInsideMap(tileX + 1, tileY) ? tileIndex(tileX + 1, tileY) : -1
        };

        std::size_t adjacentIndex = 0;
        for (; adjacentIndex < sizeof(adjacentTileIndices) / sizeof(adjacentTileIndices[0]); ++adjacentIndex) {
            if (adjacentTileIndices[adjacentIndex] >= 0 &&
                std::binary_search(sortedTileIndices.begin(), sortedTileIndices.end(), adjacentTileIndices[adjacentIndex])) {
                return true;
            }
        }
    }

    return false;
}

// Validates a tile coordinate against the fixed map bounds.
bool SimulationRuntime::isTileInsideMap(int tileX, int tileY) const {
    return tileX >= 0 && tileX < mapWidth_ && tileY >= 0 && tileY < mapHeight_;
}

// Converts a tile coordinate to row-major storage index.
int SimulationRuntime::tileIndex(int tileX, int tileY) const {
    return (tileY * mapWidth_) + tileX;
}
