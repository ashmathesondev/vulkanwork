#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

// Values match ALPHA_* in shaders/pbr.frag
enum class AlphaMode : uint32_t
{
	Opaque = 0,
	Mask = 1,
	Blend = 2,
};

enum class TextureSlot : uint32_t
{
	BaseColor,
	MetallicRoughness,
	Normal,
	Occlusion,
	Emissive,
};
inline constexpr size_t TEXTURE_SLOT_COUNT = 5;

// GPU-uploaded material factors (std140 layout)
struct MaterialFactorsGPU
{
	alignas(16) glm::vec4 baseColorFactor;	// 16B
	alignas(4) float metallicFactor;		//  4B
	alignas(4) float roughnessFactor;		//  4B
	alignas(4) float normalScale;			//  4B
	alignas(4) float occlusionStrength;		//  4B
	alignas(16) glm::vec4 emissiveFactor;	// 16B (vec3 + pad)
	alignas(4) float alphaCutoff;			//  4B
	alignas(4) uint32_t alphaMode;			//  4B
	alignas(8) glm::vec2 _pad0;				//  8B
};
static_assert(sizeof(MaterialFactorsGPU) == 64);

// The editable metallic-roughness parameters, saved in .scene files.
struct MaterialParams
{
	glm::vec4 baseColorFactor{1.0f};
	float metallicFactor = 1.0f;
	float roughnessFactor = 1.0f;
	glm::vec3 emissiveFactor{0.0f};
	float normalScale = 1.0f;
	float occlusionStrength = 1.0f;
	AlphaMode alphaMode = AlphaMode::Opaque;
	float alphaCutoff = 0.5f;

	// Maps chosen in the editor, by TextureSlot. Unset keeps the model's map;
	// an empty path removes it.
	std::array<std::optional<std::string>, TEXTURE_SLOT_COUNT> textureOverrides;
};

struct Material : MaterialParams
{
	// Texture indices into scene texture array
	int32_t baseColorTexture = -1;
	int32_t metallicRoughnessTexture = -1;
	int32_t normalTexture = -1;
	int32_t emissiveTexture = -1;
	int32_t occlusionTexture = -1;

	int32_t& texture(TextureSlot slot)
	{
		switch (slot)
		{
			case TextureSlot::BaseColor:
				return baseColorTexture;
			case TextureSlot::MetallicRoughness:
				return metallicRoughnessTexture;
			case TextureSlot::Normal:
				return normalTexture;
			case TextureSlot::Occlusion:
				return occlusionTexture;
			default:
				return emissiveTexture;
		}
	}

	// Changed in the editor, so scene files must store its parameters
	bool edited = false;

	// GPU handle (set by Renderer::create_material_descriptor)
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

	// Factor UBO (set by Renderer::create_material_descriptor)
	VkBuffer factorBuffer = VK_NULL_HANDLE;
	VkDeviceMemory factorMemory = VK_NULL_HANDLE;
};
