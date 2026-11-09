#version 450

layout(push_constant) uniform PushConstants {
    mat4 lightMVP;
} push;

layout(location = 0) in vec3 inPosition;

void main() {
    gl_Position = push.lightMVP * vec4(inPosition, 1.0);
}
