#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CommuteTypes.h"
#include "LotModule.h"
#include "Tile.h"

struct LotRenderInstance {
    int lotId;
    int originX;
    int originY;
    int width;
    int height;
    float renderOffsetX;
    float renderOffsetY;
    float renderWidth;
    float renderHeightOverride;
    float renderHeight;
    float colorR;
    float colorG;
    float colorB;
    std::uint8_t surfacePattern;
    std::uint8_t surfaceDirection;
    std::uint16_t zoningType;
    std::uint16_t renderMeshHandle;
    std::uint8_t meshRotation = 0;

    // Defaults to a small neutral placeholder prism.
    LotRenderInstance()
        : lotId(-1),
          originX(0),
          originY(0),
          width(1),
          height(1),
          renderOffsetX(0.0f),
          renderOffsetY(0.0f),
          renderWidth(1.0f),
          renderHeightOverride(1.0f),
          renderHeight(0.5f),
          colorR(0.4f),
          colorG(0.4f),
          colorB(0.4f),
          surfacePattern(0u),
          surfaceDirection(0u),
          zoningType(TileZoningNone),
          renderMeshHandle(0u) {
    }
};

struct LotAccessDefinition {
    Int2 localTile;
    std::uint8_t xReference;
    std::uint8_t yReference;
    int xOffset;
    int yOffset;
    std::uint8_t direction;
    std::uint8_t modeMask;
    bool isDynamic;

    // Defaults to a north-facing, inactive declaration until XML fills it.
    LotAccessDefinition()
        : localTile(0, 0),
          xReference(0u),
          yReference(0u),
          xOffset(0),
          yOffset(0),
          direction(kRoadDirectionNorth),
          modeMask(0),
          isDynamic(false) {
    }
};

struct LotFootprintCompatibility {
    int minWidth;
    int maxWidth;
    int minDepth;
    int maxDepth;
    bool isExplicit;

    LotFootprintCompatibility()
        : minWidth(0),
          maxWidth(0),
          minDepth(0),
          maxDepth(0),
          isExplicit(false) {
    }
};

enum LotAutoCoordinateReference {
    kLotAutoReferenceLotStart = 0,
    kLotAutoReferenceLotCenter = 1,
    kLotAutoReferenceLotEnd = 2,
    kLotAutoReferencePrimaryStart = 3,
    kLotAutoReferencePrimaryCenter = 4,
    kLotAutoReferencePrimaryEnd = 5
};

struct LotAutoSizeCondition {
    int minWidth;
    int maxWidth;
    int minDepth;
    int maxDepth;

    LotAutoSizeCondition()
        : minWidth(1),
          maxWidth(32767),
          minDepth(1),
          maxDepth(32767) {
    }
};

struct LotAutoModuleRule {
    std::string moduleId;
    LotAutoSizeCondition condition;
    std::uint8_t xReference;
    std::uint8_t yReference;
    int xOffset;
    int yOffset;
    int footprintWidth;
    int footprintHeight;
    float renderOffsetX;
    float renderOffsetY;
    float renderWidth;
    float renderHeight;
    bool hasRenderOffsetX;
    bool hasRenderOffsetY;
    bool hasRenderWidth;
    bool hasRenderHeight;
    std::uint8_t renderAlignX;
    std::uint8_t renderAlignY;
    bool isPrimary;
    bool required;
    bool affectsSimulation;
    bool claimsFootprint;
    std::vector<std::string> primaryModuleIds;
    std::vector<LotModuleAlternative> alternatives;

    LotAutoModuleRule()
        : xReference(kLotAutoReferenceLotStart),
          yReference(kLotAutoReferenceLotStart),
          xOffset(0),
          yOffset(0),
          footprintWidth(0),
          footprintHeight(0),
          renderOffsetX(0.0f),
          renderOffsetY(0.0f),
          renderWidth(0.0f),
          renderHeight(0.0f),
          hasRenderOffsetX(false),
          hasRenderOffsetY(false),
          hasRenderWidth(false),
          hasRenderHeight(false),
          renderAlignX(kLotModulePlacementAlignStart),
          renderAlignY(kLotModulePlacementAlignStart),
          isPrimary(false),
          required(true),
          affectsSimulation(true),
          claimsFootprint(true) {
    }
};

