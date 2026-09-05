#pragma once

// Shared by the game and Asset Manager. UVs are in metres, independent of lot size.
inline const char* LotMaterialShaderSource() { return R"GLSL(
uniform sampler2DArray uLotMaterials;
vec3 shadeLotMaterial(vec3 albedo, vec3 normal, vec2 uv, float material, float ao) {
    int layer = int(material + 0.5);
    vec3 texel = texture(uLotMaterials, vec3(uv * 0.5, float(layer))).rgb;
    vec3 n = normalize(normal);
    float sun = max(dot(n, normalize(vec3(-0.55, 0.82, -0.35))), 0.0);
    float sky = 0.58 + 0.16 * max(n.y, 0.0);
    vec3 light = vec3(sky * 0.90, sky * 0.96, sky) + sun * vec3(0.55, 0.51, 0.44);
    vec3 result = albedo * texel * light * ao;
    if (layer == 4) result += vec3(0.035, 0.05, 0.065) * max(n.y + 0.4, 0.0);
    return result;
}
)GLSL"; }
