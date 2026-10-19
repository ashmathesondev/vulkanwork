#version 450

// =============================================================================
// Gaussian Splat — Vertex shader
// =============================================================================
// No vertex buffer; generates 6 vertices (2 triangles) per splat.
//   splatIdx = gl_VertexIndex / 6
//   corner   = gl_VertexIndex % 6
//
// Reads the sorted splat index from sortVals (binding 1), then reads the
// preprocessed 2D Gaussian data from preprocessBuffer (binding 0).
// Computes the oriented bounding quad via eigendecomposition of cov2d and
// outputs the corner in NDC along with per-fragment data.

// ---- Descriptor set 0: render data ----------------------------------------
layout(std430, set = 0, binding = 0) readonly buffer PreprocessBuffer {
    float prepData[];   // 10 floats per splat (see gaussianSplats.cpp for layout)
};
layout(std430, set = 0, binding = 1) readonly buffer SortVals {
    uint sortedIndices[];   // sortValsA after 4 radix-sort passes
};

layout(push_constant) uniform PC {
    uint  N;
    float screenW;
    float screenH;
} push;

// ---- Outputs to fragment shader -------------------------------------------
layout(location = 0) out vec2  fragCenter;    // screen-space center (pixels)
layout(location = 1) out vec3  fragCov2dInv;  // inverse cov2d: [a_inv, b_inv, c_inv]
layout(location = 2) out vec3  fragColor;
layout(location = 3) out float fragAlpha;

// ---- quad corner offsets (unit square, 2 triangles) -----------------------
const vec2 CORNERS[6] = vec2[6](
    vec2(-1.0, -1.0),
    vec2( 1.0, -1.0),
    vec2(-1.0,  1.0),
    vec2( 1.0, -1.0),
    vec2( 1.0,  1.0),
    vec2(-1.0,  1.0)
);

// ---- Eigendecomposition of symmetric 2x2 [a,b;b,c] -----------------------
// Returns (lambda1, lambda2) and sets v1 to the first eigenvector (unit).
void eigen2x2(float a, float b, float c,
              out float l1, out float l2, out vec2 v1)
{
    float trace  = a + c;
    float disc   = sqrt(max(0.0, (a - c)*(a - c) * 0.25 + b * b));
    l1 = trace * 0.5 + disc;
    l2 = trace * 0.5 - disc;

    if (abs(b) > 1e-6) {
        v1 = normalize(vec2(b, l1 - a));
    } else {
        v1 = (a >= c) ? vec2(1.0, 0.0) : vec2(0.0, 1.0);
    }
}

// ---- Invert 2x2 symmetric matrix [a,b;b,c] --------------------------------
void invert2x2(float a, float b, float c,
               out float ai, out float bi, out float ci)
{
    float det = a * c - b * b;
    float invDet = (abs(det) > 1e-8) ? 1.0 / det : 0.0;
    ai =  c * invDet;
    bi = -b * invDet;
    ci =  a * invDet;
}

void main()
{
    uint splatIdx = uint(gl_VertexIndex) / 6u;
    uint corner   = uint(gl_VertexIndex) % 6u;

    if (splatIdx >= push.N) {
        gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Look up sorted splat index
    uint si  = sortedIndices[splatIdx];
    uint b   = si * 10u;

    float cx    = prepData[b + 0u];  // center x (pixels)
    float cy    = prepData[b + 1u];  // center y (pixels)
    float covA  = prepData[b + 2u];  // cov2d [a,b,c]
    float covB  = prepData[b + 3u];
    float covC  = prepData[b + 4u];
    float red   = prepData[b + 5u];
    float grn   = prepData[b + 6u];
    float blu   = prepData[b + 7u];
    float alpha = prepData[b + 8u];
    float depth = prepData[b + 9u];

    // Eigendecomposition -> quad half-axes in pixels
    float l1, l2;
    vec2  v1;
    eigen2x2(covA, covB, covC, l1, l2, v1);
    vec2 v2 = vec2(-v1.y, v1.x);

    float r1 = 3.0 * sqrt(max(0.0, l1));
    float r2 = 3.0 * sqrt(max(0.0, l2));

    // Screen-space position of this corner (pixels)
    vec2 offset     = CORNERS[corner];
    vec2 screenPos  = vec2(cx, cy)
                    + offset.x * r1 * v1
                    + offset.y * r2 * v2;

    // Convert pixels -> NDC (Vulkan: y points down)
    vec2 ndc = vec2(
         screenPos.x / push.screenW * 2.0 - 1.0,
        -(screenPos.y / push.screenH * 2.0 - 1.0)
    );
    gl_Position = vec4(ndc, depth, 1.0);

    // Inverse 2D covariance for Gaussian evaluation in fragment shader
    float ai, bi, ci;
    invert2x2(covA, covB, covC, ai, bi, ci);

    fragCenter   = vec2(cx, cy);
    fragCov2dInv = vec3(ai, bi, ci);
    fragColor    = vec3(red, grn, blu);
    fragAlpha    = alpha;
}
