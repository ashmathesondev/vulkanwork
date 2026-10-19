#ifndef __ANDROID__

#include "platform.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>

#include <cstdlib>
#include <filesystem>

#include "config.h"

namespace platform
{
namespace
{
void* resizeUser = nullptr;
void (*resizeCallback)(void*) = nullptr;

int to_glfw_key(Key key)
{
	switch (key)
	{
		case Key::Escape:
			return GLFW_KEY_ESCAPE;
		case Key::W:
			return GLFW_KEY_W;
		case Key::A:
			return GLFW_KEY_A;
		case Key::S:
			return GLFW_KEY_S;
		case Key::D:
			return GLFW_KEY_D;
		case Key::E:
			return GLFW_KEY_E;
		case Key::R:
			return GLFW_KEY_R;
		case Key::Space:
			return GLFW_KEY_SPACE;
		case Key::LeftControl:
			return GLFW_KEY_LEFT_CONTROL;
		case Key::Delete:
			return GLFW_KEY_DELETE;
	}
	return GLFW_KEY_UNKNOWN;
}
}  // namespace

void set_android_app(struct android_app*) {}

bool init() { return glfwInit() == GLFW_TRUE; }

void shutdown() { glfwTerminate(); }

PlatformWindow* create_window(int width, int height, const char* title)
{
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	return glfwCreateWindow(width, height, title, nullptr, nullptr);
}

void destroy_window(PlatformWindow* window) { glfwDestroyWindow(window); }

bool should_close(PlatformWindow* window)
{
	return glfwWindowShouldClose(window) == GLFW_TRUE;
}

void request_close(PlatformWindow* window)
{
	glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void poll_events() { glfwPollEvents(); }
void wait_events() { glfwWaitEvents(); }
double time_seconds() { return glfwGetTime(); }

void framebuffer_size(PlatformWindow* window, int& width, int& height)
{
	glfwGetFramebufferSize(window, &width, &height);
}

void set_resize_callback(PlatformWindow* window, void* user,
						 void (*callback)(void*))
{
	resizeUser = user;
	resizeCallback = callback;
	glfwSetFramebufferSizeCallback(window,
								   [](GLFWwindow*, int, int)
								   {
									   if (resizeCallback)
										   resizeCallback(resizeUser);
								   });
}

std::vector<const char*> required_vulkan_instance_extensions()
{
	uint32_t count = 0;
	const char** names = glfwGetRequiredInstanceExtensions(&count);
	return {names, names + count};
}

VkResult create_vulkan_surface(VkInstance instance, PlatformWindow* window,
							   VkSurfaceKHR* surface)
{
	return glfwCreateWindowSurface(instance, window, nullptr, surface);
}

bool key_down(PlatformWindow* window, Key key)
{
	return glfwGetKey(window, to_glfw_key(key)) == GLFW_PRESS;
}

bool mouse_down(PlatformWindow* window, MouseButton button)
{
	int glfwButton = button == MouseButton::Left ? GLFW_MOUSE_BUTTON_LEFT
												 : GLFW_MOUSE_BUTTON_RIGHT;
	return glfwGetMouseButton(window, glfwButton) == GLFW_PRESS;
}

void cursor_pos(PlatformWindow* window, double& x, double& y)
{
	glfwGetCursorPos(window, &x, &y);
}

void set_mouse_capture(PlatformWindow* window, bool captured)
{
	glfwSetInputMode(window, GLFW_CURSOR,
					 captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void imgui_init(PlatformWindow* window)
{
	ImGui_ImplGlfw_InitForVulkan(window, true);
}

void imgui_new_frame() { ImGui_ImplGlfw_NewFrame(); }
void imgui_shutdown() { ImGui_ImplGlfw_Shutdown(); }

std::string config_dir() { return CONFIG_DIR; }
std::string pak_file_path() { return PAK_FILE; }
std::string model_path(const char* name)
{
	return (std::filesystem::path(MODEL_DIR) / name).string();
}
void prepare_android_assets() {}

}  // namespace platform

#endif
