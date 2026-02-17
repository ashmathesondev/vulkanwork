#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

struct Material
{
	// Texture indices into scene texture array
	int32_t baseColorTexture = -1;
	int32_t metallicRoughnessTexture = -1;
	int32_t normalTexture = -1;
	int32_t emissiveTexture = -1;

	// Scalar PBR factors
	glm::vec4 baseColorFactor{1.0f};
	float metallicFactor = 1.0f;
	float roughnessFactor = 1.0f;
	glm::vec3 emissiveFactor{0.0f};

	// GPU handle (set by Renderer::create_material_descriptor)
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};
