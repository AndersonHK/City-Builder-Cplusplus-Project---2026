#shader vertex
#version 460 core

layout(location = 0) in vec3 aLocalPosition;
layout(location = 1) in vec4 aInstanceData0;
layout(location = 2) in vec4 aInstanceData1;
layout(location = 3) in vec4 aInstanceData2;
layout(location = 4) in vec3 aMeshColor;

uniform mat4 uViewProjection;
uniform int uRenderMode;
uniform sampler2D uTileLiftTexture;
uniform sampler2D uRegionPreviewTexture;

out vec2 vTileUv;
out vec2 vLocalUv;
out vec3 vLotColor;
out vec2 vRoadGlyphs;
out vec2 vRoadMasks;
out vec3 vRouteColor;
out vec4 vUiColor;
out float vSurfaceLift;
flat out int vRenderMode;

void main()
{
    vec3 worldPosition;

    if (uRenderMode == 0) {
        float tileLift = texture(uTileLiftTexture, aInstanceData0.zw).r * 0.04;
        worldPosition = vec3(
            aLocalPosition.x + aInstanceData0.x,
            tileLift,
            aLocalPosition.z + aInstanceData0.y);
        vTileUv = aInstanceData0.zw;
        vLocalUv = aLocalPosition.xz;
        vLotColor = vec3(0.0);
        vRoadGlyphs = vec2(0.0);
        vRoadMasks = vec2(0.0);
        vRouteColor = vec3(0.0);
        vUiColor = vec4(0.0);
        vSurfaceLift = tileLift;
    } else if (uRenderMode == 1) {
        vec3 scaledPosition = vec3(
            aLocalPosition.x * aInstanceData0.z,
            aLocalPosition.y * aInstanceData1.x,
            aLocalPosition.z * aInstanceData0.w);
        worldPosition = vec3(aInstanceData0.x, 0.0, aInstanceData0.y) + scaledPosition;
        vTileUv = vec2(0.0);
        vLocalUv = aLocalPosition.xz;
        vLotColor = aInstanceData1.yzw;
        vRoadGlyphs = vec2(0.0);
        vRoadMasks = aInstanceData2.xy;
        vRouteColor = vec3(0.0);
        vUiColor = vec4(0.0);
        vSurfaceLift = 0.0;
    } else if (uRenderMode == 9) {
        vec3 scaledPosition = vec3(
            aLocalPosition.x * aInstanceData0.z,
            aLocalPosition.y * aInstanceData1.x,
            aLocalPosition.z * aInstanceData0.w);
        worldPosition = vec3(aInstanceData0.x, 0.0, aInstanceData0.y) + scaledPosition;
        vTileUv = vec2(0.0);
        vLocalUv = aLocalPosition.xz;
        vLotColor = aInstanceData1.yzw * aMeshColor;
        vRoadGlyphs = vec2(0.0);
        vRoadMasks = aInstanceData2.xy;
        vRouteColor = vec3(0.0);
        vUiColor = vec4(0.0);
        vSurfaceLift = 0.0;
    } else if (uRenderMode == 2) {
        worldPosition = vec3(
            aLocalPosition.x + aInstanceData0.x,
            aInstanceData0.z,
            aLocalPosition.z + aInstanceData0.y);
        vTileUv = vec2(0.0);
        vLocalUv = aLocalPosition.xz;
        vLotColor = vec3(0.0);
        vRoadGlyphs = vec2(aInstanceData0.w, aInstanceData1.x);
        vRoadMasks = vec2(aInstanceData1.y, aInstanceData1.z);
        vRouteColor = vec3(0.0);
        vUiColor = vec4(0.0);
        vSurfaceLift = 0.0;
    } else if (uRenderMode == 4) {
        worldPosition = vec3(
            aLocalPosition.x * aInstanceData0.z + aInstanceData0.x,
            aInstanceData1.z,
            aLocalPosition.z * aInstanceData0.w + aInstanceData0.y);
        vTileUv = vec2(0.0);
        vLocalUv = aLocalPosition.xz;
        vLotColor = vec3(0.0);
        vRoadGlyphs = aInstanceData1.xy;
        vRoadMasks = vec2(aInstanceData1.w, 0.0);
        vRouteColor = aInstanceData2.rgb;
        vUiColor = vec4(0.0);
        vSurfaceLift = 0.0;
    } else if (uRenderMode == 5) {
        worldPosition = vec3(
            aLocalPosition.x * aInstanceData0.z + aInstanceData0.x,
            0.0,
            aLocalPosition.z * aInstanceData0.w + aInstanceData0.y);
        vTileUv = vec2(0.0);
        vLocalUv = vec2(aLocalPosition.x, 1.0 - aLocalPosition.z);
        vLotColor = vec3(0.0);
        vRoadGlyphs = vec2(0.0);
        vRoadMasks = vec2(0.0);
        vRouteColor = vec3(0.0);
        vUiColor = vec4(0.0);
        vSurfaceLift = 0.0;
    } else if (uRenderMode == 6) {
        worldPosition = vec3(
            aLocalPosition.x * aInstanceData0.z + aInstanceData0.x,
            aLocalPosition.z * aInstanceData0.w + aInstanceData0.y,
            0.0);
        vTileUv = vec2(0.0);
        vLocalUv = aLocalPosition.xz;
        vLotColor = vec3(0.0);
        vRoadGlyphs = vec2(0.0);
        vRoadMasks = vec2(0.0);
        vRouteColor = vec3(0.0);
        vUiColor = aInstanceData1;
        vSurfaceLift = 0.0;
    } else if (uRenderMode == 7 || uRenderMode == 8) {
        worldPosition = vec3(
            aLocalPosition.x * aInstanceData0.z + aInstanceData0.x,
            0.075,
            aLocalPosition.z * aInstanceData0.w + aInstanceData0.y);
        vTileUv = vec2(0.0);
        vLocalUv = aLocalPosition.xz;
        vLotColor = vec3(0.0);
        vRoadGlyphs = vec2(0.0);
        vRoadMasks = vec2(0.0);
        vRouteColor = vec3(0.0);
        vUiColor = aInstanceData1;
        vSurfaceLift = 0.0;
    } else {
        worldPosition = vec3(
            aLocalPosition.x + aInstanceData0.x,
            0.0,
            aLocalPosition.z + aInstanceData0.y);
        vTileUv = aInstanceData0.zw;
        vLocalUv = aLocalPosition.xz;
        vLotColor = vec3(0.0);
        vRoadGlyphs = vec2(0.0);
        vRoadMasks = vec2(0.0);
        vRouteColor = vec3(0.0);
        vUiColor = vec4(0.0);
        vSurfaceLift = 0.0;
    }

    vRenderMode = uRenderMode;
    gl_Position = uViewProjection * vec4(worldPosition, 1.0);
}

