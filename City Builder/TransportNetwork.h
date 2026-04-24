#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ChunkConfig.h"
#include "LotModule.h"

enum class TransportMode : std::uint8_t {
    Car = 0,
    Pedestrian,
    Count
};

enum class TransportLayerId : std::uint8_t {
    Ground = 0,
    Elevated,
    Underground,
    Count
};

enum class RoadFamily : std::uint8_t {
    None = 0,
    LocalStreet,
    Highway
};

enum class RoadStrokeOperation : std::uint8_t {
    Place = 0
};

enum class RoadRenderVariant : std::uint8_t {
    None = 0,
    Isolated,
    DeadEnd,
    Straight,
    Corner,
    Tee,
    Cross
};

enum class RoadBaseGlyph : std::uint8_t {
    None = 0,
    LocalIsolated,
    LocalDeadEndNorth,
    LocalDeadEndEast,
    LocalDeadEndSouth,
    LocalDeadEndWest,
    LocalStraightVertical,
    LocalStraightHorizontal,
    LocalCornerNorthEast,
    LocalCornerSouthEast,
    LocalCornerSouthWest,
    LocalCornerNorthWest,
    LocalTeeMissingNorth,
    LocalTeeMissingEast,
    LocalTeeMissingSouth,
    LocalTeeMissingWest,
    LocalCross,
    HighwayIsolated,
    HighwayDeadEndNorth,
    HighwayDeadEndEast,
    HighwayDeadEndSouth,
    HighwayDeadEndWest,
    HighwayStraightVertical,
    HighwayStraightHorizontal,
    HighwayCornerNorthEast,
    HighwayCornerSouthEast,
    HighwayCornerSouthWest,
    HighwayCornerNorthWest,
    HighwayTeeMissingNorth,
    HighwayTeeMissingEast,
    HighwayTeeMissingSouth,
    HighwayTeeMissingWest,
    HighwayCross
};

enum class RoadArrowGlyph : std::uint8_t {
    None = 0,
    North,
    East,
    South,
    West,
    NorthEast,
    SouthEast,
    SouthWest,
    NorthWest
};

struct RoadStrokeCommand {
    Int2 startTile;
    Int2 cornerTile;
    Int2 endTile;
    RoadFamily family;
    TransportLayerId layer;
    RoadStrokeOperation operation;

    // Defaults to a ground street placement command.
    RoadStrokeCommand()
        : startTile(0, 0),
          cornerTile(0, 0),
          endTile(0, 0),
          family(RoadFamily::None),
          layer(TransportLayerId::Ground),
          operation(RoadStrokeOperation::Place) {
    }
};

struct ResolvedRoadCell {
    std::uint8_t family;
    std::uint8_t laneIntentMask;
    std::uint8_t exitMask;
    std::uint8_t sidewalkMask;
    std::uint8_t junctionMask;
    std::uint8_t renderVariant;
    std::uint8_t baseGlyph;
    std::uint8_t arrowGlyph;
    std::uint16_t carCost;
    std::uint16_t pedestrianCost;

    // Starts with no resolved road visual or traversal data.
    ResolvedRoadCell()
        : family(static_cast<std::uint8_t>(RoadFamily::None)),
          laneIntentMask(0),
          exitMask(0),
          sidewalkMask(0),
          junctionMask(0),
          renderVariant(static_cast<std::uint8_t>(RoadRenderVariant::None)),
          baseGlyph(static_cast<std::uint8_t>(RoadBaseGlyph::None)),
          arrowGlyph(static_cast<std::uint8_t>(RoadArrowGlyph::None)),
          carCost(0),
          pedestrianCost(0) {
    }
};

constexpr std::uint8_t kRoadDirectionNorth = 1u << 0;
constexpr std::uint8_t kRoadDirectionEast = 1u << 1;
constexpr std::uint8_t kRoadDirectionSouth = 1u << 2;
constexpr std::uint8_t kRoadDirectionWest = 1u << 3;
constexpr std::uint8_t kRoadDirectionNorthEast = 1u << 4;
constexpr std::uint8_t kRoadDirectionSouthEast = 1u << 5;
constexpr std::uint8_t kRoadDirectionSouthWest = 1u << 6;
constexpr std::uint8_t kRoadDirectionNorthWest = 1u << 7;

constexpr std::uint8_t kLaneIntentEast = 1u << 0;
constexpr std::uint8_t kLaneIntentWest = 1u << 1;
constexpr std::uint8_t kLaneIntentNorth = 1u << 2;
constexpr std::uint8_t kLaneIntentSouth = 1u << 3;
constexpr std::size_t kGroundRoadRenderChannelsPerTile = 2u;

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

private:
    struct BuildRoadCell {
        std::uint8_t family;
        std::uint8_t laneIntentMask;

    // Starts with no authored road family or lane intent.
    BuildRoadCell()
            : family(static_cast<std::uint8_t>(RoadFamily::None)),
              laneIntentMask(0) {
        }
    };

    struct PendingPlacement {
        int tileX;
        int tileY;
        int tileIndex;
        std::uint8_t laneIntentMask;

    // Starts as an invalid pending stroke tile.
    PendingPlacement()
            : tileX(0),
              tileY(0),
              tileIndex(0),
              laneIntentMask(0) {
        }
    };

    bool isTileInsideMap(int tileX, int tileY) const;
    int tileIndex(int tileX, int tileY) const;
    int chunkIndexForTile(int tileX, int tileY) const;
    bool appendLegPlacements(const Int2& startTile, const Int2& endTile, std::vector<PendingPlacement>& placements) const;
    bool appendPlacement(int tileX, int tileY, std::uint8_t laneIntentMask, std::vector<PendingPlacement>& placements) const;
    void resolveDirtyTile(TransportLayerId layer, int tileX, int tileY);
    bool hasSameFamilyNeighbor(TransportLayerId layer, int tileX, int tileY, RoadFamily family) const;
    std::uint8_t buildExitMask(TransportLayerId layer, int tileX, int tileY, RoadFamily family, std::uint8_t laneIntentMask) const;

    int width_;
    int height_;
    std::size_t totalTileCount_;
    int chunkWidth_;
    int chunkHeight_;
    int chunksPerRow_;
    std::vector<ChunkRect> chunkLayout_;
    std::vector<BuildRoadCell> buildCells_;
    std::vector<ResolvedRoadCell> resolvedCells_;
    std::vector<std::uint8_t> groundRoadRenderState_;
    std::vector<std::uint64_t> groundChunkRevisions_;
    std::vector<std::uint64_t> elevatedChunkRevisions_;
    std::uint64_t revision_;
};
