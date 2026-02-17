#version 450

const uint TILE_SIZE = 16;
const uint MAX_LIGHTS_PER_TILE = 256;

// Per-frame UBO (set 0, binding 0)
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

// Tile light data (set 1, binding 1)
layout(std430, set = 1, binding = 1) readonly buffer TileLightBuffer {
    uint tileData[];
};

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

// Color ramp: 0=black, low=blue, mid=green, high=yellow, max=red
vec3 heatmapColor(float t)
{
    // t in [0, 1]
    if (t < 0.25) return mix(vec3(0.0, 0.0, 0.0), vec3(0.0, 0.0, 1.0), t * 4.0);
    if (t < 0.50) return mix(vec3(0.0, 0.0, 1.0), vec3(0.0, 1.0, 0.0), (t - 0.25) * 4.0);
    if (t < 0.75) return mix(vec3(0.0, 1.0, 0.0), vec3(1.0, 1.0, 0.0), (t - 0.50) * 4.0);
    return mix(vec3(1.0, 1.0, 0.0), vec3(1.0, 0.0, 0.0), (t - 0.75) * 4.0);
}

void main()
{
    // Determine tile from fragment coordinate
    uvec2 pixelCoord = uvec2(gl_FragCoord.xy);
    uvec2 tileCoord = pixelCoord / TILE_SIZE;
    uint tileIndex = tileCoord.y * frame.tileCountX + tileCoord.x;
    uint tileOffset = tileIndex * (1 + MAX_LIGHTS_PER_TILE);

    uint tileLightCount = tileData[tileOffset];

    // Normalize: 0 lights = 0, 32+ lights = 1.0
    float t = clamp(float(tileLightCount) / 32.0, 0.0, 1.0);

    vec3 color = heatmapColor(t);
    outColor = vec4(color, 0.4);  // semi-transparent overlay
}
