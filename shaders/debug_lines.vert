#version 450

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

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    fragColor = inColor;
    gl_Position = frame.proj * frame.view * vec4(inPosition, 1.0);
}
