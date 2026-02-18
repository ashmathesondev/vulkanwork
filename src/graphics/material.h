#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

// GPU-uploaded material factors (std140 layout)
struct MaterialFactorsGPU
{
	alignas(16) glm::vec4 baseColorFactor;	// 16B
	alignas(4) float metallicFactor;		//  4B
	alignas(4) float roughnessFactor;		//  4B
	alignas(8) glm::vec2 _pad0;				//  8B
	alignas(16) glm::vec4 emissiveFactor;	// 16B (vec3 + pad)
};

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

	// Factor UBO (set by Renderer::create_material_descriptor)
	VkBuffer factorBuffer = VK_NULL_HANDLE;
	VkDeviceMemory factorMemory = VK_NULL_HANDLE;
};
