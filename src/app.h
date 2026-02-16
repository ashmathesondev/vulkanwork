#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <string>

#include "camera.h"
#include "debugWindow.h"
#include "renderer.h"

struct App
{
	static constexpr int INITIAL_WIDTH = 1280;
	static constexpr int INITIAL_HEIGHT = 720;

	// Window
	GLFWwindow* window = nullptr;

	// Renderer
	Renderer renderer;

	// ImGui
	VkDescriptorPool imguiPool = VK_NULL_HANDLE;

	// Debug UI
	DebugWindow debugWindow;

	// State
	bool mouseCaptured = false;
	bool firstMouse = true;
	double lastMouseX = 0.0;
	double lastMouseY = 0.0;
	Camera camera;
	float deltaTime = 0.0f;
	float lastFrameTime = 0.0f;

	// Model path
	std::string modelPath;

	// --- Public API ----------------------------------------------------------
	void run();

   private:
	void init_window();
	void init_vulkan();
	void main_loop();
	void cleanup();
	void init_imgui();
	void process_input();
};
