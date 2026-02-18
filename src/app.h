#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <string>

#include "debugWindow.h"
#include "gizmo.h"
#include "graphics/camera.h"
#include "graphics/light.h"
#include "graphics/renderer.h"
#include "sceneGraph.h"
#include "selection.h"

struct App
{
	static constexpr int INITIAL_WIDTH = 1280;
	static constexpr int INITIAL_HEIGHT = 720;

	// Window
	GLFWwindow* window = nullptr;

	// Renderer
	Renderer renderer;

	// Scene graph
	SceneGraph sceneGraph;

	// ImGui
	VkDescriptorPool imguiPool = VK_NULL_HANDLE;

	// Debug UI
	DebugWindow debugWindow;

	// Lights
	LightEnvironment lights;

	// Selection & Gizmo
	Selection selection;
	Gizmo gizmo;

	// State
	bool mouseCaptured = false;
	bool leftClickHeld = false;
	bool firstMouse = true;
	double lastMouseX = 0.0;
	double lastMouseY = 0.0;
	Camera camera;
	float deltaTime = 0.0f;
	float lastFrameTime = 0.0f;

	// Model path
	std::string modelPath;

	// Scene file state
	std::string currentScenePath;

	// Import dialog state
	bool showImportDialog_ = false;

	// --- Public API ----------------------------------------------------------
	void run();

   private:
	void init_window();
	void init_vulkan();
	void main_loop();
	void cleanup();
	void init_imgui();
	void process_input();
	void build_scene_graph();
	void save_window_config();
	bool load_window_config(int& x, int& y, int& w, int& h);

	// Scene file operations
	bool showLoadDialog_ = false;
	bool showSaveDialog_ = false;
	void new_scene();
	void do_save_scene(const std::string& path);
	void do_load_scene(const std::string& path);
	void do_import_mesh(const std::string& path);
	void do_delete_selected();
};
