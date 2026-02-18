#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

struct Texture
{
	// CPU data
	uint32_t width = 0;
	uint32_t height = 0;
	std::vector<uint8_t> pixels;  // RGBA8
	bool isSrgb = true;

	// GPU handles (set by Renderer::upload_texture)
	VkImage image = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	uint32_t mipLevels = 1;

	bool uploaded() const;
	static Texture solid_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
							   bool srgb);
};
