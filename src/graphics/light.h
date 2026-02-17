#pragma once

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

// =============================================================================
// Light types (CPU-side, user-facing)
// =============================================================================

enum class LightType : uint32_t
{
	Directional = 0,
	Point = 1,
	Spot = 2,
};

struct DirectionalLight
{
	glm::vec3 direction{0.0f, -1.0f, 0.0f};
	glm::vec3 color{1.0f};
	float intensity = 1.0f;
};

struct PointLight
{
	glm::vec3 position{0.0f};
	glm::vec3 color{1.0f};
	float intensity = 1.0f;
	float radius = 10.0f;
};

struct SpotLight
{
	glm::vec3 position{0.0f};
	glm::vec3 direction{0.0f, -1.0f, 0.0f};
	glm::vec3 color{1.0f};
	float intensity = 1.0f;
	float radius = 10.0f;
	float innerConeAngle = glm::radians(25.0f);
	float outerConeAngle = glm::radians(35.0f);
};

struct AmbientLight
{
	glm::vec3 color{1.0f};
	float intensity = 0.03f;
};

// =============================================================================
// GPU-packed light (shared between C++ and GLSL, 64 bytes per light)
// =============================================================================

struct GPULight
{
	alignas(16) glm::vec4 positionAndType;	   // xyz=position, w=float(type)
	alignas(16) glm::vec4 directionAndRadius;  // xyz=direction, w=radius
	alignas(16) glm::vec4 colorAndIntensity;   // xyz=color, w=intensity
	alignas(16) glm::vec4 coneParams;  // x=cos(inner), y=cos(outer), zw=0
};

// =============================================================================
// LightEnvironment -- aggregates all lights in a scene
// =============================================================================

struct LightEnvironment
{
	AmbientLight ambient;
	std::vector<DirectionalLight> directionals;
	std::vector<PointLight> points;
	std::vector<SpotLight> spots;

	uint32_t total_light_count() const;
	std::vector<GPULight> pack_gpu_lights() const;
};
