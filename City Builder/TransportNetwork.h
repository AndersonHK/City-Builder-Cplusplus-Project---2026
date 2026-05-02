#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ChunkConfig.h"
#include "Road.h"
#include "TransportTile.h"

class TransportNetwork {
public:
    TransportNetwork();

    void initialize(int width, int height, const std::vector<ChunkRect>& chunkLayout);
    void clear();

    bool placeRoadStroke(const RoadStrokeCommand& roadStrokeCommand, const std::vector<int>& lotOccupancy, int invalidLotId);

    const std::vector<ResolvedRoadCell>& resolvedCells() const;
    const std::vector<std::uint8_t>& groundRoadRenderState() const;
    const std::vector<std::uint64_t>& groundChunkRevisions() const;
    const std::vector<std::uint64_t>& elevatedChunkRevisions() const;
    std::uint64_t revision() const;

    bool hasOccupancy(TransportLayerId layer, int tileIndex) const;
    bool hasGroundOccupancy(int tileIndex) const;

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
    void resolveDirtyTile(TransportLayerId layer, int tileX, int tileY);
    void markDirtyNeighborhood(const std::vector<RoadTilePlacement>& placements, std::vector<int>& dirtyTileIndices) const;
    void bumpDirtyChunkRevisions(TransportLayerId layer, const std::vector<int>& dirtyTileIndices);
    bool pruneInvalidPedestrianLanes(TransportLayerId layer, const std::vector<int>& dirtyTileIndices);

    const TransportTile* tileAt(TransportLayerId layer, int tileX, int tileY) const;
    TransportTile* tileAt(TransportLayerId layer, int tileX, int tileY);
    bool hasCompatibleNeighborLane(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& lanePlacement, std::uint8_t roadDirection) const;
    bool hasCarThroughBothEnds(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& carLane) const;
    bool hasPedestrianThroughBothEnds(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane) const;
    bool pedestrianLaneBordersEmptyTile(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane) const;
    bool pedestrianLaneShouldRenderCrosswalk(TransportLayerId layer, int tileX, int tileY, const RoadLanePlacement& pedestrianLane, const TransportTile& tile) const;
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
    std::vector<std::uint8_t> groundRoadRenderState_;
    std::vector<std::uint64_t> groundChunkRevisions_;
    std::vector<std::uint64_t> elevatedChunkRevisions_;
    std::uint64_t revision_;
};