#shader fragment
#version 460 core

layout(location = 0) out vec4 color;

uniform sampler2D uTileStateTexture;
uniform sampler2D uGroundRoadStateTexture;
uniform sampler2D uTileOverlayTexture;
uniform sampler2D uRoadBaseAtlasTexture;
uniform sampler2D uRoadArrowAtlasTexture;
uniform sampler2D uRegionPreviewTexture;
uniform vec2 uRoadAtlasGrid;
uniform int uRoadDebugVisible;
uniform int uZoningOverlayVisible;
uniform float uRoadAlphaScale;
uniform vec3 uRoadTintColor;
uniform float uRoadTintStrength;
uniform float uLotAlphaScale;
uniform vec3 uLotTintColor;
uniform float uLotTintStrength;
uniform int uTileOverlaySemanticMode;
uniform int uTileOverlayGradientDirection;

in vec2 vTileUv;
in vec2 vLocalUv;
in vec3 vLotColor;
in vec2 vRoadGlyphs;
in vec2 vRoadMasks;
in vec3 vRouteColor;
in vec4 vUiColor;
in float vSurfaceLift;
flat in int vRenderMode;

vec4 sampleRoadAtlas(sampler2D atlasTexture, float glyphIndex, vec2 localUv)
{
    if (glyphIndex < 0.5) {
        return vec4(0.0);
    }

    float glyph = floor(glyphIndex + 0.5);
    vec2 atlasSize = vec2(textureSize(atlasTexture, 0));
    vec2 cellSize = atlasSize / uRoadAtlasGrid;
    vec2 glyphCell = vec2(mod(glyph, uRoadAtlasGrid.x), floor(glyph / uRoadAtlasGrid.x));
    vec2 localPixel = clamp(localUv, vec2(0.0), vec2(1.0)) * (cellSize - vec2(1.0)) + vec2(0.5);
    vec2 atlasUv = vec2(
        (glyphCell.x * cellSize.x + localPixel.x) / atlasSize.x,
        (glyphCell.y * cellSize.y + localPixel.y) / atlasSize.y);
    return texture(atlasTexture, atlasUv);
}

