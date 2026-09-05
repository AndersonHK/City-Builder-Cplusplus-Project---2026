#pragma once

#include <cstdint>

constexpr std::uint16_t TileZoningNone = 0;
constexpr std::uint16_t TileZoningResidential = 1;
constexpr std::uint16_t TileZoningIndustrial = 2;
constexpr std::uint16_t TileZoningResidentialLow = 3;
constexpr std::uint16_t TileZoningResidentialHigh = TileZoningResidential;

// Display caps define how simulation stats map into renderer scalar payloads.
// Change these constants instead of reintroducing byte-scale magic numbers.
constexpr int kSimulationStatDisplayMinimum = 0;
constexpr int kSimulationStatDisplayCap = 160000;
constexpr int kLandValueDisplayMinimum = kSimulationStatDisplayMinimum;
constexpr int kLandValueDisplayCap = kSimulationStatDisplayCap;
constexpr int kRciDesirabilityDisplayMinimum = 0;
constexpr int kRciDesirabilityDisplayCap = 100;

struct Tile {
    int landValue;
    int airPollution;
    int parkEffect;
    bool isVacant;
    std::uint16_t zoningType;

    // Seeds a vacant tile with neutral early-prototype statistics.
    Tile()
        : landValue(kLandValueDisplayCap),
          airPollution(0),
          parkEffect(0),
          isVacant(true),
          zoningType(0) {
    }
};
