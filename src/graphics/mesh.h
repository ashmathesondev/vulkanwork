#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

struct Vertex
{
	glm::vec3 pos;
	glm::vec3 normal;
	glm::vec2 uv;
	glm::vec4 tangent;	// .w = handedness (+1 or -1)

	static VkVertexInputBindingDescription binding_desc()
	{
		return {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
	}

	static std::array<VkVertexInputAttributeDescription, 4> attrib_descs()
	{
		return {{
			{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
			{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
			{2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
			{3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)},
		}};
	}
};

struct Mesh
{
	// CPU data
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	uint32_t materialIndex = 0;
	glm::mat4 transform{1.0f};

	// GPU handles (set by Renderer::upload_mesh)
	VkBuffer vertexBuffer = VK_NULL_HANDLE;
	VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
	VkBuffer indexBuffer = VK_NULL_HANDLE;
	VkDeviceMemory indexMemory = VK_NULL_HANDLE;
};