struct LotAutoLineRule {
    std::string moduleId;
    LotAutoSizeCondition condition;
    std::uint8_t xReference;
    int xOffset;
    std::uint8_t startYReference;
    int startYOffset;
    std::uint8_t endYReference;
    int endYOffset;
    float renderOffsetX;
    float renderOffsetY;
    float renderWidth;
    float renderHeight;
    bool hasRenderOffsetX;
    bool hasRenderOffsetY;
    bool hasRenderWidth;
    bool hasRenderHeight;
    std::uint8_t renderAlignX;
    std::uint8_t renderAlignY;
    bool required;
    bool affectsSimulation;
    bool claimsFootprint;
    std::vector<std::string> primaryModuleIds;
    std::vector<LotModuleAlternative> alternatives;

    LotAutoLineRule()
        : xReference(kLotAutoReferenceLotStart),
          xOffset(0),
          startYReference(kLotAutoReferenceLotStart),
          startYOffset(0),
          endYReference(kLotAutoReferenceLotEnd),
          endYOffset(0),
          renderOffsetX(0.0f),
          renderOffsetY(0.0f),
          renderWidth(0.0f),
          renderHeight(0.0f),
          hasRenderOffsetX(false),
          hasRenderOffsetY(false),
          hasRenderWidth(false),
          hasRenderHeight(false),
          renderAlignX(kLotModulePlacementAlignStart),
          renderAlignY(kLotModulePlacementAlignStart),
          required(true),
          affectsSimulation(true),
          claimsFootprint(true) {
    }
};

struct LotAutoFillRule {
    std::string moduleId;
    LotAutoSizeCondition condition;
    bool affectsSimulation;
    bool claimsFootprint;
    std::vector<std::string> primaryModuleIds;
    std::vector<LotModuleAlternative> alternatives;

    LotAutoFillRule()
        : affectsSimulation(true),
          claimsFootprint(true) {
    }
};

struct LotAutoEdgeRule {
    std::string moduleId;
    std::string sourceModuleId;
    LotAutoSizeCondition condition;
    bool affectsSimulation;
    bool claimsFootprint;
    std::vector<std::string> primaryModuleIds;

    LotAutoEdgeRule()
        : affectsSimulation(false),
          claimsFootprint(false) {
    }
};

struct LotAutoLayout {
    std::vector<LotAutoModuleRule> moduleRules;
    std::vector<LotAutoLineRule> lineRules;
    std::vector<LotAutoFillRule> fillRules;
    std::vector<LotAutoEdgeRule> edgeRules;

    bool empty() const {
        return moduleRules.empty() && lineRules.empty() && fillRules.empty() && edgeRules.empty();
    }
};

struct LotAsset {
    std::string id;
    std::string name;
    std::string densityBand;
    std::string rciTypeId;
    std::uint16_t zoningType;
    Int2 anchor;
    Int2 footprintOrigin;
    int footprintWidth;
    int footprintHeight;
    Int2 renderOrigin;
    LotFootprintCompatibility compatibility;
    std::uint8_t frontDirection;
    bool hasFrontDirection;
    int constructionTicks;
    std::vector<LotModulePlacementDefinition> initialModules;
    LotAutoLayout autoLayout;
    std::vector<LotAccessDefinition> accessDefinitions;

    // Starts an unloaded lot archetype with a zero anchor.
    LotAsset()
        : rciTypeId(),
          zoningType(TileZoningNone),
          anchor(0, 0),
          footprintOrigin(0, 0),
          footprintWidth(0),
          footprintHeight(0),
          renderOrigin(0, 0),
          frontDirection(kRoadDirectionNorth),
          hasFrontDirection(false),
          constructionTicks(0) {
    }
};

class Lot {
public:
    Lot();
    Lot(int lotId, const std::string& assetId, int anchorTileX, int anchorTileY, int rotationSteps = 0);

