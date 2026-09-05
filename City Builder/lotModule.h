#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CityParameters.h"
#include "AssetVariation.h"

struct Int2 {
    int x;
    int y;

    // Defaults to the origin tile.
    Int2()
        : x(0),
          y(0) {
    }

    // Stores an explicit tile-space coordinate.
    Int2(int xValue, int yValue)
        : x(xValue),
          y(yValue) {
    }
};

inline bool operator==(const Int2& left, const Int2& right) {
    return left.x == right.x && left.y == right.y;
}

struct RenderMeshBinding {
    std::uint16_t handle;
    std::string key;

    RenderMeshBinding()
        : handle(0u),
          key("box") {
    }
};

struct LotModuleAlternative {
    std::string moduleId;
    int weight;

    LotModuleAlternative()
        : weight(1) {
    }
};

enum LotModulePlacementAlignment {
    kLotModulePlacementAlignStart = 0,
    kLotModulePlacementAlignCenter = 1,
    kLotModulePlacementAlignEnd = 2
};

struct LotModule;

struct LotModulePropDefinition {
    std::string moduleId;
    const LotModule* module;
    Int2 localOrigin;
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
    std::vector<LotModuleAlternative> alternatives;
    std::vector<const LotModule*> alternativeModules;

    LotModulePropDefinition()
        : module(0),
          localOrigin(0, 0),
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
          renderAlignY(kLotModulePlacementAlignStart) {
    }
};

struct LotPathBlocker {float xMeters=0,zMeters=0,widthMeters=0,depthMeters=0;};

struct LotModule {
    std::string id;
    std::string density;
    int width;
    int height;
    int airPollutionEmit;
    int parkEffectEmit;
    int landValueEmit;
    float renderHeight;
    float colorR;
    float colorG;
    float colorB;
    std::string renderMeshKey;
    std::uint16_t renderMeshHandle;
    std::string artFamily;
    std::vector<AssetVariation> variations;
    std::shared_ptr<const AssetVariationBindings> variationBindings;
    std::uint16_t parkingCrossingHandle=0,parkingAisleHandle=0,parkingStallsHandle=0,parkingIslandHandle=0,industrialYardHandle=0,concreteHandle=0;
    bool hasPedestrianEntrance = false;
    bool pathPassable = false;
    bool optionalLandscape = false;
    std::vector<LotPathBlocker> pathBlockers;
    bool hasGarageEntrance = false;
    std::uint16_t pathMeshHandle=0, driveMeshHandle=0, grassMeshHandle=0, gardenMeshHandle=0, treeMeshHandle=0,fenceMeshHandle=0;
    std::uint16_t accessPathHandle=0, accessDriveHandle=0, driveMidHandle=0,driveCapLeftHandle=0,driveCapRightHandle=0,driveCapBothHandle=0;
    bool metricGeometry = false;
    bool stretchGeometry = false;
    float naturalWidth = 0.0f;
    float naturalDepth = 0.0f;
    std::vector<CityParameterContribution> parameterContributions;
    std::vector<LotModulePropDefinition> props;

    // Starts an unloaded module archetype with neutral render/effect values.
    LotModule()
        : density(),
          width(1),
          height(1),
          airPollutionEmit(0),
          parkEffectEmit(0),
          landValueEmit(0),
          renderHeight(0.5f),
          colorR(0.4f),
          colorG(0.4f),
          colorB(0.4f),
          renderMeshKey("box"),
          renderMeshHandle(0u) {
    }
};

struct LotModulePlacementDefinition {
    std::string moduleId;
    Int2 localOrigin;
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
    bool affectsSimulation;
    bool claimsFootprint;
    std::vector<LotModuleAlternative> alternatives;

    LotModulePlacementDefinition()
        : localOrigin(0, 0),
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
          affectsSimulation(true),
          claimsFootprint(true) {
    }
};

struct LotModulePlacement {
    int instanceId;
    const LotModule* module;
    Int2 localOrigin;
    int footprintWidth;
    int footprintHeight;
    float renderOffsetX;
    float renderOffsetY;
    float renderWidth;
    float renderHeight;
    bool affectsSimulation;
    bool claimsFootprint;

    // Starts an unbound module placement until a lot attaches an archetype.
    LotModulePlacement()
        : instanceId(0),
          module(0),
          localOrigin(0, 0),
          footprintWidth(1),
          footprintHeight(1),
          renderOffsetX(0.0f),
          renderOffsetY(0.0f),
          renderWidth(1.0f),
          renderHeight(1.0f),
          affectsSimulation(true),
          claimsFootprint(true) {
    }
};
