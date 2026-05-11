#shader vertex
#version 460 core

layout(location = 0) in vec3 aLocalPosition;
layout(location = 1) in vec4 aInstanceData0;
layout(location = 2) in vec4 aInstanceData1;

uniform mat4 uViewProjection;
uniform int uRenderMode;
uniform sampler2D uTileLiftTexture;

out vec2 vTileUv;
out vec2 vLocalUv;
out vec3 vLotColor;
out vec2 vRoadGlyphs;
out vec2 vRoadMasks;
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
        vRoadMasks = vec2(0.0);
        vSurfaceLift = 0.0;
    } else {
        worldPosition = vec3(
            aLocalPosition.x + aInstanceData0.x,
            aInstanceData0.z,
            aLocalPosition.z + aInstanceData0.y);
        vTileUv = vec2(0.0);
        vLocalUv = aLocalPosition.xz;
        vLotColor = vec3(0.0);
        vRoadGlyphs = vec2(aInstanceData0.w, aInstanceData1.x);
        vRoadMasks = vec2(aInstanceData1.y, aInstanceData1.z);
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
uniform sampler2D uRoadBaseAtlasTexture;
uniform sampler2D uRoadArrowAtlasTexture;
uniform vec2 uRoadAtlasGrid;
uniform float uRoadAlphaScale;
uniform vec3 uRoadTintColor;
uniform float uRoadTintStrength;

in vec2 vTileUv;
in vec2 vLocalUv;
in vec3 vLotColor;
in vec2 vRoadGlyphs;
in vec2 vRoadMasks;
in float vSurfaceLift;
flat in int vRenderMode;

vec4 sampleRoadAtlas(sampler2D atlasTexture, float glyphIndex, vec2 localUv)
{
    if (glyphIndex < 0.5) {
        return vec4(0.0);
    }

    vec2 atlasUv = vec2(
        (mod(glyphIndex, uRoadAtlasGrid.x) + clamp(localUv.x, 0.0, 0.9999)) / uRoadAtlasGrid.x,
        (floor(glyphIndex / uRoadAtlasGrid.x) + clamp(localUv.y, 0.0, 0.9999)) / uRoadAtlasGrid.y);
    return texture(atlasTexture, atlasUv);
}

bool hasMaskBit(float maskValue, int bitValue)
{
    int mask = int(floor(maskValue + 0.5));
    return (mask & bitValue) != 0;
}

vec3 applyRoadEdgeOverlays(vec3 baseColor, vec2 localUv, float laneGraphicMask, float dividerMask)
{
    vec3 finalColor = baseColor;
    vec3 sidewalkColor = vec3(0.74, 0.72, 0.66);
    vec3 crosswalkColor = vec3(0.91, 0.89, 0.78);
    vec3 whiteDividerColor = vec3(0.88, 0.84, 0.74);
    vec3 yellowDividerColor = vec3(0.93, 0.86, 0.32);

    int laneGraphics = int(floor(laneGraphicMask + 0.5));
    int sidewalkEdges = laneGraphics & 15;
    int crosswalkEdges = (laneGraphics >> 4) & 15;
    bool crosswalkStripe = fract((localUv.x + localUv.y) * 10.0) < 0.52;

    if ((sidewalkEdges & 1) != 0 && localUv.y < 0.16) {
        finalColor = sidewalkColor;
    }
    if ((sidewalkEdges & 2) != 0 && localUv.x > 0.84) {
        finalColor = sidewalkColor;
    }
    if ((sidewalkEdges & 4) != 0 && localUv.y > 0.84) {
        finalColor = sidewalkColor;
    }
    if ((sidewalkEdges & 8) != 0 && localUv.x < 0.16) {
        finalColor = sidewalkColor;
    }
    if ((crosswalkEdges & 1) != 0 && localUv.y < 0.18 && crosswalkStripe) {
        finalColor = crosswalkColor;
    }
    if ((crosswalkEdges & 2) != 0 && localUv.x > 0.82 && crosswalkStripe) {
        finalColor = crosswalkColor;
    }
    if ((crosswalkEdges & 4) != 0 && localUv.y > 0.82 && crosswalkStripe) {
        finalColor = crosswalkColor;
    }
    if ((crosswalkEdges & 8) != 0 && localUv.x < 0.18 && crosswalkStripe) {
        finalColor = crosswalkColor;
    }

    int divider = int(floor(dividerMask + 0.5));
    int whiteMask = divider & 15;
    int yellowMask = (divider >> 4) & 15;
    bool northDivider = ((whiteMask | yellowMask) & 1) != 0 && localUv.y < 0.035;
    bool eastDivider = ((whiteMask | yellowMask) & 2) != 0 && localUv.x > 0.965;
    bool southDivider = ((whiteMask | yellowMask) & 4) != 0 && localUv.y > 0.965;
    bool westDivider = ((whiteMask | yellowMask) & 8) != 0 && localUv.x < 0.035;
    if (northDivider || eastDivider || southDivider || westDivider) {
        bool yellow = ((yellowMask & 1) != 0 && northDivider) ||
            ((yellowMask & 2) != 0 && eastDivider) ||
            ((yellowMask & 4) != 0 && southDivider) ||
            ((yellowMask & 8) != 0 && westDivider);
        finalColor = yellow ? yellowDividerColor : whiteDividerColor;
    }

    return finalColor;
}

void main()
{
    if (vRenderMode == 0) {
        vec2 tileState = clamp(vec2(0.5) + texture(uTileStateTexture, vTileUv).rg * 0.5, vec2(0.0), vec2(1.0));
        vec3 finalColor = vec3(tileState.r, tileState.g, 0.18 + vSurfaceLift * 4.0);

        vec4 packedRoadState = floor(texture(uGroundRoadStateTexture, vTileUv).rgba * 255.0 + 0.5);
        vec4 roadBase = sampleRoadAtlas(uRoadBaseAtlasTexture, packedRoadState.x, vLocalUv);
        vec4 roadArrow = sampleRoadAtlas(uRoadArrowAtlasTexture, packedRoadState.y, vLocalUv);
        finalColor = mix(finalColor, roadBase.rgb, roadBase.a);
        if (roadBase.a > 0.001) {
            finalColor = applyRoadEdgeOverlays(finalColor, vLocalUv, packedRoadState.z, packedRoadState.w);
        }
        finalColor = mix(finalColor, roadArrow.rgb, roadArrow.a);
        color = vec4(finalColor, 1.0);
        return;
    }

    if (vRenderMode == 2) {
        vec4 roadBase = sampleRoadAtlas(uRoadBaseAtlasTexture, vRoadGlyphs.x, vLocalUv);
        vec4 roadArrow = sampleRoadAtlas(uRoadArrowAtlasTexture, vRoadGlyphs.y, vLocalUv);
        vec3 finalColor = applyRoadEdgeOverlays(roadBase.rgb, vLocalUv, vRoadMasks.x, vRoadMasks.y);
        finalColor = mix(finalColor, roadArrow.rgb, roadArrow.a);
        finalColor = mix(finalColor, uRoadTintColor, clamp(uRoadTintStrength, 0.0, 1.0));
        float finalAlpha = max(roadBase.a, roadArrow.a) * clamp(uRoadAlphaScale, 0.0, 1.0);
        if (finalAlpha <= 0.001) {
            discard;
        }

        color = vec4(finalColor, finalAlpha);
        return;
    }

    color = vec4(vLotColor, 1.0);
}
