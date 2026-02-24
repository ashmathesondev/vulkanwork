#version 450

layout(push_constant) uniform Push {
    mat4 model;
    mat4 lightVP;
} push;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec4 inTangent;

void main() {
    gl_Position = push.lightVP * push.model * vec4(inPosition, 1.0);
}
