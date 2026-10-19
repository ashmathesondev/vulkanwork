#version 450

// =============================================================================
// Gaussian Splat — Fragment shader
// =============================================================================
// Evaluates the 2D Gaussian function at each fragment and outputs the
// alpha-weighted color.  Pipeline uses src-alpha / one-minus-src-alpha blending.

layout(location = 0) in vec2  fragCenter;    // screen-space center (pixels)
layout(location = 1) in vec3  fragCov2dInv;  // inverse cov2d [a, b, c]
layout(location = 2) in vec3  fragColor;
layout(location = 3) in float fragAlpha;

layout(location = 0) out vec4 outColor;

void main()
{
    // Distance from fragment to splat center in screen pixels
    vec2 d = gl_FragCoord.xy - fragCenter;

    // Evaluate 2D Gaussian: g = exp(-0.5 * d^T * Sigma_inv * d)
    // Sigma_inv = [a b; b c]  (fragCov2dInv)
    float a = fragCov2dInv.x;
    float b = fragCov2dInv.y;
    float c = fragCov2dInv.z;
    float power = -0.5 * (a * d.x*d.x + 2.0 * b * d.x*d.y + c * d.y*d.y);

    if (power > 0.0) discard;  // degenerate (outside ellipse)

    float g = exp(power);

    float finalAlpha = fragAlpha * g;
    if (finalAlpha < (1.0 / 255.0)) discard;

    outColor = vec4(fragColor, finalAlpha);
}
