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

enum class RoadTrafficSide : std::uint8_t {
    RightHand = 0,
    LeftHand
};

enum class RoadDirectionMode : std::uint8_t {
    TwoWay = 0,
    OneWayForward,
    OneWayReverse
};

enum class RoadElementKind : std::uint8_t {
    Sidewalk = 0,
    Lane,
    Divider,
    Shoulder
};

enum class RoadLaneRole : std::uint8_t {
    None = 0,
    Through,
    Turn,
    Transit
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

struct RoadElementBehavior {
    RoadElementKind kind;
    float minimumWidth;
    float preferredWidth;
    float maximumWidth;
    std::uint8_t connectorMask;

    // Defaults to one lane-width road element.
    RoadElementBehavior()
        : kind(RoadElementKind::Lane),
          minimumWidth(1.0f),
          preferredWidth(1.0f),
          maximumWidth(1.0f),
          connectorMask(0) {
    }
};

struct RoadLaneFlow {
    std::uint8_t fromMask;
    std::uint8_t toMask;

    // Starts as a lane with no travel direction.
    RoadLaneFlow()
        : fromMask(0),
          toMask(0) {
    }
};

struct RoadTemplateElement {
    RoadElementKind kind;
    RoadLaneRole laneRole;
    RoadElementBehavior behavior;
    RoadLaneFlow flow;

    // Starts as a lane element until a template builder fills it.
    RoadTemplateElement()
        : kind(RoadElementKind::Lane),
          laneRole(RoadLaneRole::Through) {
    }
};

struct RoadTemplate {
    RoadFamily family;
    TransportLayerId layer;
    RoadTrafficSide trafficSide;
    RoadDirectionMode directionMode;
    int laneCount;
    std::vector<RoadTemplateElement> elements;

    // Defaults to a one-lane-per-direction right-hand local street template.
    RoadTemplate()
        : family(RoadFamily::LocalStreet),
          layer(TransportLayerId::Ground),
          trafficSide(RoadTrafficSide::RightHand),
          directionMode(RoadDirectionMode::TwoWay),
          laneCount(1) {
    }
};

struct RoadStrokeCommand {
    Int2 startTile;
    Int2 cornerTile;
    Int2 endTile;
    RoadFamily family;
    TransportLayerId layer;
    RoadStrokeOperation operation;
    RoadTemplate roadTemplate;

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
    std::uint8_t elementMask;
    std::uint8_t laneCount;
    std::uint8_t exitMask;
    std::uint8_t sidewalkMask;
    std::uint8_t dividerMask;
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
          elementMask(0),
          laneCount(0),
          exitMask(0),
          sidewalkMask(0),
          dividerMask(0),
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
constexpr std::uint8_t kRoadElementLane = 1u << 0;
constexpr std::uint8_t kRoadElementSidewalk = 1u << 1;
constexpr std::uint8_t kRoadElementDivider = 1u << 2;
constexpr std::uint8_t kRoadElementShoulder = 1u << 3;
constexpr std::uint8_t kRoadDividerWhiteShift = 0u;
constexpr std::uint8_t kRoadDividerYellowShift = 4u;
constexpr std::size_t kGroundRoadRenderChannelsPerTile = 4u;

struct RoadResolvedTile {
    std::uint8_t family;
    std::uint8_t elementMask;
    std::uint8_t laneCount;
    std::uint8_t travelMask;
    std::uint8_t exitMask;

    // Starts with no aggregate tile-level road path data.
    RoadResolvedTile()
        : family(static_cast<std::uint8_t>(RoadFamily::None)),
          elementMask(0),
          laneCount(0),
          travelMask(0),
          exitMask(0) {
    }
};

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
    struct BuildRoadCell {
        std::uint8_t family;
        std::uint8_t elementMask;
        std::uint8_t laneTravelMask;
        std::uint8_t laneCount;
        std::uint8_t sidewalkMask;
        std::uint8_t dividerMask;

        // Starts with no authored road elements.
        BuildRoadCell()
            : family(static_cast<std::uint8_t>(RoadFamily::None)),
              elementMask(0),
              laneTravelMask(0),
              laneCount(0),
              sidewalkMask(0),
              dividerMask(0) {
        }
    };

    struct PendingPlacement {
        int tileX;
        int tileY;
        int tileIndex;
        std::uint8_t elementMask;
        std::uint8_t laneTravelMask;
        std::uint8_t laneCount;
        std::uint8_t sidewalkMask;
        std::uint8_t dividerMask;

        // Starts as an invalid pending stroke tile.
        PendingPlacement()
            : tileX(0),
              tileY(0),
              tileIndex(0),
              elementMask(0),
              laneTravelMask(0),
              laneCount(0),
              sidewalkMask(0),
              dividerMask(0) {
        }
    };

    bool isTileInsideMap(int tileX, int tileY) const;
    int tileIndex(int tileX, int tileY) const;
    int chunkIndexForTile(int tileX, int tileY) const;
    bool appendLegPlacements(const Int2& startTile, const Int2& endTile, const RoadTemplate& roadTemplate, std::vector<PendingPlacement>& placements) const;
    bool appendPlacement(int tileX, int tileY, std::uint8_t elementMask, std::uint8_t laneTravelMask, std::uint8_t laneCount, std::uint8_t sidewalkMask, std::uint8_t dividerMask, std::vector<PendingPlacement>& placements) const;
    void resolveDirtyTile(TransportLayerId layer, int tileX, int tileY);
    bool hasSameFamilyNeighbor(TransportLayerId layer, int tileX, int tileY, RoadFamily family) const;
    bool hasCompatibleLaneNeighbor(TransportLayerId layer, int tileX, int tileY, RoadFamily family, std::uint8_t directionBit) const;
    std::uint8_t buildExitMask(TransportLayerId layer, int tileX, int tileY, RoadFamily family, std::uint8_t laneTravelMask) const;

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
