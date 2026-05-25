#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "RendererPayload.h"
#include "TransportTypes.h"

constexpr std::uint16_t kTransportNoCost = 0u;
constexpr std::uint16_t kTransportMaxCost = std::numeric_limits<std::uint16_t>::max();
constexpr std::uint16_t kTransportMaxLoad = std::numeric_limits<std::uint16_t>::max();

enum class TransportPathStepKind : std::uint8_t {
    Movement = 0,
    Transfer
};

struct TransportCostCell {
    std::uint16_t costs[kRoadDirectionCount];
    std::uint16_t capacities[kRoadDirectionCount];
    std::uint8_t buildingAccessMask;

    TransportCostCell();
    void clearCosts();
};

struct TransportTrafficLoadCell {
    std::uint16_t oldLoads[kRoadDirectionCount];
    std::uint16_t newLoads[kRoadDirectionCount];

    TransportTrafficLoadCell();
    void clearLoads();
};

struct TransportTrafficTransferLoad {
    std::uint16_t oldLoad;
    std::uint16_t newLoad;

    TransportTrafficTransferLoad();
};

struct TransportTouchedMovementLoad {
    std::uint32_t nodeId;
    std::uint8_t directionIndex;

    TransportTouchedMovementLoad();
    TransportTouchedMovementLoad(std::uint32_t nodeIdValue, std::uint8_t directionIndexValue);
};

struct TransportTrafficLoadState {
    std::vector<TransportTrafficLoadCell> cells;
    std::vector<TransportTrafficTransferLoad> transferLoads;
    std::vector<TransportTouchedMovementLoad> touchedMovementLoads;
    std::vector<std::uint32_t> touchedTransferLoads;
    std::vector<bool> touchedMovementFlags;
    std::vector<bool> touchedTransferFlags;
    bool nextLoadStartsFromZero;

    TransportTrafficLoadState();
    void initialize(std::size_t nodeCount, std::size_t transferCount);
    void clearLoads();
    void resetTouchedLoads(bool startsFromZero);
};

struct TransportCongestionPoint {
    float utilization;
    float speedMultiplier;

    TransportCongestionPoint();
    TransportCongestionPoint(float utilizationValue, float speedMultiplierValue);
};

struct TransportCongestionCurve {
    std::vector<TransportCongestionPoint> points;

    TransportCongestionCurve();
};

struct TransportTransferEdge {
    std::uint32_t fromNodeId;
    std::uint32_t toNodeId;
    std::uint16_t cost;
    std::uint16_t capacity;

    TransportTransferEdge();
};

struct TransportPathStep {
    std::uint32_t fromNodeId;
    std::uint32_t toNodeId;
    std::uint8_t roadDirection;
    TransportPathStepKind kind;
    std::uint32_t transferEdgeIndex;

    TransportPathStep();
};

struct TransportPathRequest {
    std::vector<std::uint32_t> startNodeIds;
    std::vector<std::uint32_t> goalNodeIds;
    std::uint32_t routeSeed;
    std::uint16_t demand;
    float maximumCost;
    bool useCongestion;
    CommuteTimeOfDay commuteTimeOfDay;

    TransportPathRequest();
};

struct TransportPathResult {
    bool success;
    float totalCost;
    std::uint32_t reachedNodeId;
    std::vector<TransportPathStep> steps;

    TransportPathResult();
};

class TransportPathScratch {
public:
    struct HeapEntry {
        std::uint32_t nodeId;
        float priority;
        float costSoFar;
    };

    TransportPathScratch();
    void reset(std::size_t nodeCount);

private:
    std::vector<float> costs;
    std::vector<std::uint32_t> stamps;
    std::vector<std::uint32_t> closedStamps;
    std::vector<std::uint32_t> goalStamps;
    std::vector<std::uint32_t> parentNodes;
    std::vector<std::uint8_t> parentDirections;
    std::vector<std::uint8_t> parentKinds;
    std::vector<std::uint32_t> parentTransferEdges;
    std::vector<HeapEntry> heap;
    std::uint32_t currentStamp;

    friend class TransportCostMap;
};

class TransportCostMap {
public:
    TransportCostMap();

    void initialize(int width, int height);
    void clear();
    void clearCosts();
    void clearCostsForTile(TransportLayerId layer, int tileIndex);
    void clearLoads();

