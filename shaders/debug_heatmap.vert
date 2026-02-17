#version 450

// Fullscreen triangle (3 vertices, no vertex buffer)
layout(location = 0) out vec2 fragUV;

void main()
{
    // gl_VertexIndex: 0, 1, 2 -> fullscreen triangle covering [-1,1] NDC
    fragUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragUV * 2.0 - 1.0, 0.0, 1.0);
}
