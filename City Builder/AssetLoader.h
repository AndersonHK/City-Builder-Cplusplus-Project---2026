#pragma once

#include <string>
#include <vector>

#include "CityParameters.h"
#include "Lot.h"
#include "LotModule.h"
#include "TransportCostMap.h"

struct LoadedGameAssets {
    std::vector<LotModule> modules;
    std::vector<LotAsset> lots;
    TransportCongestionCurve congestionCurve;
};

bool LoadGameAssets(const std::string& dataDirectory, const CityParameterRegistry& parameterRegistry, LoadedGameAssets& assets, std::string& errorMessage);
