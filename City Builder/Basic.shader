#shader vertex
#version 460 core

layout(location = 0) in vec3 aLocalPosition;
layout(location = 1) in vec4 aInstanceData0;
layout(location = 2) in vec4 aInstanceData1;

uniform mat4 uViewProjection;
uniform int uRenderMode;

out vec2 vTileUv;
out vec3 vLotColor;
out float vSurfaceLift;
flat out int vRenderMode;

void main()
{
    vec3 worldPosition;

    if (uRenderMode == 0) {
        worldPosition = vec3(
            aLocalPosition.x + aInstanceData0.x,
            aInstanceData1.x,
            aLocalPosition.z + aInstanceData0.y);
        vTileUv = aInstanceData0.zw;
        vLotColor = vec3(0.0);
        vSurfaceLift = aInstanceData1.x;
    } else {
        vec3 scaledPosition = vec3(
            aLocalPosition.x * aInstanceData0.z,
            aLocalPosition.y * aInstanceData1.x,
            aLocalPosition.z * aInstanceData0.w);
        worldPosition = vec3(aInstanceData0.x, 0.0, aInstanceData0.y) + scaledPosition;
        vTileUv = vec2(0.0);
        vLotColor = aInstanceData1.yzw;
        vSurfaceLift = 0.0;
    }

    vRenderMode = uRenderMode;
    gl_Position = uViewProjection * vec4(worldPosition, 1.0);
}

#shader fragment
#version 460 core

layout(location = 0) out vec4 color;

uniform sampler2D uTileStateTexture;

in vec2 vTileUv;
in vec3 vLotColor;
in float vSurfaceLift;
flat in int vRenderMode;

void main()
{
    if (vRenderMode == 0) {
        vec2 tileState = texture(uTileStateTexture, vTileUv).rg;
        color = vec4(tileState.r, tileState.g, 0.18 + vSurfaceLift * 4.0, 1.0);
        return;
    }

    color = vec4(vLotColor, 1.0);
}
