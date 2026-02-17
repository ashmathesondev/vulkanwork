#version 450

const float PI = 3.14159265359;
const uint TILE_SIZE = 16;

// Light types
const uint LIGHT_DIRECTIONAL = 0;
const uint LIGHT_POINT       = 1;
const uint LIGHT_SPOT        = 2;

// Per-frame UBO (set 0, binding 0) -- Forward+
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4  view;
    mat4  proj;
    mat4  invProj;
    vec3  cameraPos;
    uint  lightCount;
    vec3  ambientColor;
    uint  tileCountX;
    uint  tileCountY;
    uint  screenWidth;
    uint  screenHeight;
} frame;

// Per-material textures (set 1)
layout(set = 1, binding = 0) uniform sampler2D baseColorMap;
layout(set = 1, binding = 1) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D emissiveMap;

// Per-material factors (set 1, binding 4)
layout(set = 1, binding = 4) uniform MaterialFactors {
    vec4  baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    vec2  _pad0;
    vec4  emissiveFactor;
} matFactors;

// Light data (set 2)
struct GPULight {
    vec4 positionAndType;
    vec4 directionAndRadius;
    vec4 colorAndIntensity;
    vec4 coneParams;
};

layout(std430, set = 2, binding = 0) readonly buffer LightBuffer {
    GPULight lights[];
};

layout(std430, set = 2, binding = 1) readonly buffer TileLightBuffer {
    uint tileData[];
};

// Inputs from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in mat3 fragTBN;

layout(location = 0) out vec4 outColor;

// =============================================================================
// PBR functions
// =============================================================================

float distributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float geometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) *
           geometrySchlickGGX(NdotL, roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float attenuate(float dist, float radius)
{
    float d2 = dist * dist;
    float r2 = radius * radius;
    float num = clamp(1.0 - (d2 * d2) / (r2 * r2), 0.0, 1.0);
    return (num * num) / (d2 + 1.0);
}

// Evaluate BRDF for one light direction
vec3 evaluateBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic,
                  float roughness, vec3 F0, vec3 lightColor, float lightIntensity)
{
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);

    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = D * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    return (kD * albedo / PI + specular) * lightColor * lightIntensity * NdotL;
}

// =============================================================================
// Main
// =============================================================================

void main()
{
    // Sample textures and apply material factors
    vec4 baseColor = texture(baseColorMap, fragTexCoord) * matFactors.baseColorFactor;
    vec2 metallicRoughness = texture(metallicRoughnessMap, fragTexCoord).bg;
    float metallic = metallicRoughness.x * matFactors.metallicFactor;
    float roughness = metallicRoughness.y * matFactors.roughnessFactor;
    vec3 emissive = texture(emissiveMap, fragTexCoord).rgb * matFactors.emissiveFactor.rgb;

    // Normal mapping
    vec3 tangentNormal = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;
    vec3 N = normalize(fragTBN * tangentNormal);

    vec3 V = normalize(frame.cameraPos - fragWorldPos);
    vec3 albedo = baseColor.rgb;

    // Dielectric F0 = 0.04, metals use albedo
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Determine which tile this fragment belongs to
    uvec2 tileCoord = uvec2(gl_FragCoord.xy) / TILE_SIZE;
    uint tileIndex = tileCoord.y * frame.tileCountX + tileCoord.x;
    uint tileOffset = tileIndex * (1 + 256);  // count + MAX_LIGHTS_PER_TILE indices
    uint tileLightCount = tileData[tileOffset];

    vec3 Lo = vec3(0.0);

    // Loop over this tile's light list
    for (uint i = 0; i < tileLightCount; ++i)
    {
        uint lightIdx = tileData[tileOffset + 1 + i];
        GPULight light = lights[lightIdx];

        uint lightType = uint(light.positionAndType.w);
        vec3 lightColor = light.colorAndIntensity.rgb;
        float lightIntensity = light.colorAndIntensity.w;

        if (lightType == LIGHT_DIRECTIONAL)
        {
            vec3 L = normalize(-light.directionAndRadius.xyz);
            Lo += evaluateBRDF(N, V, L, albedo, metallic, roughness, F0,
                               lightColor, lightIntensity);
        }
        else if (lightType == LIGHT_POINT)
        {
            vec3 toLight = light.positionAndType.xyz - fragWorldPos;
            float dist = length(toLight);
            vec3 L = toLight / dist;
            float radius = light.directionAndRadius.w;
            float atten = attenuate(dist, radius);
            Lo += evaluateBRDF(N, V, L, albedo, metallic, roughness, F0,
                               lightColor, lightIntensity * atten);
        }
        else if (lightType == LIGHT_SPOT)
        {
            vec3 toLight = light.positionAndType.xyz - fragWorldPos;
            float dist = length(toLight);
            vec3 L = toLight / dist;
            float radius = light.directionAndRadius.w;
            float atten = attenuate(dist, radius);

            // Cone falloff
            float cosAngle = dot(normalize(light.directionAndRadius.xyz), -L);
            float cosInner = light.coneParams.x;
            float cosOuter = light.coneParams.y;
            float spotFactor = smoothstep(cosOuter, cosInner, cosAngle);

            Lo += evaluateBRDF(N, V, L, albedo, metallic, roughness, F0,
                               lightColor, lightIntensity * atten * spotFactor);
        }
    }

    // Ambient
    vec3 ambient = frame.ambientColor * albedo;

    vec3 color = ambient + Lo + emissive;

    // Reinhard tone mapping
    color = color / (color + vec3(1.0));

    // No manual gamma — sRGB swapchain handles it
    outColor = vec4(color, baseColor.a);
}
