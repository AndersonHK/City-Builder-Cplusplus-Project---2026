#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "City.h"
#include "ChunkConfig.h"
#include "Road.h"
#include "TransportCostMap.h"
#include "TransportTile.h"

class TransportNetwork {
public:
    TransportNetwork();

    void initialize(int width, int height, const std::vector<ChunkRect>& chunkLayout);
    void clear();

    bool placeRoadStroke(const RoadStrokeCommand& roadStrokeCommand, const std::vector<int>& lotOccupancy, int invalidLotId, std::vector<int>* topologyDirtyTileIndices = 0);
    bool canPlaceRoadStroke(const RoadStrokeCommand& roadStrokeCommand, const std::vector<int>& lotOccupancy, int invalidLotId) const;
    bool removeRoadAtTile(int tileX, int tileY, std::vector<int>* topologyDirtyTileIndices = 0);
    bool removeRoadsAtTiles(const std::vector<int>& tileIndices, std::vector<int>* topologyDirtyTileIndices = 0);

    const std::vector<ResolvedRoadCell>& resolvedCells() const;
    const TransportCostMap& costMap() const;
    const std::vector<std::uint8_t>& groundRoadRenderState() const;
    const std::vector<std::uint8_t>& trafficOverlayState() const;
    const std::vector<std::uint64_t>& groundChunkRevisions() const;
    const std::vector<std::uint64_t>& elevatedChunkRevisions() const;
    const std::vector<std::uint64_t>& trafficOverlayChunkRevisions() const;
    std::uint64_t revision() const;
    std::uint64_t trafficOverlayRevision() const;
    void setCongestionCurve(const TransportCongestionCurve& congestionCurve);

    void beginTrafficAssignmentFromOldLoad(CommuteTimeOfDay commuteTimeOfDay);
    void beginTrafficAssignmentFromZero(CommuteTimeOfDay commuteTimeOfDay);
    void applyTrafficPathLoad(CommuteTimeOfDay commuteTimeOfDay, const TransportPathResult& pathResult, std::uint16_t demand, bool addLoad);
    void commitTrafficAssignment(CommuteTimeOfDay commuteTimeOfDay);

    bool hasOccupancy(TransportLayerId layer, int tileIndex) const;
    bool hasGroundOccupancy(int tileIndex) const;

    TransportNetworkSaveState exportSaveState() const;
    void importSaveState(const TransportNetworkSaveState& saveState);

    int width() const;
    int height() const;
    std::size_t totalTileCount() const;

    static std::size_t layerCount();
    static std::size_t slotIndex(TransportLayerId layer, int tileIndex, std::size_t totalTileCount);
    static RoadTemplate makeRoadTemplate(RoadFamily family, TransportLayerId layer, int laneCount, RoadTrafficSide trafficSide, RoadDirectionMode directionMode);
    static RoadTemplate makeRoadTemplate(RoadTemplateKind templateKind, RoadTrafficSide trafficSide, RoadDirectionMode directionMode);

private:
    bool isTileInsideMap(int tileX, int tileY) const;
    int tileIndex(int tileX, int tileY) const;
    int chunkIndexForTile(int tileX, int tileY) const;

