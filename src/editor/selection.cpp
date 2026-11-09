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

// Nearest two-sided hit against the mesh triangles. The local direction stays
// unnormalized so that t is already a world-space distance along the ray.
// shortcut: tests every triangle per click, add a BVH if clicks start hitching.
static bool ray_triangles(const Ray& ray, const Mesh& mesh,
						  const glm::mat4& transform, float& tOut)
{
	glm::mat4 inv = glm::inverse(transform);
	glm::vec3 origin(inv * glm::vec4(ray.origin, 1.0f));
	glm::vec3 dir(inv * glm::vec4(ray.direction, 0.0f));

	float best = std::numeric_limits<float>::max();
	bool hit = false;
	for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
	{
		uint32_t ia = mesh.indices[i];
		uint32_t ib = mesh.indices[i + 1];
		uint32_t ic = mesh.indices[i + 2];
		// Indices come from the model file unchecked
		if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() ||
			ic >= mesh.vertices.size())
			continue;

		const glm::vec3& a = mesh.vertices[ia].pos;
		glm::vec3 e1 = mesh.vertices[ib].pos - a;
		glm::vec3 e2 = mesh.vertices[ic].pos - a;

		// Moller-Trumbore. No epsilon: valid determinants scale with the
		// node transform and triangle size, so tiny triangles must pass.
		glm::vec3 p = glm::cross(dir, e2);
		float det = glm::dot(e1, p);
		if (det == 0.0f) continue;
		float invDet = 1.0f / det;

		glm::vec3 s = origin - a;
		float u = glm::dot(s, p) * invDet;
		if (u < 0.0f || u > 1.0f) continue;

		glm::vec3 q = glm::cross(s, e1);
		float v = glm::dot(dir, q) * invDet;
		if (v < 0.0f || u + v > 1.0f) continue;

		float t = glm::dot(e2, q) * invDet;
		if (t >= 0.0f && t < best)
		{
			best = t;
			hit = true;
		}
	}

	if (hit) tOut = best;
	return hit;
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

		// Lights have no mesh: pick a small box around the light's origin.
		if (node.light.has_value())
		{
			constexpr float LIGHT_PICK_HALF_EXTENT = 0.25f;
			AABB box;
			box.expand(glm::vec3(-LIGHT_PICK_HALF_EXTENT));
			box.expand(glm::vec3(LIGHT_PICK_HALF_EXTENT));
			glm::mat4 at = glm::translate(glm::mat4{1.0f},
										  glm::vec3(node.worldTransform[3]));
			float t = 0.0f;
			if (ray_aabb(ray, box, at, t) && t < closestT)
			{
				closestT = t;
				closestNode = i;
			}
			continue;
		}

		if (!node.meshIndex.has_value()) continue;

		uint32_t mi = node.meshIndex.value();
		if (mi >= meshes.size()) continue;

		// The box only rejects: overlapping boxes must not decide the winner.
		float boxT = 0.0f;
		float t = 0.0f;
		if (ray_aabb(ray, meshes[mi].localBounds, node.worldTransform, boxT) &&
			boxT < closestT &&
			ray_triangles(ray, meshes[mi], node.worldTransform, t) &&
			t < closestT)
		{
			closestT = t;
			closestNode = i;
		}
	}

	selectedNode = closestNode;
}