const int kOverlayScalarPayloadBitDepth = 16;
const int kTileOverlaySemanticTrafficCapacity = 0;
const int kTileOverlaySemanticLandValue = 1;
const int kTileOverlaySemanticRciDesirability = 2;
const int kTileOverlaySemanticAirPollution = 3;
const int kTileOverlaySemanticParkEffect = 4;
const int kOverlayGradientGoodToBad = 0;
const int kOverlayGradientBadToGood = 1;
const uint kOverlayScalarPayloadMaxValue = (1u << kOverlayScalarPayloadBitDepth) - 1u;
const uint kTrafficOverlayRelevantMask = 1u << (kOverlayScalarPayloadBitDepth - 1);
const uint kTrafficOverlayUtilizationMask = kTrafficOverlayRelevantMask - 1u;
const float kAuthoredColorByteMax = 255.0;

uint overlayPayload(vec2 uv)
{
    return uint(floor(texture(uTileOverlayTexture, uv).r * float(kOverlayScalarPayloadMaxValue) + 0.5));
}

vec4 zoningOverlayColor(uint payload)
{
    if (payload == 1u) {
        return vec4(26.0 / kAuthoredColorByteMax, 122.0 / kAuthoredColorByteMax, 51.0 / kAuthoredColorByteMax, 96.0 / kAuthoredColorByteMax);
    }
    if (payload == 2u) {
        return vec4(238.0 / kAuthoredColorByteMax, 211.0 / kAuthoredColorByteMax, 58.0 / kAuthoredColorByteMax, 104.0 / kAuthoredColorByteMax);
    }
    if (payload == 3u) {
        return vec4(112.0 / kAuthoredColorByteMax, 235.0 / kAuthoredColorByteMax, 117.0 / kAuthoredColorByteMax, 96.0 / kAuthoredColorByteMax);
    }

    return vec4(0.0);
}

vec4 goodToBadRampOverlayColor(float normalizedBadness, float alpha)
{
    float red = normalizedBadness <= 0.5 ? normalizedBadness * 2.0 : 1.0;
    float green = normalizedBadness <= 0.5 ? 1.0 : (1.0 - normalizedBadness) * 2.0;
    return vec4(red, green, 0.0, alpha);
}

vec4 rampOverlayColor(float normalized, float alpha)
{
    float normalizedBadness = uTileOverlayGradientDirection == kOverlayGradientBadToGood ? 1.0 - normalized : normalized;
    return goodToBadRampOverlayColor(normalizedBadness, alpha);
}

vec4 tileOverlayColor(uint payload)
{
    if (uTileOverlaySemanticMode == kTileOverlaySemanticTrafficCapacity) {
        if ((payload & kTrafficOverlayRelevantMask) == 0u) {
            return vec4(0.0);
        }

        float utilization = float(payload & kTrafficOverlayUtilizationMask) / float(kTrafficOverlayUtilizationMask);
        return rampOverlayColor(utilization, 89.0 / kAuthoredColorByteMax);
    }

    float normalized = clamp(float(payload) / float(kOverlayScalarPayloadMaxValue), 0.0, 1.0);
    if (uTileOverlaySemanticMode == kTileOverlaySemanticLandValue) {
        return rampOverlayColor(normalized, 89.0 / kAuthoredColorByteMax);
    }

    if (uTileOverlaySemanticMode == kTileOverlaySemanticRciDesirability) {
        return rampOverlayColor(normalized, 128.0 / kAuthoredColorByteMax);
    }

    if (uTileOverlaySemanticMode == kTileOverlaySemanticAirPollution) {
        return rampOverlayColor(normalized, 101.0 / kAuthoredColorByteMax);
    }

    if (uTileOverlaySemanticMode == kTileOverlaySemanticParkEffect) {
        return rampOverlayColor(normalized, 101.0 / kAuthoredColorByteMax);
    }

    return vec4(0.0);
}

