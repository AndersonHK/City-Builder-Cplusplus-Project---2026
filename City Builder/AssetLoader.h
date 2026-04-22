#pragma once

#include <string>
#include <vector>

#include "Lot.h"
#include "LotModule.h"

struct LoadedGameAssets {
    std::vector<LotModule> modules;
    std::vector<LotAsset> lots;
};

bool LoadGameAssets(const std::string& dataDirectory, LoadedGameAssets& assets, std::string& errorMessage);
