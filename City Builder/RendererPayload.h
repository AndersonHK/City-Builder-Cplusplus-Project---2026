#pragma once

#include <cstdint>
#include <limits>

using RendererScalarPayload = std::uint16_t;
using RendererSignedScalarPayload = std::int16_t;

enum class RendererOverlaySemantic : std::uint8_t {
    TrafficCapacity = 0,
    LandValue = 1,
    RciDesirability = 2,
    AirPollution = 3
};

// Overlay gradients are semantic. Red means bad, green means good; this enum
// tells the shader whether a rising payload moves from good to bad or bad to good.
enum class RendererOverlayGradientDirection : std::uint8_t {
    GoodToBad = 0,
    BadToGood = 1
};

// Renderer scalar payloads are fixed-point data, not colors. Shaders expand
// these semantic values into ramps or typed indices after upload.
constexpr int kRendererScalarPayloadBitDepth = 16;
constexpr std::uint32_t kRendererScalarPayloadMaxValue = (1u << kRendererScalarPayloadBitDepth) - 1u;

// Signed tile-state channels use the same storage width for normalized stat
// fields that can be positive or negative, such as debug pollution deltas.
constexpr int kRendererSignedScalarPayloadBitDepth = 16;
constexpr std::int32_t kRendererSignedScalarPayloadMaxValue = (1 << (kRendererSignedScalarPayloadBitDepth - 1)) - 1;
constexpr std::int32_t kRendererSignedScalarPayloadMinValue = -(1 << (kRendererSignedScalarPayloadBitDepth - 1));

// Traffic overlays reserve one high bit for "this tile has traffic data" and
// use the remaining bits as utilization. Empty tiles therefore stay invisible
// without needing a second mask texture.
constexpr int kRendererTrafficOverlayRelevantBitCount = 1;
constexpr int kRendererTrafficOverlayUtilizationBitDepth = kRendererScalarPayloadBitDepth - kRendererTrafficOverlayRelevantBitCount;
constexpr std::uint32_t kRendererTrafficOverlayRelevantMask = 1u << kRendererTrafficOverlayUtilizationBitDepth;
constexpr std::uint32_t kRendererTrafficOverlayUtilizationMask = kRendererTrafficOverlayRelevantMask - 1u;

static_assert(kRendererScalarPayloadBitDepth > kRendererTrafficOverlayRelevantBitCount, "Traffic overlay needs at least one relevance bit and one utilization bit.");
static_assert(kRendererScalarPayloadMaxValue <= std::numeric_limits<RendererScalarPayload>::max(), "RendererScalarPayload storage must match the configured scalar payload bit depth.");
static_assert(kRendererSignedScalarPayloadMaxValue <= std::numeric_limits<RendererSignedScalarPayload>::max(), "RendererSignedScalarPayload storage must match the configured signed scalar payload bit depth.");
static_assert(kRendererSignedScalarPayloadMinValue >= std::numeric_limits<RendererSignedScalarPayload>::min(), "RendererSignedScalarPayload storage must match the configured signed scalar payload bit depth.");
static_assert(kRendererTrafficOverlayRelevantMask <= kRendererScalarPayloadMaxValue, "Traffic overlay relevance mask must fit in the scalar payload.");

constexpr int RendererOverlaySemanticIndex(RendererOverlaySemantic semantic) noexcept {
    return static_cast<int>(semantic);
}

constexpr int RendererOverlayGradientDirectionIndex(RendererOverlayGradientDirection direction) noexcept {
    return static_cast<int>(direction);
}

constexpr RendererOverlayGradientDirection RendererOverlayGradientDirectionForSemantic(RendererOverlaySemantic semantic) noexcept {
    switch (semantic) {
    case RendererOverlaySemantic::LandValue:
    case RendererOverlaySemantic::RciDesirability:
        return RendererOverlayGradientDirection::BadToGood;
    case RendererOverlaySemantic::TrafficCapacity:
    case RendererOverlaySemantic::AirPollution:
    default:
        return RendererOverlayGradientDirection::GoodToBad;
    }
}

