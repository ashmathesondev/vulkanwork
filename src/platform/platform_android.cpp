#ifdef __ANDROID__

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/input.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android_native_app_glue.h>
#include <imgui.h>
#include <imgui_impl_android.h>
#include <vulkan/vulkan_android.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "platform.h"

namespace platform
{
namespace
{
android_app* app = nullptr;
bool closeRequested = false;
bool leftDown = false;
bool rightDown = false;
double cursorX = 0.0;
double cursorY = 0.0;
void* resizeUser = nullptr;
void (*resizeCallback)(void*) = nullptr;

void copy_asset(const char* assetName, const std::filesystem::path& outPath)
{
	AAsset* asset = AAssetManager_open(app->activity->assetManager, assetName,
									   AASSET_MODE_STREAMING);
	if (!asset)
		throw std::runtime_error(std::string("Missing APK asset: ") +
								 assetName);

	std::filesystem::create_directories(outPath.parent_path());
	FILE* out = std::fopen(outPath.string().c_str(), "wb");
	if (!out)
	{
		AAsset_close(asset);
		throw std::runtime_error("Cannot write asset copy: " +
								 outPath.string());
	}

	char buffer[64 * 1024];
	for (;;)
	{
		int read = AAsset_read(asset, buffer, sizeof(buffer));
		if (read <= 0) break;
		std::fwrite(buffer, 1, static_cast<size_t>(read), out);
	}
	std::fclose(out);
	AAsset_close(asset);
}

int32_t handle_input(android_app*, AInputEvent* event)
{
	ImGui_ImplAndroid_HandleInputEvent(event);
	if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;

	int32_t action = AMotionEvent_getAction(event);
	int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
	cursorX = AMotionEvent_getX(event, 0);
	cursorY = AMotionEvent_getY(event, 0);

	if (masked == AMOTION_EVENT_ACTION_DOWN)
		leftDown = true;
	else if (masked == AMOTION_EVENT_ACTION_UP ||
			 masked == AMOTION_EVENT_ACTION_CANCEL)
		leftDown = false;
	else if (masked == AMOTION_EVENT_ACTION_POINTER_DOWN)
		rightDown = true;
	else if (masked == AMOTION_EVENT_ACTION_POINTER_UP)
		rightDown = false;
	return 1;
}

void handle_cmd(android_app*, int32_t cmd)
{
	if (cmd == APP_CMD_TERM_WINDOW)
	{
		if (resizeCallback) resizeCallback(resizeUser);
	}
	else if (cmd == APP_CMD_INIT_WINDOW || cmd == APP_CMD_WINDOW_RESIZED ||
			 cmd == APP_CMD_CONFIG_CHANGED)
	{
		if (resizeCallback) resizeCallback(resizeUser);
	}
	else if (cmd == APP_CMD_DESTROY)
	{
		closeRequested = true;
	}
}
}  // namespace

void set_android_app(android_app* androidApp)
{
	app = androidApp;
	app->onAppCmd = handle_cmd;
	app->onInputEvent = handle_input;
}

bool init() { return app != nullptr; }
void shutdown() {}

PlatformWindow* create_window(int, int, const char*)
{
	while (!app->destroyRequested && app->window == nullptr) poll_events();
	return app->window;
}

void destroy_window(PlatformWindow*) {}
bool should_close(PlatformWindow*)
{
	return closeRequested || app->destroyRequested;
}
void request_close(PlatformWindow*) { closeRequested = true; }

void poll_events()
{
	int events = 0;
	android_poll_source* source = nullptr;
	while (ALooper_pollOnce(0, nullptr, &events,
							reinterpret_cast<void**>(&source)) >= 0)
	{
		if (source) source->process(app, source);
		if (app->destroyRequested)
		{
			closeRequested = true;
			break;
		}
	}
}

void wait_events()
{
	int events = 0;
	android_poll_source* source = nullptr;
	ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void**>(&source));
	if (source) source->process(app, source);
}

double time_seconds() { return static_cast<double>(ImGui::GetTime()); }

void framebuffer_size(PlatformWindow* window, int& width, int& height)
{
	width = ANativeWindow_getWidth(window);
	height = ANativeWindow_getHeight(window);
}

void set_resize_callback(PlatformWindow*, void* user, void (*callback)(void*))
{
	resizeUser = user;
	resizeCallback = callback;
}

std::vector<const char*> required_vulkan_instance_extensions()
{
	return {VK_KHR_SURFACE_EXTENSION_NAME,
			VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
}

VkResult create_vulkan_surface(VkInstance instance, PlatformWindow* window,
							   VkSurfaceKHR* surface)
{
	VkAndroidSurfaceCreateInfoKHR ci{
		VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
	ci.window = window;
	return vkCreateAndroidSurfaceKHR(instance, &ci, nullptr, surface);
}

bool key_down(PlatformWindow*, Key) { return false; }
bool mouse_down(PlatformWindow*, MouseButton button)
{
	return button == MouseButton::Left ? leftDown : rightDown;
}
void cursor_pos(PlatformWindow*, double& x, double& y)
{
	x = cursorX;
	y = cursorY;
}
void set_mouse_capture(PlatformWindow*, bool) {}

void imgui_init(PlatformWindow* window) { ImGui_ImplAndroid_Init(window); }
void imgui_new_frame() { ImGui_ImplAndroid_NewFrame(); }
void imgui_shutdown() { ImGui_ImplAndroid_Shutdown(); }

std::string config_dir()
{
	return std::filesystem::path(app->activity->internalDataPath).string();
}

std::string pak_file_path()
{
	return (std::filesystem::path(config_dir()) / "assets.pak").string();
}

std::string model_path(const char* name)
{
	return (std::filesystem::path(config_dir()) / "models" / name).string();
}

void prepare_android_assets()
{
	copy_asset("assets.pak", pak_file_path());
	copy_asset("models/DamagedHelmet.glb", model_path("DamagedHelmet.glb"));
}

}  // namespace platform

#endif
