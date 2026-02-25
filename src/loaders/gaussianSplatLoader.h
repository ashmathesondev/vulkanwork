#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

// =============================================================================
// CPU-side Gaussian Splat data
// =============================================================================

// GPU-compatible layout: exactly 240 bytes, matching the SSBO struct in
// gsplat_preprocess.comp.  Keep fields in this order / with this padding.
struct RawSplat
{
	glm::vec3 position;	 // xyz                    (12 bytes, offset   0)
	float opacity;		 // pre-sigmoid logit       ( 4 bytes, offset  12)
	glm::vec4 rotation;	 // wxyz quaternion         (16 bytes, offset  16)
	glm::vec3 scale;	 // pre-exp log-scale       (12 bytes, offset  32)
	float _pad;			 //                         ( 4 bytes, offset  44)
	float sh[48];		 // R_dc,G_dc,B_dc + rest   (192 bytes, offset 48)
						 // Total: 240 bytes
};
static_assert(sizeof(RawSplat) == 240, "RawSplat must be 240 bytes");

struct GaussianSplatCloud
{
	std::vector<RawSplat> splats;
	std::string sourcePath;
};

// Loads a binary-little-endian 3DGS .ply file.
// Returns empty cloud on failure.
GaussianSplatCloud load_gaussian_splat_ply(const std::string& path);