static_assert(RendererOverlaySemanticIndex(RendererOverlaySemantic::TrafficCapacity) == 0, "Traffic overlay semantic index must match the shader.");
static_assert(RendererOverlaySemanticIndex(RendererOverlaySemantic::LandValue) == 1, "Land-value overlay semantic index must match the shader.");
static_assert(RendererOverlaySemanticIndex(RendererOverlaySemantic::RciDesirability) == 2, "RCI desirability overlay semantic index must match the shader.");
static_assert(RendererOverlayGradientDirectionIndex(RendererOverlayGradientDirection::GoodToBad) == 0, "Good-to-bad overlay direction must match the shader.");
static_assert(RendererOverlayGradientDirectionIndex(RendererOverlayGradientDirection::BadToGood) == 1, "Bad-to-good overlay direction must match the shader.");

// Packs numerator / denominator into the configured unsigned fixed-point range.
// A numerator at or above the denominator maps to full scale.
inline RendererScalarPayload RendererPackRatioToScalarPayload(std::uint64_t numerator, std::uint64_t denominator) {
    if (denominator == 0u || numerator == 0u) {
        return 0u;
    }

    if (numerator >= denominator) {
        return static_cast<RendererScalarPayload>(kRendererScalarPayloadMaxValue);
    }

    return static_cast<RendererScalarPayload>(((numerator * kRendererScalarPayloadMaxValue) + (denominator / 2u)) / denominator);
}

// Packs a stat against its semantic cap; this is where values like land value
// enter the renderer gradient contract without preserving old byte-scale logic.
inline RendererScalarPayload RendererPackCappedStatToScalarPayload(int value, int cap) {
    if (value <= 0 || cap <= 0) {
        return 0u;
    }

    return RendererPackRatioToScalarPayload(static_cast<std::uint64_t>(value), static_cast<std::uint64_t>(cap));
}

// Traffic utilization intentionally packs into one fewer bit than general
// scalar payloads because the high bit carries relevance.
inline RendererScalarPayload RendererPackRatioToTrafficUtilizationPayload(std::uint64_t numerator, std::uint64_t denominator) {
    if (denominator == 0u || numerator == 0u) {
        return 0u;
    }

    if (numerator >= denominator) {
        return static_cast<RendererScalarPayload>(kRendererTrafficOverlayUtilizationMask);
    }

    return static_cast<RendererScalarPayload>(((numerator * kRendererTrafficOverlayUtilizationMask) + (denominator / 2u)) / denominator);
}

// Combines the explicit relevance bit with an already-packed utilization value.
inline RendererScalarPayload RendererPackTrafficOverlayPayloadFromUtilization(bool relevant, RendererScalarPayload packedUtilization) {
    if (!relevant) {
        return 0u;
    }

    return static_cast<RendererScalarPayload>(kRendererTrafficOverlayRelevantMask | (packedUtilization & kRendererTrafficOverlayUtilizationMask));
}

// Packs a road load/capacity pair into the single traffic overlay texture value.
inline RendererScalarPayload RendererPackTrafficOverlayPayload(bool relevant, std::uint64_t load, std::uint64_t capacity) {
    return RendererPackTrafficOverlayPayloadFromUtilization(relevant, RendererPackRatioToTrafficUtilizationPayload(load, capacity));
}

// Returns whether the traffic payload should draw at all.
inline bool RendererTrafficOverlayPayloadIsRelevant(RendererScalarPayload payload) {
    return (payload & kRendererTrafficOverlayRelevantMask) != 0u;
}

// Extracts the utilization field without converting it to a presentation color.
inline RendererScalarPayload RendererTrafficOverlayPayloadUtilizationValue(RendererScalarPayload payload) {
    return static_cast<RendererScalarPayload>(payload & kRendererTrafficOverlayUtilizationMask);
}
