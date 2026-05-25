#pragma once

enum class RendererOutputMode {
    HdrScRgbFp16,
    Hdr10Rgb10A2,
    SdrSrgb8
};

// Scene-linear RGBA used by renderer-facing presentation colors. Values may
// exceed 1.0 until the final presentation pass clamps or encodes for hardware.
struct LinearColor {
    float r;
    float g;
    float b;
    float a;

    LinearColor();
    LinearColor(float red, float green, float blue, float alpha);
};

typedef LinearColor HdrColor;

// Clamps a single channel to the display/output-normalized 0..1 range.
float RendererClamp01(float value);

// Converts authored sRGB channel values into the renderer's scene-linear space.
float RendererSrgbToLinear(float value);

// Converts scene-linear channel values back to sRGB for SDR presentation.
float RendererLinearToSrgb(float value);

// Builds a scene-linear color from authored sRGB RGB plus linear alpha.
LinearColor RendererColorFromSrgb(float red, float green, float blue, float alpha);

// Converts scene-linear RGB to sRGB while preserving clamped alpha.
LinearColor RendererColorToSrgb(const LinearColor& color);

// Applies the current SDR compression curve before sRGB output encoding.
LinearColor RendererToneMapSdr(const LinearColor& color);

// Encodes an absolute luminance value into HDR10 PQ normalized code space.
float RendererEncodePqFromNits(float luminanceNits);

// Chooses the preferred presentation mode without hiding HDR downgrade policy.
RendererOutputMode RendererChooseOutputMode(bool supportsFp16ScRgb, bool supportsHdr10Rgb10A2);
