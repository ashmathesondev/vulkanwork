#version 450

const float PI = 3.14159265359;

// Per-frame UBO (set 0, binding 0)
layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
    vec3 lightDir;
    vec3 lightColor;
} frame;

// Per-material textures (set 1)
layout(set = 1, binding = 0) uniform sampler2D baseColorMap;
layout(set = 1, binding = 1) uniform sampler2D metallicRoughnessMap;
layout(set = 1, binding = 2) uniform sampler2D normalMap;
layout(set = 1, binding = 3) uniform sampler2D emissiveMap;

// Inputs from vertex shader
layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in mat3 fragTBN;

layout(location = 0) out vec4 outColor;

// =============================================================================
// PBR functions
// =============================================================================

// GGX/Trowbridge-Reitz normal distribution
float distributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Smith's Schlick-GGX geometry function
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

// Fresnel-Schlick approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// =============================================================================
// Main
// =============================================================================

void main()
{
    // Sample textures
    // sRGB format handles linearization on sample for baseColor
    vec4 baseColor = texture(baseColorMap, fragTexCoord);
    vec2 metallicRoughness = texture(metallicRoughnessMap, fragTexCoord).bg;
    float metallic = metallicRoughness.x;
    float roughness = metallicRoughness.y;
    vec3 emissive = texture(emissiveMap, fragTexCoord).rgb;

    // Normal mapping
    vec3 tangentNormal = texture(normalMap, fragTexCoord).rgb * 2.0 - 1.0;
    vec3 N = normalize(fragTBN * tangentNormal);

    vec3 V = normalize(frame.cameraPos - fragWorldPos);
    vec3 L = normalize(-frame.lightDir); // lightDir points toward light
    vec3 H = normalize(V + L);

    vec3 albedo = baseColor.rgb;

    // Dielectric F0 = 0.04, metals use albedo
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // Cook-Torrance BRDF
    float D = distributionGGX(N, H, roughness);
    float G = geometrySmith(N, V, L, roughness);
    vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = D * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;

    // Energy conservation
    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);

    vec3 Lo = (kD * albedo / PI + specular) * frame.lightColor * NdotL;

    // Ambient
    vec3 ambient = vec3(0.03) * albedo;

    vec3 color = ambient + Lo + emissive;

    // Reinhard tone mapping
    color = color / (color + vec3(1.0));

    // No manual gamma — sRGB swapchain handles it
    outColor = vec4(color, baseColor.a);
}
