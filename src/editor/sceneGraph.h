#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

#include "graphics/light.h"

// Light attached to a scene node. Position comes from the node's world
// translation and direction from its local -Z axis, so the gizmo edits both.
struct LightComponent
{
	LightType type = LightType::Point;
	glm::vec3 color{1.0f};
	float intensity = 1.0f;
	float radius = 10.0f;						 // point / spot
	float innerConeAngle = glm::radians(25.0f);	 // spot, radians
	float outerConeAngle = glm::radians(35.0f);	 // spot, radians
};

const char* light_type_name(LightType type);

// Transform placing a light at `position` with its -Z axis along `direction`.
glm::mat4 make_light_transform(const glm::vec3& position,
							   const glm::vec3& direction);

struct SceneNode
{
	std::string name;
	glm::mat4 localTransform{1.0f};
	glm::mat4 worldTransform{1.0f};

	// Mesh link
	std::optional<uint32_t> meshIndex;	// runtime index into Renderer::meshes_
	std::string modelPath;				// source GLB/GLTF path
	uint32_t meshIndexInModel = 0;		// index within that model's mesh list

	// Light link
	std::optional<LightComponent> light;

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

	uint32_t add_light_node(const std::string& name,
							const LightComponent& light,
							const glm::mat4& localTransform,
							std::optional<uint32_t> parentId = std::nullopt);

	void remove_node(uint32_t nodeIdx);
	void update_world_transforms();
	void clear();

	// Rebuild env's directional/point/spot lists from light nodes. Ambient is
	// left untouched. Call after update_world_transforms().
	void collect_lights(LightEnvironment& env) const;

   private:
	void update_node(uint32_t nodeIdx, const glm::mat4& parentWorld);
};
