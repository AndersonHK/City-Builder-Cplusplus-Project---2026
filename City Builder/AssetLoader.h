#pragma once

#include <string>
#include <vector>

#include "CityParameters.h"
#include "Lot.h"
#include "LotModule.h"

struct LoadedGameAssets {
    std::vector<LotModule> modules;
    std::vector<LotAsset> lots;
};

bool LoadGameAssets(const std::string& dataDirectory, const CityParameterRegistry& parameterRegistry, LoadedGameAssets& assets, std::string& errorMessage);
