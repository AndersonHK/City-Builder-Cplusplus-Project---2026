#pragma once

#include <cstdint>

struct Tile {
    int landValue;
    int airPollution;
    bool isVacant;
    std::uint16_t zoningType;

    Tile()
        : landValue(160000),
          airPollution(0),
          isVacant(true),
          zoningType(0) {
    }
};
