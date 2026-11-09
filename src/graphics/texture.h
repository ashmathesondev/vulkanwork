#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

struct Texture
{
	// CPU data
	uint32_t width = 0;
	uint32_t height = 0;
	std::vector<uint8_t> pixels;  // RGBA8
	bool isSrgb = true;
	std::string sourcePath;		// the image's own file, if it has one
	std::string containerPath;	// model file holding an embedded image

	// GPU handles (set by Renderer::upload_texture)
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	uint32_t mipLevels = 1;
	VkDescriptorSet thumbnail =
		VK_NULL_HANDLE;	 // ImGui preview, made on demand

	bool uploaded() const;
	static Texture solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
							   bool srgb);
};
