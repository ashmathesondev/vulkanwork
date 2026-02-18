#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

struct SceneNode
{
	std::string name;
	glm::mat4 localTransform{1.0f};
	glm::mat4 worldTransform{1.0f};
	std::optional<uint32_t> meshIndex;	// into Renderer::meshes_
	std::optional<uint32_t> parent;		// into SceneGraph::nodes
	std::vector<uint32_t> children;
};

struct SceneGraph
{
	std::vector<SceneNode> nodes;
	std::vector<uint32_t> roots;  // nodes with no parent

	uint32_t add_node(const std::string& name, const glm::mat4& localTransform,
					  std::optional<uint32_t> meshIndex,
					  std::optional<uint32_t> parentId);

	void remove_node(uint32_t nodeIdx);
	void update_world_transforms();
	void clear();

   private:
	void update_node(uint32_t nodeIdx, const glm::mat4& parentWorld);
};
