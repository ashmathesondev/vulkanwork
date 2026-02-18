#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

struct AABB;
struct Mesh;
struct SceneGraph;

struct Ray
{
	glm::vec3 origin;
	glm::vec3 direction;
};

struct Selection
{
	std::optional<uint32_t> selectedNode;

	static Ray screen_to_ray(float mouseX, float mouseY, float screenW,
							 float screenH, const glm::mat4& view,
							 const glm::mat4& proj);

	static bool ray_aabb(const Ray& ray, const AABB& aabb,
						 const glm::mat4& transform, float& tOut);

	void pick(float mouseX, float mouseY, float screenW, float screenH,
			  const glm::mat4& view, const glm::mat4& proj,
			  const SceneGraph& sceneGraph, const std::vector<Mesh>& meshes);
};
