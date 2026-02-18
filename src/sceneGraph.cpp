#include "sceneGraph.h"

#include <algorithm>
#include <functional>

uint32_t SceneGraph::add_node(const std::string& name,
							  const glm::mat4& localTransform,
							  std::optional<uint32_t> meshIndex,
							  std::optional<uint32_t> parentId)
{
	uint32_t idx = static_cast<uint32_t>(nodes.size());

	SceneNode node;
	node.name = name;
	node.localTransform = localTransform;
	node.worldTransform = localTransform;
	node.meshIndex = meshIndex;
	node.parent = parentId;

	nodes.push_back(std::move(node));

	if (parentId.has_value())
		nodes[parentId.value()].children.push_back(idx);
	else
		roots.push_back(idx);

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
	for (uint32_t idx : toRemove)
		nodes.erase(nodes.begin() + idx);

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
		for (auto& child : node.children)
			child = compute_new_index(child);
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
	for (uint32_t root : roots)
		update_node(root, glm::mat4{1.0f});
}

void SceneGraph::update_node(uint32_t nodeIdx, const glm::mat4& parentWorld)
{
	auto& node = nodes[nodeIdx];
	node.worldTransform = parentWorld * node.localTransform;
	for (uint32_t child : node.children)
		update_node(child, node.worldTransform);
}
