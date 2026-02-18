#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

struct SceneNode
{
	std::string name;
	glm::mat4 localTransform{1.0f};
	glm::mat4 worldTransform{1.0f};

	// Mesh link
	std::optional<uint32_t> meshIndex;	// runtime index into Renderer::meshes_
	std::string modelPath;				// source GLB/GLTF path
	uint32_t meshIndexInModel = 0;		// index within that model's mesh list

	std::optional<uint32_t> parent;	 // into SceneGraph::nodes
	std::vector<uint32_t> children;
};

struct SceneGraph
{
	std::vector<SceneNode> nodes;
	std::vector<uint32_t> roots;  // nodes with no parent

	uint32_t add_node(const std::string& name, const glm::mat4& localTransform,
					  std::optional<uint32_t> meshIndex,
					  const std::string& modelPath, uint32_t meshIndexInModel,
					  std::optional<uint32_t> parentId);

	void remove_node(uint32_t nodeIdx);
	void update_world_transforms();
	void clear();

   private:
	void update_node(uint32_t nodeIdx, const glm::mat4& parentWorld);
};
