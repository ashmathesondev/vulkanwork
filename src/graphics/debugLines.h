#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "graphics/light.h"

struct LineVertex
{
	glm::vec3 pos;
	glm::vec3 color;

	static VkVertexInputBindingDescription binding_desc()
	{
		return {0, sizeof(LineVertex), VK_VERTEX_INPUT_RATE_VERTEX};
	}

	static std::array<VkVertexInputAttributeDescription, 2> attrib_descs()
	{
		return {{
			{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LineVertex, pos)},
			{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(LineVertex, color)},
		}};
	}
};

std::vector<LineVertex> generate_light_lines(const LightEnvironment& lights);
