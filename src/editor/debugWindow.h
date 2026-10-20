#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>

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

   private:
	// Selection seen last frame; a change (e.g. viewport pick) expands the
	// tree down to the new selection and scrolls it into view.
	std::optional<uint32_t> lastSelected_;
};
