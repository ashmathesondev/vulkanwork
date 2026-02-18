#pragma once

#include <vulkan/vulkan.h>

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
	bool deleteRequested = false;
};