float visibleRoadArrowGlyph(float packedGlyph)
{
    float glyph = floor(packedGlyph + 0.5);
    bool debugArrow = glyph >= 128.0;
    if (debugArrow && uRoadDebugVisible == 0) {
        return 0.0;
    }
    if (debugArrow) {
        glyph -= 128.0;
    }
    return glyph;
}

float roadTileGlyphIndex(float baseGlyph, float laneGraphicMask, float dividerMask)
{
    int baseGlyphIndex = int(floor(baseGlyph + 0.5));
    float materialOffset = baseGlyphIndex >= 17 ? 2048.0 : 0.0;
    int laneGraphics = int(floor(laneGraphicMask + 0.5));
    int sidewalkEdges = laneGraphics & 15;
    int crosswalkEdges = (laneGraphics >> 4) & 15;
    if (crosswalkEdges != 0) {
        return materialOffset + 768.0 + float(laneGraphics);
    }

    if (sidewalkEdges != 0) {
        int divider = int(floor(dividerMask + 0.5));
        int whiteMask = divider & 15;
        int yellowMask = (divider >> 4) & 15;
        int medianMask = whiteMask & yellowMask;
        if (medianMask != 0) {
            return materialOffset + float(1024 + sidewalkEdges * 16 + medianMask);
        }
        if (yellowMask != 0) {
            return materialOffset + float(256 + sidewalkEdges * 16 + yellowMask);
        }
        if (whiteMask != 0) {
            return materialOffset + float(512 + sidewalkEdges * 16 + whiteMask);
        }
        return materialOffset + float(64 + sidewalkEdges);
    }
    return materialOffset + baseGlyph;
}

