#pragma once

#include <cstdint>

const std::uint16_t TileZoningNone = 0;
const std::uint16_t TileZoningResidential = 1;
const std::uint16_t TileZoningIndustrial = 2;
const std::uint16_t TileZoningResidentialLow = 3;
const std::uint16_t TileZoningResidentialHigh = TileZoningResidential;

const int kLandValueDisplayMinimum = 0;
const int kLandValueDisplayCap = 160000;

struct Tile {
    int landValue;
    int airPollution;
    int parkEffect;
    bool isVacant;
    std::uint16_t zoningType;

    // Seeds a vacant tile with neutral early-prototype statistics.
    Tile()
        : landValue(160000),
          airPollution(0),
          parkEffect(0),
          isVacant(true),
          zoningType(0) {
    }
};