    bool validateAndApplyPlacements(TransportLayerId layer, const std::vector<RoadTilePlacement>& placements, const std::vector<int>& lotOccupancy, int invalidLotId, bool& madeChange);
    void rebuildRoadTilesInRegion(TransportLayerId layer, const std::vector<int>& tileIndices);
    bool tileIsErased(TransportLayerId layer, int tileIndex) const;
    void collectRoadStrokeTileIndices(const TransportStrokeSaveState& stroke, std::vector<int>& tileIndices) const;
    void resetNextRoadStrokeId();
    bool mergeReplayStrokeIds(TransportLayerId layer, const std::vector<RoadTilePlacement>& placements);
    bool mergeConnectedReplayStrokeId(TransportLayerId layer, const RoadLanePlacement& lanePlacement, std::uint32_t oldStrokeId);
    void resolveDirtyTile(TransportLayerId layer, int tileX, int tileY);
    void rebuildCostMapAndTrafficOverlay();
    void rebuildCostMapAndTrafficOverlayForTiles(const std::vector<int>& dirtyTileIndices);
    void addLaneToCostMap(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement);
    void refreshTrafficOverlayState();
    void bumpAllTrafficOverlayChunkRevisions();
    void bumpTrafficOverlayChunkRevisionsForTiles(const std::vector<int>& dirtyTileIndices);
    void markDirtyNeighborhood(const std::vector<RoadTilePlacement>& placements, std::vector<int>& dirtyTileIndices) const;
    void markDirtyTileNeighborhood(const std::vector<int>& tileIndices, std::vector<int>& dirtyTileIndices) const;
    void expandDirtyRoadDependencies(TransportLayerId layer, std::vector<int>& dirtyTileIndices) const;
    void bumpDirtyChunkRevisions(TransportLayerId layer, const std::vector<int>& dirtyTileIndices);

    const TransportTile* tileAt(TransportLayerId layer, int tileX, int tileY) const;
    TransportTile* tileAt(TransportLayerId layer, int tileX, int tileY);
    bool tileHasActiveRoadLane(TransportLayerId layer, int tileX, int tileY) const;
    bool tileIsStableStraightSandwich(TransportLayerId layer, int tileX, int tileY) const;
    void collectRoadRemovalFootprint(TransportLayerId layer, int tileX, int tileY, std::vector<int>& removalTileIndices) const;
    void collectRoadSliceFootprint(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& referenceLane, std::vector<int>& removalTileIndices) const;
    bool tileHasMatchingRoadSliceLane(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& referenceLane) const;
    std::uint8_t capReturnDirectionForLane(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection) const;
    bool laneHasAuthoredContinuation(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection) const;
    std::uint8_t pathLaneMovementMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const;
    std::uint8_t laneGraphicDirectionMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const;
    std::uint8_t laneCenterTravelDirectionMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const;
    bool isCarIntersectionCollectionTile(TransportLayerId layer, int tileX, int tileY) const;
    std::uint8_t buildCarExitMask(TransportLayerId layer, int tileX, int tileY, const TransportTile& tile) const;
    std::uint8_t buildTurnExitMaskThroughIntersection(TransportLayerId layer, int entryTileX, int entryTileY, std::uint8_t travelDirection) const;
    std::uint8_t buildTurnArrowIntentMask(TransportLayerId layer, int tileX, int tileY, const TransportTile& tile) const;
    bool tileHasCarBodyAxis(TransportLayerId layer, int tileX, int tileY, RoadAxis axis, RoadFamily family) const;
    bool tileHasAnyCarBody(TransportLayerId layer, int tileX, int tileY, RoadFamily family) const;

    int width_;
    int height_;
    std::size_t totalTileCount_;
    int chunkWidth_;
    int chunkHeight_;
    int chunksPerRow_;
    std::vector<ChunkRect> chunkLayout_;
    std::vector<TransportTile> transportTiles_;
    std::vector<ResolvedRoadCell> resolvedCells_;
    TransportCostMap costMap_;
    std::vector<std::uint8_t> groundRoadRenderState_;
    std::vector<std::uint8_t> trafficOverlayState_;
    std::vector<std::uint64_t> groundChunkRevisions_;
    std::vector<std::uint64_t> elevatedChunkRevisions_;
    std::vector<std::uint64_t> trafficOverlayChunkRevisions_;
    std::vector<TransportStrokeSaveState> strokes_;
    std::vector<TransportTileEraseSaveState> tileErasures_;
    std::uint64_t revision_;
    std::uint64_t trafficOverlayRevision_;
    std::uint32_t nextRoadStrokeId_;
};