    int id() const;
    const std::string& assetId() const;
    int anchorTileX() const;
    int anchorTileY() const;
    int rotationSteps() const;
    const std::vector<LotModulePlacement>& modules() const;
    const std::vector<Int2>& occupiedOffsets() const;
    const std::vector<int>& occupiedTileIndices() const;
    const std::vector<CityParameterContribution>& parameterContributions() const;
    const std::vector<CommuteRouteSegment>& commuteRouteSegments() const;
    const std::vector<CommuteRouteRecord>& commuteRoutes() const;
    int commuteDemand() const;
    int commuteSatisfied() const;
    int lowWealthResidentsTotal() const;
    int lowWealthJobsTotal() const;
    int lowWealthJobsFilled() const;
    bool hasMissingRoadAccessComplaint() const;
    bool hasLongCommuteComplaint() const;
    int minimumTileX() const;
    int minimumTileY() const;
    int footprintWidth() const;
    int footprintHeight() const;
    bool isUnderConstruction() const;
    int constructionTotalTicks() const;
    int constructionRemainingTicks() const;
    float constructionProgress() const;

    void setExplicitFootprint(const Int2& localOrigin, int width, int height, int mapWidth);
    int addModule(
        const LotModule& module,
        const Int2& localOrigin,
        int mapWidth,
        int footprintWidth = 0,
        int footprintHeight = 0,
        float renderOffsetX = 0.0f,
        float renderOffsetY = 0.0f,
        float renderWidth = 0.0f,
        float renderHeight = 0.0f,
        bool affectsSimulation = true,
        bool claimsFootprint = true);
    bool removeModule(int moduleInstanceId, int mapWidth);
    void clearModules(int mapWidth);
    void startConstruction(int totalTicks, int mapWidth);
    void setConstructionState(int totalTicks, int remainingTicks, int mapWidth);
    bool advanceConstructionTick(int mapWidth);
    int moduleInstanceIdAtLocalTile(const Int2& localTile) const;
    void rebaseAnchorToMinimumTile(int mapWidth);
    void applyEffects(std::vector<Tile>& tiles) const;
    LotRenderInstance buildRenderInstance() const;
    void buildRenderInstances(std::vector<LotRenderInstance>& instances) const;
    std::string moduleSummary() const;
    std::string parameterSummary(const CityParameterRegistry& registry) const;
    std::string complaintSummary() const;
    void clearCommutes();
    void clearCommuteRoutes();
    void setLowWealthResidentsTotal(int residents);
    void setLowWealthJobsTotal(int jobs);
    void setLowWealthJobsFilled(int jobs);
    void setLowWealthResidentsRoadAccess(bool hasRoadAccess);
    void setLowWealthJobsRoadAccess(bool hasRoadAccess);
    void setCommuteDemand(int demand);
    void addCommuteDemand(int demand);
    void addCommuteRoute(int destinationLotId, int demand, std::uint16_t transportLoad, bool longCommute, bool morningMediumRetry, bool eveningMediumRetry, const TransportPathResult& morningPathResult, const TransportPathResult& eveningPathResult, const std::vector<CommuteRouteSegment>& morningSegments, const std::vector<CommuteRouteSegment>& eveningSegments);
    void addLowWealthJobsFilled(int jobs);
    void flagLongCommute();

private:
    void rebuildCachedState(int mapWidth);
    void lotGroundColor(float& red, float& green, float& blue) const;
    void rebuildCommuteRouteSummary();

    int lotId_;
    std::string assetId_;
    int anchorTileX_;
    int anchorTileY_;
    int rotationSteps_;
    int nextModuleInstanceId_;
    std::vector<LotModulePlacement> modules_;
    std::vector<Int2> explicitFootprintOffsets_;
    std::vector<Int2> occupiedOffsets_;
    std::vector<int> occupiedTileIndices_;
    std::vector<CityParameterContribution> parameterContributions_;
    std::vector<CommuteRouteRecord> commuteRoutes_;
    std::vector<CommuteRouteSegment> commuteRouteSegments_;
    int commuteDemand_;
    int commuteSatisfied_;
    int lowWealthResidentsTotal_;
    int lowWealthJobsTotal_;
    int lowWealthJobsFilled_;
    bool lowWealthResidentsHaveRoadAccess_;
    bool lowWealthJobsHaveRoadAccess_;
    bool hasLongCommuteComplaint_;
    int airPollutionEmit_;
    int parkEffectEmit_;
    int landValueEmit_;
    Int2 minimumOccupiedOffset_;
    Int2 maximumOccupiedOffset_;
    float renderHeight_;
    float colorR_;
    float colorG_;
    float colorB_;
    int constructionTotalTicks_;
    int constructionRemainingTicks_;
};
