#pragma once

#ifdef __ANDROID__
#include <android/native_window.h>
using PlatformWindow = ANativeWindow;
#else
#ifndef GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_VULKAN
#endif
#include <GLFW/glfw3.h>
using PlatformWindow = GLFWwindow;
#endif

struct android_app;

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace platform
{

void set_android_app(struct android_app* app);
bool init();
void shutdown();

PlatformWindow* create_window(int width, int height, const char* title);
void destroy_window(PlatformWindow* window);
bool should_close(PlatformWindow* window);
void request_close(PlatformWindow* window);
void poll_events();
void wait_events();
double time_seconds();
void framebuffer_size(PlatformWindow* window, int& width, int& height);
void set_resize_callback(PlatformWindow* window, void* user,
						 void (*callback)(void*));

std::vector<const char*> required_vulkan_instance_extensions();
VkResult create_vulkan_surface(VkInstance instance, PlatformWindow* window,
							   VkSurfaceKHR* surface);

enum class Key
{
	Escape,
	W,
	A,
	S,
	D,
	E,
	R,
	Space,
	LeftControl,
	Delete
};

enum class MouseButton
{
	Left,
	Right
};

bool key_down(PlatformWindow* window, Key key);
bool mouse_down(PlatformWindow* window, MouseButton button);
void cursor_pos(PlatformWindow* window, double& x, double& y);
void set_mouse_capture(PlatformWindow* window, bool captured);

void imgui_init(PlatformWindow* window);
void imgui_new_frame();
void imgui_shutdown();

std::string config_dir();
std::string pak_file_path();
std::string model_path(const char* name);
void prepare_android_assets();

}  // namespace platform
