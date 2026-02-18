#include "selection.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

#include "graphics/mesh.h"
#include "sceneGraph.h"

Ray Selection::screen_to_ray(float mouseX, float mouseY, float screenW,
							 float screenH, const glm::mat4& view,
							 const glm::mat4& proj)
{
	// Convert screen coords to NDC [-1, 1]
	float ndcX = (2.0f * mouseX) / screenW - 1.0f;
	float ndcY = 1.0f - (2.0f * mouseY) / screenH;	// flip Y

	glm::mat4 invVP = glm::inverse(proj * view);

	// Unproject near and far points
	glm::vec4 nearNDC(ndcX, ndcY, 0.0f, 1.0f);	// Vulkan depth 0 = near
	glm::vec4 farNDC(ndcX, ndcY, 1.0f, 1.0f);

	glm::vec4 nearWorld = invVP * nearNDC;
	glm::vec4 farWorld = invVP * farNDC;
	nearWorld /= nearWorld.w;
	farWorld /= farWorld.w;

	Ray ray;
	ray.origin = glm::vec3(nearWorld);
	ray.direction = glm::normalize(glm::vec3(farWorld - nearWorld));
	return ray;
}

bool Selection::ray_aabb(const Ray& ray, const AABB& aabb,
						 const glm::mat4& transform, float& tOut)
{
	// Transform ray into local space of the AABB
	glm::mat4 invTransform = glm::inverse(transform);
	glm::vec3 localOrigin =
		glm::vec3(invTransform * glm::vec4(ray.origin, 1.0f));
	glm::vec3 localDir =
		glm::vec3(invTransform * glm::vec4(ray.direction, 0.0f));

	// Avoid zero-length direction after transform
	float dirLen = glm::length(localDir);
	if (dirLen < 1e-8f) return false;
	localDir /= dirLen;

	// Slab test
	float tMin = 0.0f;
	float tMax = std::numeric_limits<float>::max();

	for (int i = 0; i < 3; ++i)
	{
		if (std::abs(localDir[i]) < 1e-8f)
		{
			// Ray parallel to slab — miss if origin outside
			if (localOrigin[i] < aabb.min[i] || localOrigin[i] > aabb.max[i])
				return false;
		}
		else
		{
			float invD = 1.0f / localDir[i];
			float t1 = (aabb.min[i] - localOrigin[i]) * invD;
			float t2 = (aabb.max[i] - localOrigin[i]) * invD;
			if (t1 > t2) std::swap(t1, t2);
			tMin = std::max(tMin, t1);
			tMax = std::min(tMax, t2);
			if (tMin > tMax) return false;
		}
	}

	// Convert local t back to world t (approximate via direction scale)
	tOut = tMin / dirLen;
	return tOut >= 0.0f;
}

void Selection::pick(float mouseX, float mouseY, float screenW, float screenH,
					 const glm::mat4& view, const glm::mat4& proj,
					 const SceneGraph& sceneGraph,
					 const std::vector<Mesh>& meshes)
{
	Ray ray = screen_to_ray(mouseX, mouseY, screenW, screenH, view, proj);

	float closestT = std::numeric_limits<float>::max();
	std::optional<uint32_t> closestNode;

	for (uint32_t i = 0; i < static_cast<uint32_t>(sceneGraph.nodes.size());
		 ++i)
	{
		const auto& node = sceneGraph.nodes[i];
		if (!node.meshIndex.has_value()) continue;

		uint32_t mi = node.meshIndex.value();
		if (mi >= meshes.size()) continue;

		float t = 0.0f;
		if (ray_aabb(ray, meshes[mi].localBounds, node.worldTransform, t))
		{
			if (t < closestT)
			{
				closestT = t;
				closestNode = i;
			}
		}
	}

	selectedNode = closestNode;
}
