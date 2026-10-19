#include "sceneGraph.h"

#include <algorithm>
#include <cmath>
#include <functional>

const char* light_type_name(LightType type)
{
	switch (type)
	{
		case LightType::Directional:
			return "Directional";
		case LightType::Point:
			return "Point";
		case LightType::Spot:
			return "Spot";
	}
	return "Unknown";
}

glm::mat4 make_light_transform(const glm::vec3& position,
							   const glm::vec3& direction)
{
	float len2 = glm::dot(direction, direction);
	glm::vec3 forward = len2 > 1e-12f ? direction * (1.0f / std::sqrt(len2))
									  : glm::vec3(0.0f, -1.0f, 0.0f);
	glm::vec3 z = -forward;
	glm::vec3 up = std::abs(z.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f)
										 : glm::vec3(1.0f, 0.0f, 0.0f);
	glm::vec3 x = glm::normalize(glm::cross(up, z));
	glm::vec3 y = glm::cross(z, x);

	glm::mat4 m{1.0f};
	m[0] = glm::vec4(x, 0.0f);
	m[1] = glm::vec4(y, 0.0f);
	m[2] = glm::vec4(z, 0.0f);
	m[3] = glm::vec4(position, 1.0f);
	return m;
}

uint32_t SceneGraph::add_node(const std::string& name,
							  const glm::mat4& localTransform,
							  std::optional<uint32_t> meshIndex,
							  const std::string& modelPath,
							  uint32_t meshIndexInModel,
							  std::optional<uint32_t> parentId)
{
	uint32_t idx = static_cast<uint32_t>(nodes.size());

	SceneNode node;
	node.name = name;
	node.localTransform = localTransform;
	node.worldTransform = localTransform;
	node.meshIndex = meshIndex;
	node.modelPath = modelPath;
	node.meshIndexInModel = meshIndexInModel;
	node.parent = parentId;

	nodes.push_back(std::move(node));

	if (parentId.has_value())
		nodes[parentId.value()].children.push_back(idx);
	else
		roots.push_back(idx);

	return idx;
}

uint32_t SceneGraph::add_light_node(const std::string& name,
									const LightComponent& light,
									const glm::mat4& localTransform,
									std::optional<uint32_t> parentId)
{
	uint32_t idx =
		add_node(name, localTransform, std::nullopt, {}, 0, parentId);
	nodes[idx].light = light;
	return idx;
}

void SceneGraph::remove_node(uint32_t nodeIdx)
{
	if (nodeIdx >= nodes.size()) return;

	// Collect this node and all descendants (BFS)
	std::vector<uint32_t> toRemove;
	toRemove.push_back(nodeIdx);
	for (size_t i = 0; i < toRemove.size(); ++i)
		for (uint32_t child : nodes[toRemove[i]].children)
			toRemove.push_back(child);

	// Sort descending so we can erase from back to front
	std::sort(toRemove.begin(), toRemove.end(), std::greater<uint32_t>());

	// Detach the top-level node from its parent or roots
	auto& topNode = nodes[nodeIdx];
	if (topNode.parent.has_value())
	{
		auto& parentChildren = nodes[topNode.parent.value()].children;
		parentChildren.erase(
			std::remove(parentChildren.begin(), parentChildren.end(), nodeIdx),
			parentChildren.end());
	}
	else
	{
		roots.erase(std::remove(roots.begin(), roots.end(), nodeIdx),
					roots.end());
	}

	// Erase nodes from back to front
	for (uint32_t idx : toRemove) nodes.erase(nodes.begin() + idx);

	// Build a remap table: old index -> new index
	// Since we removed sorted-descending, we can compute shifts
	auto compute_new_index = [&](uint32_t oldIdx) -> uint32_t
	{
		uint32_t shift = 0;
		for (uint32_t removed : toRemove)
			if (removed < oldIdx) ++shift;
		return oldIdx - shift;
	};

	// Fix up all parent/children indices and roots
	for (auto& node : nodes)
	{
		if (node.parent.has_value())
			node.parent = compute_new_index(node.parent.value());
		for (auto& child : node.children) child = compute_new_index(child);
	}
	for (auto& root : roots) root = compute_new_index(root);
}

void SceneGraph::clear()
{
	nodes.clear();
	roots.clear();
}

void SceneGraph::update_world_transforms()
{
	for (uint32_t root : roots) update_node(root, glm::mat4{1.0f});
}

void SceneGraph::update_node(uint32_t nodeIdx, const glm::mat4& parentWorld)
{
	auto& node = nodes[nodeIdx];
	node.worldTransform = parentWorld * node.localTransform;
	for (uint32_t child : node.children)
		update_node(child, node.worldTransform);
}

void SceneGraph::collect_lights(LightEnvironment& env) const
{
	env.directionals.clear();
	env.points.clear();
	env.spots.clear();

	for (const auto& node : nodes)
	{
		if (!node.light.has_value()) continue;
		const LightComponent& l = node.light.value();

		glm::vec3 position = glm::vec3(node.worldTransform[3]);
		// -Z axis, normalized so node scale never skews the direction.
		glm::vec3 zAxis = glm::vec3(node.worldTransform[2]);
		float zLen2 = glm::dot(zAxis, zAxis);
		glm::vec3 direction = zLen2 > 1e-12f
								  ? -zAxis * (1.0f / std::sqrt(zLen2))
								  : glm::vec3(0.0f, -1.0f, 0.0f);

		switch (l.type)
		{
			case LightType::Directional:
			{
				DirectionalLight d;
				d.position = position;
				d.direction = direction;
				d.color = l.color;
				d.intensity = l.intensity;
				env.directionals.push_back(d);
				break;
			}
			case LightType::Point:
			{
				PointLight p;
				p.position = position;
				p.color = l.color;
				p.intensity = l.intensity;
				p.radius = l.radius;
				env.points.push_back(p);
				break;
			}
			case LightType::Spot:
			{
				SpotLight sp;
				sp.position = position;
				sp.direction = direction;
				sp.color = l.color;
				sp.intensity = l.intensity;
				sp.radius = l.radius;
				sp.innerConeAngle = l.innerConeAngle;
				sp.outerConeAngle = l.outerConeAngle;
				env.spots.push_back(sp);
				break;
			}
		}
	}
}
