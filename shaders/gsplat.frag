#version 450

// =============================================================================
// Gaussian Splat — Fragment shader
// =============================================================================
// mode 0 (Gaussian): evaluates the 2D Gaussian at each fragment and outputs
//                    the alpha-weighted colour (src-alpha / one-minus-src-alpha
//                    blending, back-to-front order).
// mode 1 (Points):   opaque disc of the splat colour. Back-to-front order makes
//                    nearer points overwrite farther ones.

layout(location = 0) in vec2  fragCenter;    // screen-space center (pixels)
layout(location = 1) in vec3  fragCov2dInv;  // inverse cov2d [a, b, c]
layout(location = 2) in vec3  fragColor;     // linear RGB
layout(location = 3) in float fragAlpha;
layout(location = 4) flat in uint fragMode;
layout(location = 5) in float fragPointRadius;

layout(location = 0) out vec4 outColor;

void main()
{
    // Distance from fragment to splat center in screen pixels
    vec2 d = gl_FragCoord.xy - fragCenter;

    if (fragMode == 1u) {
        if (dot(d, d) > fragPointRadius * fragPointRadius) discard;
        outColor = vec4(fragColor, 1.0);
        return;
    }

    // Evaluate 2D Gaussian: g = exp(-0.5 * d^T * Sigma_inv * d)
    // Sigma_inv = [a b; b c]  (fragCov2dInv)
    float a = fragCov2dInv.x;
    float b = fragCov2dInv.y;
    float c = fragCov2dInv.z;
    float power = -0.5 * (a * d.x*d.x + 2.0 * b * d.x*d.y + c * d.y*d.y);

    if (power > 0.0) discard;  // degenerate covariance

    float g = exp(power);

    // Clamp like the reference rasterizer so dense cores stay translucent
    float finalAlpha = min(0.99, fragAlpha * g);
    if (finalAlpha < (1.0 / 255.0)) discard;

    outColor = vec4(fragColor, finalAlpha);
}