void main()
{
    if (vRenderMode == 0) {
        vec2 tileState = clamp(texture(uTileStateTexture, vTileUv).rg, vec2(0.0), vec2(1.0));
        float airPollution = tileState.r;
        float parkEffect = tileState.g;
        vec3 neutralGrass = vec3(0.19, 0.29, 0.17);
        vec3 healthyGrass = vec3(0.43, 0.64, 0.31);
        vec3 pollutedGrass = vec3(0.48, 0.39, 0.18);
        vec3 finalColor = mix(neutralGrass, healthyGrass, clamp(parkEffect * (1.0 - airPollution * 0.65), 0.0, 1.0));
        finalColor = mix(finalColor, pollutedGrass, clamp(airPollution * (1.0 - parkEffect * 0.45), 0.0, 1.0));
        finalColor += vec3(vSurfaceLift * 0.08);
        if (uZoningOverlayVisible != 0) {
            vec4 zoningColor = zoningOverlayColor(overlayPayload(vTileUv));
            finalColor = mix(finalColor, zoningColor.rgb, zoningColor.a);
        }

        vec4 packedRoadState = floor(texture(uGroundRoadStateTexture, vTileUv).rgba * 255.0 + 0.5);
        vec4 roadBase = sampleRoadAtlas(uRoadBaseAtlasTexture, roadTileGlyphIndex(packedRoadState.x, packedRoadState.z, packedRoadState.w), vLocalUv);
        vec4 roadArrow = sampleRoadAtlas(uRoadArrowAtlasTexture, visibleRoadArrowGlyph(packedRoadState.y), vLocalUv);
        finalColor = mix(finalColor, roadBase.rgb, roadBase.a);
        finalColor = mix(finalColor, roadArrow.rgb, roadArrow.a);
        color = vec4(finalColor, 1.0);
        return;
    }

    if (vRenderMode == 2) {
        vec4 roadBase = sampleRoadAtlas(uRoadBaseAtlasTexture, roadTileGlyphIndex(vRoadGlyphs.x, vRoadMasks.x, vRoadMasks.y), vLocalUv);
        vec4 roadArrow = sampleRoadAtlas(uRoadArrowAtlasTexture, visibleRoadArrowGlyph(vRoadGlyphs.y), vLocalUv);
        vec3 finalColor = roadBase.rgb;
        finalColor = mix(finalColor, roadArrow.rgb, roadArrow.a);
        finalColor = mix(finalColor, uRoadTintColor, clamp(uRoadTintStrength, 0.0, 1.0));
        float finalAlpha = max(roadBase.a, roadArrow.a) * clamp(uRoadAlphaScale, 0.0, 1.0);
        if (finalAlpha <= 0.001) {
            discard;
        }

        color = vec4(finalColor, finalAlpha);
        return;
    }

    if (vRenderMode == 1 || vRenderMode == 9) {
        vec3 finalColor = mix(vLotColor, uLotTintColor, clamp(uLotTintStrength, 0.0, 1.0));
        int surfacePattern = int(floor(vRoadMasks.x + 0.5));
        int surfaceDirection = int(floor(vRoadMasks.y + 0.5));
        if (surfacePattern == 1) {
            bool onPath = false;
            if (surfaceDirection == 1) {
                onPath = abs(vLocalUv.x - 0.5) < 0.085 && vLocalUv.y < 0.64;
            } else if (surfaceDirection == 2) {
                onPath = abs(vLocalUv.y - 0.5) < 0.085 && vLocalUv.x > 0.36;
            } else if (surfaceDirection == 4) {
                onPath = abs(vLocalUv.x - 0.5) < 0.085 && vLocalUv.y > 0.36;
            } else if (surfaceDirection == 8) {
                onPath = abs(vLocalUv.y - 0.5) < 0.085 && vLocalUv.x < 0.64;
            }
            if (onPath) {
                finalColor = mix(finalColor, vec3(0.47, 0.47, 0.45), 0.88);
            }
        }
        color = vec4(finalColor, clamp(uLotAlphaScale, 0.0, 1.0));
        return;
    }

    if (vRenderMode == 3) {
        vec4 overlayColor = tileOverlayColor(overlayPayload(vTileUv));
        if (overlayColor.a <= 0.001) {
            discard;
        }

        color = overlayColor;
        return;
    }

    if (vRenderMode == 4) {
        vec2 direction = vRoadGlyphs;
        float horizontal = step(abs(direction.y), abs(direction.x));
        float alongHorizontal = direction.x >= 0.0 ? vLocalUv.x : 1.0 - vLocalUv.x;
        float alongVertical = direction.y >= 0.0 ? vLocalUv.y : 1.0 - vLocalUv.y;
        float along = mix(alongVertical, alongHorizontal, horizontal);
        float across = mix(abs(vLocalUv.x - 0.5), abs(vLocalUv.y - 0.5), horizontal);
        float shaft = smoothstep(0.085, 0.055, across) * step(0.08, along) * step(along, 0.78);
        float headWidth = mix(0.18, 0.045, clamp((along - 0.78) / 0.18, 0.0, 1.0));
        float head = smoothstep(headWidth + 0.025, headWidth, across) * step(0.76, along) * step(along, 0.96);
        float alpha = max(shaft, head) * clamp(vRoadMasks.x, 0.0, 1.0);
        if (alpha <= 0.001) {
            discard;
        }

        color = vec4(vRouteColor, alpha);
        return;
    }

    if (vRenderMode == 5) {
        color = texture(uRegionPreviewTexture, vLocalUv);
        return;
    }

    if (vRenderMode == 6) {
        color = vUiColor;
        return;
    }

    if (vRenderMode == 7) {
        color = vUiColor;
        return;
    }

    if (vRenderMode == 8) {
        float edge = min(min(vLocalUv.x, 1.0 - vLocalUv.x), min(vLocalUv.y, 1.0 - vLocalUv.y));
        float border = smoothstep(0.065, 0.025, edge);
        vec3 lineColor = max(vUiColor.rgb * 0.36, vec3(0.015));
        vec3 fillColor = vUiColor.rgb;
        float alpha = max(vUiColor.a * 0.24, border * min(0.82, vUiColor.a + 0.26));
        color = vec4(mix(fillColor, lineColor, border), alpha);
        return;
    }

    color = vec4(vLotColor, 1.0);
}
