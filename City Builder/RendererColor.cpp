#include "RendererColor.h"

#include <algorithm>
#include <cmath>

namespace {
const float kPqM1 = 0.1593017578125f;
const float kPqM2 = 78.84375f;
const float kPqC1 = 0.8359375f;
const float kPqC2 = 18.8515625f;
const float kPqC3 = 18.6875f;
const float kPqReferenceWhiteNits = 10000.0f;

float ToneMapChannel(float value) {
    const float clamped = std::max(0.0f, value);
    return clamped / (1.0f + clamped);
}
}

LinearColor::LinearColor()
    : r(0.0f),
      g(0.0f),
      b(0.0f),
      a(1.0f) {
}

LinearColor::LinearColor(float red, float green, float blue, float alpha)
    : r(red),
      g(green),
      b(blue),
      a(alpha) {
}

float RendererClamp01(float value) {
    return std::max(0.0f, std::min(value, 1.0f));
}

float RendererSrgbToLinear(float value) {
    const float clamped = RendererClamp01(value);
    if (clamped <= 0.04045f) {
        return clamped / 12.92f;
    }

    return std::pow((clamped + 0.055f) / 1.055f, 2.4f);
}

float RendererLinearToSrgb(float value) {
    const float clamped = RendererClamp01(value);
    if (clamped <= 0.0031308f) {
        return clamped * 12.92f;
    }

    return (1.055f * std::pow(clamped, 1.0f / 2.4f)) - 0.055f;
}

LinearColor RendererColorFromSrgb(float red, float green, float blue, float alpha) {
    return LinearColor(
        RendererSrgbToLinear(red),
        RendererSrgbToLinear(green),
        RendererSrgbToLinear(blue),
        RendererClamp01(alpha));
}

LinearColor RendererColorToSrgb(const LinearColor& color) {
    return LinearColor(
        RendererLinearToSrgb(color.r),
        RendererLinearToSrgb(color.g),
        RendererLinearToSrgb(color.b),
        RendererClamp01(color.a));
}

LinearColor RendererToneMapSdr(const LinearColor& color) {
    return LinearColor(
        ToneMapChannel(color.r),
        ToneMapChannel(color.g),
        ToneMapChannel(color.b),
        RendererClamp01(color.a));
}

float RendererEncodePqFromNits(float luminanceNits) {
    const float normalized = RendererClamp01(luminanceNits / kPqReferenceWhiteNits);
    const float powered = std::pow(normalized, kPqM1);
    const float numerator = kPqC1 + (kPqC2 * powered);
    const float denominator = 1.0f + (kPqC3 * powered);
    return std::pow(numerator / denominator, kPqM2);
}

RendererOutputMode RendererChooseOutputMode(bool supportsFp16ScRgb, bool supportsHdr10Rgb10A2) {
    if (supportsFp16ScRgb) {
        return RendererOutputMode::HdrScRgbFp16;
    }

    if (supportsHdr10Rgb10A2) {
        return RendererOutputMode::Hdr10Rgb10A2;
    }

    return RendererOutputMode::SdrSrgb8;
}

