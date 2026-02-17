#pragma once

#include <vulkan/vulkan.h>

struct Renderer;
struct LightEnvironment;

struct DebugWindow
{
	void draw(Renderer& renderer, LightEnvironment& lights);
};
