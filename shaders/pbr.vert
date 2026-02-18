#version 450

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

// Per-mesh push constant
layout(push_constant) uniform PushConstants {
    mat4 model;
} push;

// Vertex attributes
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

// Outputs to fragment shader
layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out mat3 fragTBN;

invariant gl_Position;  // ensure identical depth across pipelines

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;
    fragTexCoord = inTexCoord;

    mat3 normalMatrix = transpose(inverse(mat3(push.model)));
    vec3 N = normalize(normalMatrix * inNormal);
    vec3 T = normalize(normalMatrix * inTangent.xyz);
    // Re-orthogonalize T with respect to N
    T = normalize(T - dot(T, N) * N);
    vec3 B = cross(N, T) * inTangent.w;
    fragTBN = mat3(T, B, N);

    gl_Position = frame.proj * frame.view * worldPos;
}
