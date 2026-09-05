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

enum class RciDesirabilityField {
    AirPollution,
    ParkEffect
};

struct RciDesirabilityPoint {
    float value;
    int desirabilityDelta;

    RciDesirabilityPoint()
        : value(0.0f),
          desirabilityDelta(0) {
    }
};

struct RciDesirabilitySensitivity {
    RciDesirabilityField field;
    int normalizer;
    std::vector<RciDesirabilityPoint> points;

    RciDesirabilitySensitivity()
        : field(RciDesirabilityField::AirPollution),
          normalizer(kLandValueDisplayCap) {
    }
};

struct RciDesirabilityRule {
    std::string rciTypeId;
    std::uint16_t zoningType;
    int baseline;
    std::vector<RciDesirabilitySensitivity> sensitivities;

    RciDesirabilityRule()
        : rciTypeId(),
          zoningType(TileZoningNone),
          baseline(60) {
    }
};

struct LoadedGameAssets {
    std::vector<LotModule> modules;
    std::vector<LotAsset> lots;
    std::vector<RenderMeshBinding> renderMeshBindings;
    std::vector<RciGrowthRule> rciGrowthRules;
    std::vector<RciDesirabilityRule> rciDesirabilityRules;
    std::vector<float> initialDemands;
    TransportCongestionCurve congestionCurve;
    RoadLaneCapacityConfig roadLaneCapacities;
    std::vector<std::string> invalidLotReports;
    int rciConstructorAttemptsPerTick;
    float rciConstructorOverbuildMultiplier;
    float rciConstructorMergeCapacityDiscount;
    float rciConstructorRedevelopmentCapacityIncrease;
    int rciBaselineLandValue;

    LoadedGameAssets()
        : rciConstructorAttemptsPerTick(5),
          rciConstructorOverbuildMultiplier(1.2f),
          rciConstructorMergeCapacityDiscount(0.2f),
          rciConstructorRedevelopmentCapacityIncrease(0.2f),
          rciBaselineLandValue(0) {
    }
};

bool LoadGameAssets(const std::string& dataDirectory, const CityParameterRegistry& parameterRegistry, LoadedGameAssets& assets, std::string& errorMessage);
