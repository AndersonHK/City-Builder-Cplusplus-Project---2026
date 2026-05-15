#pragma once

#include <string>
#include <vector>

#include "CityParameters.h"

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

struct LotModule {
    std::string id;
    int width;
    int height;
    int airPollutionEmit;
    int landValueEmit;
    float renderHeight;
    float colorR;
    float colorG;
    float colorB;
    std::vector<CityParameterContribution> parameterContributions;

    // Starts an unloaded module archetype with neutral render/effect values.
    LotModule()
        : width(1),
          height(1),
          airPollutionEmit(0),
          landValueEmit(0),
          renderHeight(0.5f),
          colorR(0.4f),
          colorG(0.4f),
          colorB(0.4f) {
    }
};

struct LotModulePlacementDefinition {
    std::string moduleId;
    Int2 localOrigin;

    LotModulePlacementDefinition()
        : localOrigin(0, 0) {
    }
};

struct LotModulePlacement {
    int instanceId;
    const LotModule* module;
    Int2 localOrigin;

    // Starts an unbound module placement until a lot attaches an archetype.
    LotModulePlacement()
        : instanceId(0),
          module(0),
          localOrigin(0, 0) {
    }
};
