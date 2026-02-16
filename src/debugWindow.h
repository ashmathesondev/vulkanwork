#pragma once

#include <vulkan/vulkan.h>

struct Renderer;

struct DebugWindow
{
	void draw(const Renderer& renderer);
};
