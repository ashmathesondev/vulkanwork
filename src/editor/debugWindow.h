#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "graphics/material.h"

struct Renderer;
struct LightEnvironment;
struct Selection;
struct Gizmo;
struct SceneGraph;

struct DebugWindow
{
	void draw(Renderer& renderer, LightEnvironment& lights,
			  Selection& selection, Gizmo& gizmo, SceneGraph& sceneGraph);

	bool importRequested = false;
	bool importSplatRequested = false;
	bool deleteRequested = false;
	// Material index and slot whose map the user wants to replace
	std::optional<std::pair<uint32_t, TextureSlot>> textureBrowseRequested;
	std::string textureError;  // shown under the maps until the next browse

   private:
	void draw_node_tree(SceneGraph& sceneGraph, uint32_t nodeIdx,
						Selection& selection, const std::vector<bool>& reveal,
						bool scrollToSelected);
	void begin_rename(const SceneGraph& sceneGraph, uint32_t nodeIdx);

	// Selection seen last frame; a change (e.g. viewport pick) expands the
	// tree down to the new selection and scrolls it into view.
	std::optional<uint32_t> lastSelected_;

	// Inline rename in the tree (F2, double-click, or context menu)
	std::optional<uint32_t> renamingNode_;
	bool renameFocusPending_ = false;
	std::array<char, 256> renameBuf_{};

	// Name field in the inspector
	std::optional<uint32_t> nameFieldNode_;
	bool nameFieldActive_ = false;
	std::array<char, 256> nameBuf_{};
};