    int width() const;
    int height() const;
    std::size_t totalTileCount() const;
    std::size_t totalNodeCount() const;

    std::uint32_t nodeId(TransportLayerId layer, TransportMode mode, int tileIndex) const;
    std::uint32_t nodeId(TransportLayerId layer, TransportMode mode, int tileX, int tileY) const;
    int nodeTileIndex(std::uint32_t nodeIdValue) const;
    TransportMode nodeMode(std::uint32_t nodeIdValue) const;
    TransportLayerId nodeLayer(std::uint32_t nodeIdValue) const;

    const TransportCostCell& cell(TransportLayerId layer, TransportMode mode, int tileIndex) const;
    TransportCostCell& cellForMutation(TransportLayerId layer, TransportMode mode, int tileIndex);
    const TransportTrafficLoadState& trafficLoadState(CommuteTimeOfDay commuteTimeOfDay) const;
    TransportTrafficLoadState& trafficLoadStateForMutation(CommuteTimeOfDay commuteTimeOfDay);

    void addDirectionalCost(TransportLayerId layer, TransportMode mode, int tileIndex, std::uint8_t roadDirection, std::uint16_t cost, std::uint16_t capacity);
    void addBuildingAccess(TransportLayerId layer, TransportMode mode, int tileIndex, std::uint8_t buildingAccessMask);
    std::uint32_t addTransferEdge(std::uint32_t fromNodeId, std::uint32_t toNodeId, std::uint16_t cost, std::uint16_t capacity);
    void clearTransferEdges();
    void finalizeTransferEdges();
    void collectBuildingAccessNodes(int footprintX, int footprintY, int footprintWidth, int footprintHeight, std::uint8_t allowedModeMask, std::vector<std::uint32_t>& nodeIds) const;
    void setCongestionCurve(const TransportCongestionCurve& congestionCurve);

    void beginNextLoadFromOldLoad(CommuteTimeOfDay commuteTimeOfDay);
    void beginNextLoadFromZero(CommuteTimeOfDay commuteTimeOfDay);
    void commitNextLoad(CommuteTimeOfDay commuteTimeOfDay, std::vector<int>* touchedTileIndices = 0);
    void applyPathLoad(CommuteTimeOfDay commuteTimeOfDay, const TransportPathResult& pathResult, std::uint16_t demand, bool addLoad);

    bool findPath(const TransportPathRequest& request, TransportPathScratch& scratch, TransportPathResult& result) const;
    void buildTrafficOverlay(std::vector<RendererScalarPayload>& overlayPayloads) const;
    void buildTrafficOverlayForTiles(const std::vector<int>& tileIndices, std::vector<RendererScalarPayload>& overlayPayloads) const;

private:
    bool isTileInsideMap(int tileX, int tileY) const;
    bool tryNeighborTile(int tileIndex, std::uint8_t roadDirection, int& neighborTileIndex) const;
    float movementCostWithCongestion(const TransportCostCell& cell, const TransportTrafficLoadState& loadState, int directionIndex, std::uint32_t routeSeed, std::uint32_t nodeIdValue) const;
    float transferCostWithCongestion(const TransportTransferEdge& transferEdge, const TransportTrafficTransferLoad& transferLoad, std::uint32_t routeSeed, std::uint32_t nodeIdValue) const;
    float routeJitter(std::uint32_t routeSeed, std::uint32_t nodeIdValue, std::uint32_t edgeSalt) const;
    void reconstructPath(std::uint32_t reachedNodeId, const TransportPathScratch& scratch, TransportPathResult& result) const;
    std::size_t commuteTimeIndex(CommuteTimeOfDay commuteTimeOfDay) const;
    void touchMovementLoad(TransportTrafficLoadState& loadState, std::uint32_t nodeIdValue, int directionIndex);
    void touchTransferLoad(TransportTrafficLoadState& loadState, std::uint32_t transferEdgeIndex);

    int width_;
    int height_;
    std::size_t totalTileCount_;
    std::size_t totalNodeCount_;
    std::vector<TransportCostCell> cells_;
    std::vector<TransportTransferEdge> transferEdges_;
    std::vector<std::uint32_t> transferOffsets_;
    std::array<TransportTrafficLoadState, static_cast<std::size_t>(CommuteTimeOfDay::Count)> trafficLoadStates_;
    TransportCongestionCurve congestionCurve_;
    bool transferOffsetsDirty_;
};
