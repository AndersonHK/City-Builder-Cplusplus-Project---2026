#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "CityParameters.h"
#include "Lot.h"
#include "LotModule.h"
#include "RoadTemplateDefinition.h"
#include "TransportCostMap.h"

struct RciDensityPoint {
    int population;
    float maxDensityPerTile;

    RciDensityPoint()
        : population(0),
          maxDensityPerTile(0.0f) {
    }
};

struct RciGrowthRule {
    std::uint16_t zoningType;
    int desirabilityThreshold;
    std::vector<RciDensityPoint> densityPoints;

    RciGrowthRule()
        : zoningType(TileZoningNone),
          desirabilityThreshold(0) {
    }
};

struct LoadedGameAssets {
    std::vector<LotModule> modules;
    std::vector<LotAsset> lots;
    std::vector<RciGrowthRule> rciGrowthRules;
    std::vector<float> initialDemands;
    TransportCongestionCurve congestionCurve;
    RoadLaneCapacityConfig roadLaneCapacities;
    int rciConstructorAttemptsPerTick;
    float rciConstructorOverbuildMultiplier;
    int rciBaselineLandValue;

    LoadedGameAssets()
        : rciConstructorAttemptsPerTick(5),
          rciConstructorOverbuildMultiplier(1.2f),
          rciBaselineLandValue(0) {
    }
};

bool LoadGameAssets(const std::string& dataDirectory, const CityParameterRegistry& parameterRegistry, LoadedGameAssets& assets, std::string& errorMessage);
