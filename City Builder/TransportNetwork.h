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

    bool placeRoadStroke(const RoadStrokeCommand& roadStrokeCommand, const std::vector<int>& lotOccupancy, int invalidLotId);
    bool canPlaceRoadStroke(const RoadStrokeCommand& roadStrokeCommand, const std::vector<int>& lotOccupancy, int invalidLotId) const;
    bool removeRoadAtTile(int tileX, int tileY);

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

    void beginTrafficAssignmentFromZero();
    void applyTrafficPathLoad(const TransportPathResult& pathResult, std::uint16_t demand, bool addLoad);
    void commitTrafficAssignment();

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

private:
    bool isTileInsideMap(int tileX, int tileY) const;
    int tileIndex(int tileX, int tileY) const;
    int chunkIndexForTile(int tileX, int tileY) const;

    bool validateAndApplyPlacements(TransportLayerId layer, const std::vector<RoadTilePlacement>& placements, const std::vector<int>& lotOccupancy, int invalidLotId, bool& madeChange);
    void rebuildRoadTilesInRegion(TransportLayerId layer, const std::vector<int>& tileIndices);
    bool tileIsErased(TransportLayerId layer, int tileIndex) const;
    bool mergeReplayStrokeIds(TransportLayerId layer, const std::vector<RoadTilePlacement>& placements);
    bool mergeConnectedReplayStrokeId(TransportLayerId layer, const RoadLanePlacement& lanePlacement, std::uint32_t oldStrokeId);
    void resolveDirtyTile(TransportLayerId layer, int tileX, int tileY);
    void rebuildCostMapAndTrafficOverlay();
    void addLaneToCostMap(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement);
    void refreshTrafficOverlayState();
    void bumpAllTrafficOverlayChunkRevisions();
    void markDirtyNeighborhood(const std::vector<RoadTilePlacement>& placements, std::vector<int>& dirtyTileIndices) const;
    void markDirtyTileNeighborhood(const std::vector<int>& tileIndices, std::vector<int>& dirtyTileIndices) const;
    void expandDirtyRoadDependencies(TransportLayerId layer, std::vector<int>& dirtyTileIndices) const;
    void bumpDirtyChunkRevisions(TransportLayerId layer, const std::vector<int>& dirtyTileIndices);
    void ensurePedestrianCapLanes(TransportLayerId layer, const std::vector<int>& dirtyTileIndices);
    bool refreshDerivedLaneActivity(TransportLayerId layer, const std::vector<int>& dirtyTileIndices);

    const TransportTile* tileAt(TransportLayerId layer, int tileX, int tileY) const;
    TransportTile* tileAt(TransportLayerId layer, int tileX, int tileY);
    bool tileHasActiveRoadLane(TransportLayerId layer, int tileX, int tileY) const;
    bool tileIsStableStraightSandwich(TransportLayerId layer, int tileX, int tileY) const;
    void collectRoadRemovalFootprint(TransportLayerId layer, int tileX, int tileY, std::vector<int>& removalTileIndices) const;
    void collectRoadSliceFootprint(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& referenceLane, std::vector<int>& removalTileIndices) const;
    bool tileHasMatchingRoadSliceLane(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& referenceLane) const;
    bool tileHasPathLaneBody(TransportLayerId layer, int tileX, int tileY, RoadFamily family, RoadLaneTypeId laneType, RoadAxis axis, bool includeInactiveLanes = false) const;
    bool tileHasPathLaneTravel(TransportLayerId layer, int tileX, int tileY, RoadFamily family, RoadLaneTypeId laneType, RoadAxis axis, std::uint8_t roadDirection) const;
    bool intersectionGroupBounds(TransportLayerId layer, int tileX, int tileY, RoadFamily family, int& minTileX, int& minTileY, int& maxTileX, int& maxTileY) const;
    bool intersectionGroupHasThroughStroke(TransportLayerId layer, int tileX, int tileY, RoadFamily family, RoadAxis axis) const;
    bool authoredStrokeCoversMovementEdge(TransportLayerId layer, int tileX, int tileY, RoadFamily family, std::uint8_t roadDirection) const;
    std::uint8_t liveCapReturnDirectionForLane(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection) const;
    std::uint8_t carCapLoopMovementMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const;
    std::uint8_t intersectionBodyLaneMovementMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const;
    std::uint8_t livePathLaneMovementMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const;
    bool pathLaneCanMove(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection) const;
    std::uint8_t pathLaneMovementMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const;
    bool tileHasCarBodyAxis(TransportLayerId layer, int tileX, int tileY, RoadAxis axis, RoadFamily family) const;
    bool tileHasAnyCarBody(TransportLayerId layer, int tileX, int tileY, RoadFamily family) const;
    bool tileIsIntersectionGroupBody(TransportLayerId layer, int tileX, int tileY, RoadFamily family) const;
    std::uint8_t buildIntersectionGroupApproachMask(TransportLayerId layer, int tileX, int tileY, RoadFamily family) const;
    bool roadAxisHasTerminalEnd(TransportLayerId layer, int tileX, int tileY, RoadAxis axis, RoadFamily family) const;
    bool separatorLaneIsCrossedByCarLane(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const;
    bool separatorLaneShouldBeActive(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement) const;
    std::uint8_t separatorDividerMaskForLane(const RoadLanePlacement& lanePlacement) const;
    bool tileHasActiveSeparatorEdge(TransportLayerId layer, int tileX, int tileY, RoadAxis separatorAxis, std::uint8_t edgeDirection, RoadFamily family) const;
    bool pedestrianConnectionCrossesSeparator(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection) const;
    bool pedestrianLaneBordersEmptyTile(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane) const;
    bool pedestrianLaneShouldBeActive(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane) const;
    std::uint8_t pedestrianLaneGraphicMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane) const;
    std::uint8_t pedestrianLaneCrosswalkMask(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane, const TransportTile& tile) const;
    bool isSingleAxisDeadEndCapMask(const TransportTile& tile, std::uint8_t junctionMask) const;
    RoadRenderVariant chooseRenderVariantForTile(const TransportTile& tile, std::uint8_t junctionMask) const;
    std::uint8_t baseGlyphJunctionMaskForTile(const TransportTile& tile, RoadRenderVariant renderVariant, std::uint8_t junctionMask) const;
    bool isCarIntersectionNode(TransportLayerId layer, int tileX, int tileY) const;
    bool isCarIntersectionCollectionTile(TransportLayerId layer, int tileX, int tileY) const;
    std::uint8_t buildTurnExitMaskThroughIntersection(TransportLayerId layer, int entryTileX, int entryTileY, std::uint8_t travelDirection) const;
    std::uint8_t buildTurnArrowIntentMask(TransportLayerId layer, int tileX, int tileY, const TransportTile& tile) const;
    std::uint8_t buildCarExitMask(TransportLayerId layer, int tileX, int tileY, const TransportTile& tile) const;
    std::uint8_t buildExitMask(TransportLayerId layer, int tileX, int tileY, const TransportTile& tile) const;
    std::uint8_t buildJunctionMask(TransportLayerId layer, int tileX, int tileY, const TransportTile& tile, std::uint8_t exitMask) const;

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
