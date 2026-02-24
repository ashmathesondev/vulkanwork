#include "app.h"

#include <ImGuiFileDialog.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <glm/glm.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "config.h"
#include "editor/sceneFile.h"
#include "logger.h"

// =============================================================================
// Macros
// =============================================================================

#define VK_CHECK(x)                                                          \
	do                                                                       \
	{                                                                        \
		VkResult _r = (x);                                                   \
		if (_r != VK_SUCCESS)                                                \
		{                                                                    \
			std::fprintf(stderr, "Vulkan error %d at %s:%d\n", _r, __FILE__, \
						 __LINE__);                                          \
			std::abort();                                                    \
		}                                                                    \
	} while (0)

// =============================================================================
// App -- top-level flow
// =============================================================================

void App::run()
{
	init_window();
	init_vulkan();
	main_loop();
	cleanup();
}

void App::init_window()
{
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	int x, y, w = INITIAL_WIDTH, h = INITIAL_HEIGHT;
	bool hasConfig = load_window_config(x, y, w, h);

	window = glfwCreateWindow(w, h, "vulkanwork", nullptr, nullptr);

	if (hasConfig) glfwSetWindowPos(window, x, y);

	glfwSetWindowUserPointer(window, this);
	glfwSetFramebufferSizeCallback(
		window,
		[](GLFWwindow* w, int, int)
		{
			static_cast<App*>(glfwGetWindowUserPointer(w))
				->renderer.notify_resize();
		});
}

void App::init_vulkan()
{
	renderer.init(window, modelPath);
	build_scene_graph();
	init_imgui();

	// Initialize default lights (matching previous hardcoded directional)
	DirectionalLight sun;
	sun.direction = glm::normalize(glm::vec3(0.5f, -1.0f, 0.3f));
	sun.color = glm::vec3(1.0f);
	sun.intensity = 3.0f;
	lights.directionals.push_back(sun);

	lights.ambient.color = glm::vec3(1.0f);
	lights.ambient.intensity = 0.03f;
}

void App::main_loop()
{
	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		float now = static_cast<float>(glfwGetTime());
		deltaTime = now - lastFrameTime;
		lastFrameTime = now;

		process_input();

		// --- ImGui frame -----------------------------------------------------
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("New Scene")) new_scene();
				if (ImGui::MenuItem("Load Scene...")) showLoadDialog_ = true;
				if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
				{
					if (currentScenePath.empty())
						showSaveDialog_ = true;
					else
						do_save_scene(currentScenePath);
				}
				if (ImGui::MenuItem("Save Scene As...")) showSaveDialog_ = true;
				ImGui::Separator();
				if (ImGui::MenuItem("Import Mesh...")) showImportDialog_ = true;
				ImGui::Separator();
				if (ImGui::MenuItem("Exit"))
					glfwSetWindowShouldClose(window, true);
				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}

		// Ctrl+S shortcut
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
		{
			if (currentScenePath.empty())
				showSaveDialog_ = true;
			else
				do_save_scene(currentScenePath);
		}

		// Open file dialogs
		if (showLoadDialog_)
		{
			IGFD::FileDialogConfig config;
			config.path = ".";
			ImGuiFileDialog::Instance()->OpenDialog("LoadScene", "Load Scene",
													".scene", config);
			showLoadDialog_ = false;
		}
		if (showSaveDialog_)
		{
			IGFD::FileDialogConfig config;
			config.path = ".";
			ImGuiFileDialog::Instance()->OpenDialog("SaveScene", "Save Scene",
													".scene", config);
			showSaveDialog_ = false;
		}
		if (showImportDialog_)
		{
			IGFD::FileDialogConfig config;
			config.path = ".";
			ImGuiFileDialog::Instance()->OpenDialog("ImportMesh", "Import Mesh",
													".gltf,.glb", config);
			showImportDialog_ = false;
		}

		// Render file dialogs
		if (ImGuiFileDialog::Instance()->Display("LoadScene"))
		{
			if (ImGuiFileDialog::Instance()->IsOk())
				do_load_scene(ImGuiFileDialog::Instance()->GetFilePathName());
			ImGuiFileDialog::Instance()->Close();
		}
		if (ImGuiFileDialog::Instance()->Display("SaveScene"))
		{
			if (ImGuiFileDialog::Instance()->IsOk())
				do_save_scene(ImGuiFileDialog::Instance()->GetFilePathName());
			ImGuiFileDialog::Instance()->Close();
		}
		if (ImGuiFileDialog::Instance()->Display("ImportMesh"))
		{
			if (ImGuiFileDialog::Instance()->IsOk())
				do_import_mesh(ImGuiFileDialog::Instance()->GetFilePathName());
			ImGuiFileDialog::Instance()->Close();
		}

		gizmo.begin_frame();

		debugWindow.draw(renderer, lights, selection, gizmo, sceneGraph);

		// Handle import/delete requests from debug window
		if (debugWindow.importRequested)
		{
			debugWindow.importRequested = false;
			showImportDialog_ = true;
		}
		if (debugWindow.deleteRequested)
		{
			debugWindow.deleteRequested = false;
			do_delete_selected();
		}

		// Delete key shortcut
		if (!ImGui::GetIO().WantCaptureKeyboard &&
			ImGui::IsKeyPressed(ImGuiKey_Delete) &&
			selection.selectedNode.has_value())
		{
			do_delete_selected();
		}

		// --- Gizmo manipulation ------------------------------------------
		if (selection.selectedNode.has_value())
		{
			uint32_t nodeIdx = selection.selectedNode.value();
			if (nodeIdx < sceneGraph.nodes.size())
			{
				auto& node = sceneGraph.nodes[nodeIdx];
				VkExtent2D ext = renderer.swapchain_extent();
				gizmo.manipulate(renderer.last_view(), renderer.last_proj(),
								 node.localTransform, 0.0f, 0.0f,
								 static_cast<float>(ext.width),
								 static_cast<float>(ext.height));
			}
		}

		// --- Sync scene graph → mesh transforms --------------------------
		sceneGraph.update_world_transforms();
		auto& meshes = renderer.meshes();
		for (const auto& node : sceneGraph.nodes)
		{
			if (node.meshIndex.has_value() &&
				node.meshIndex.value() < meshes.size())
				meshes[node.meshIndex.value()].transform = node.worldTransform;
		}

		ImGui::Render();

		// --- Draw ------------------------------------------------------------
		auto frame = renderer.begin_frame(lights);
		if (!frame) continue;  // swapchain was recreated

		float time = static_cast<float>(glfwGetTime());
		renderer.update_uniforms(camera, time, lights);
		renderer.update_debug_lines(lights);
		renderer.draw_scene(frame->cmd);

		// ImGui draws into the same render pass
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), frame->cmd);

		renderer.end_frame(*frame);
	}
	vkDeviceWaitIdle(renderer.vk_device());
}

// =============================================================================
// Input
// =============================================================================

void App::process_input()
{
	if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
		glfwSetWindowShouldClose(window, true);

	ImGuiIO& io = ImGui::GetIO();

	// --- Mouse look ----------------------------------------------------------
	bool wantCapture =
		glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS &&
		!io.WantCaptureMouse;
	if (wantCapture && !mouseCaptured)
	{
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		mouseCaptured = true;
		firstMouse = true;
	}
	else if (!wantCapture && mouseCaptured)
	{
		glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		mouseCaptured = false;
	}

	if (mouseCaptured)
	{
		double mx, my;
		glfwGetCursorPos(window, &mx, &my);

		if (firstMouse)
		{
			lastMouseX = mx;
			lastMouseY = my;
			firstMouse = false;
		}

		float xoff = static_cast<float>(mx - lastMouseX) * camera.sensitivity;
		float yoff = static_cast<float>(lastMouseY - my) * camera.sensitivity;
		lastMouseX = mx;
		lastMouseY = my;

		camera.yaw += xoff;
		camera.pitch += yoff;
		if (camera.pitch > 89.0f) camera.pitch = 89.0f;
		if (camera.pitch < -89.0f) camera.pitch = -89.0f;

		glm::vec3 d;
		d.x = std::cos(glm::radians(camera.yaw)) *
			  std::cos(glm::radians(camera.pitch));
		d.y = std::sin(glm::radians(camera.pitch));
		d.z = std::sin(glm::radians(camera.yaw)) *
			  std::cos(glm::radians(camera.pitch));
		camera.front = glm::normalize(d);
	}

	// --- Left-click picking --------------------------------------------------
	if (!io.WantCaptureMouse && !gizmo.is_using())
	{
		if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS &&
			!leftClickHeld)
		{
			leftClickHeld = true;
			double mx, my;
			glfwGetCursorPos(window, &mx, &my);
			VkExtent2D ext = renderer.swapchain_extent();
			selection.pick(static_cast<float>(mx), static_cast<float>(my),
						   static_cast<float>(ext.width),
						   static_cast<float>(ext.height), renderer.last_view(),
						   renderer.last_proj(), sceneGraph, renderer.meshes());
		}
	}
	if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_RELEASE)
		leftClickHeld = false;

	// --- Keyboard movement ---------------------------------------------------
	if (!io.WantCaptureKeyboard)
	{
		float v = camera.speed * deltaTime;
		if (mouseCaptured)
		{
			// WASD camera movement only when right-click is held
			if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
				camera.position += camera.front * v;
			if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
				camera.position -= camera.front * v;
			if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
				camera.position -=
					glm::normalize(glm::cross(camera.front, camera.up)) * v;
			if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
				camera.position +=
					glm::normalize(glm::cross(camera.front, camera.up)) * v;
		}
		else
		{
			// Gizmo mode shortcuts (only when camera not captured)
			if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
				gizmo.operation = Gizmo::Op::Translate;
			if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS)
				gizmo.operation = Gizmo::Op::Rotate;
			if (glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS)
				gizmo.operation = Gizmo::Op::Scale;
		}
		if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
			camera.position += camera.up * v;
		if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
			camera.position -= camera.up * v;
	}
}

// =============================================================================
// Scene graph
// =============================================================================

void App::build_scene_graph()
{
	const auto& meshes = renderer.meshes();
	LOG_INFO("build_scene_graph: %zu meshes", meshes.size());
	for (uint32_t i = 0; i < static_cast<uint32_t>(meshes.size()); ++i)
	{
		std::string name = meshes[i].name;
		if (name.empty()) name = "Mesh " + std::to_string(i);
		LOG_INFO("  mesh[%u] '%s'", i, name.c_str());
		sceneGraph.add_node(name, meshes[i].transform, i, meshes[i].sourcePath,
							meshes[i].sourceMeshIndex, std::nullopt);
	}
}

// =============================================================================
// Scene file operations
// =============================================================================

void App::new_scene()
{
	renderer.unload_scene();
	renderer.load_scene_empty();
	sceneGraph.clear();
	build_scene_graph();
	camera = Camera{};
	lights = LightEnvironment{};
	lights.ambient.color = glm::vec3(1.0f);
	lights.ambient.intensity = 0.03f;
	DirectionalLight sun;
	sun.direction = glm::normalize(glm::vec3(0.5f, -1.0f, 0.3f));
	sun.color = glm::vec3(1.0f);
	sun.intensity = 3.0f;
	lights.directionals.push_back(sun);
	selection.selectedNode.reset();
	currentScenePath.clear();
	modelPath.clear();
}

void App::do_save_scene(const std::string& path)
{
	LOG_INFO("Saving scene to: %s", path.c_str());
	SceneFileData data;
	data.modelPath = modelPath;
	data.sceneGraph = sceneGraph;
	data.camera = camera;
	data.lights = lights;
	if (save_scene_file(path, data))
	{
		currentScenePath = path;
		LOG_INFO("Scene saved successfully");
	}
	else
	{
		LOG_ERROR("Failed to save scene: %s", path.c_str());
	}
}

void App::do_load_scene(const std::string& path)
{
	LOG_INFO("Loading scene: %s", path.c_str());

	SceneFileData data;
	if (!load_scene_file(path, data))
	{
		LOG_ERROR("load_scene_file failed for: %s", path.c_str());
		return;
	}

	LOG_INFO("Scene file parsed. modelPath='%s', nodes=%zu",
			 data.modelPath.c_str(), data.sceneGraph.nodes.size());

	renderer.unload_scene();
	renderer.load_scene_empty();  // Loads the default cube at index 0

	// Track loaded models to avoid double-loading and to compute offsets
	std::unordered_map<std::string, uint32_t> modelOffsets;
	modelOffsets["internal://cube"] = 0;

	// For each node, ensure its model is loaded and its meshIndex is re-mapped
	for (auto& node : data.sceneGraph.nodes)
	{
		if (!node.meshIndex.has_value()) continue;

		// Backward compatibility: if node has no modelPath, use the global one
		if (node.modelPath.empty())
		{
			node.modelPath = data.modelPath;
			node.meshIndexInModel = node.meshIndex.value();
		}

		if (node.modelPath.empty()) continue;

		if (modelOffsets.find(node.modelPath) == modelOffsets.end())
		{
			LOG_INFO("Loading model dependency: %s", node.modelPath.c_str());
			uint32_t offset = static_cast<uint32_t>(renderer.meshes().size());
			try
			{
				renderer.import_gltf(node.modelPath);
				modelOffsets[node.modelPath] = offset;
			}
			catch (const std::exception& e)
			{
				LOG_ERROR("Failed to load model '%s': %s",
						  node.modelPath.c_str(), e.what());
				node.meshIndex.reset();
				continue;
			}
		}

		node.meshIndex = modelOffsets[node.modelPath] + node.meshIndexInModel;
	}

	sceneGraph = std::move(data.sceneGraph);
	camera = data.camera;
	lights = data.lights;
	modelPath = data.modelPath;

	// Sync scene graph → mesh transforms
	sceneGraph.update_world_transforms();
	auto& meshes = renderer.meshes();
	LOG_INFO("Post-load: %zu scene nodes, %zu renderer meshes",
			 sceneGraph.nodes.size(), meshes.size());

	for (const auto& node : sceneGraph.nodes)
	{
		if (!node.meshIndex.has_value()) continue;

		uint32_t mi = node.meshIndex.value();
		if (mi < meshes.size())
		{
			meshes[mi].transform = node.worldTransform;
		}
		else
		{
			LOG_WARN(
				"Node '%s' has meshIndex=%u but only %zu meshes loaded — "
				"object will not render",
				node.name.c_str(), mi, meshes.size());
		}
	}

	selection.selectedNode.reset();
	currentScenePath = path;
	LOG_INFO("Scene load complete");
}

void App::do_import_mesh(const std::string& path)
{
	LOG_INFO("Importing mesh: %s", path.c_str());
	uint32_t prevMeshCount = static_cast<uint32_t>(renderer.meshes().size());
	renderer.import_gltf(path);

	const auto& meshes = renderer.meshes();
	uint32_t newMeshCount = static_cast<uint32_t>(meshes.size());
	LOG_INFO("Import complete: %u new mesh(es) added (total %u)",
			 newMeshCount - prevMeshCount, newMeshCount);

	for (uint32_t i = prevMeshCount; i < newMeshCount; ++i)
	{
		std::string name = meshes[i].name;
		if (name.empty()) name = "Mesh " + std::to_string(i);
		LOG_INFO("  Added mesh[%u] '%s'", i, name.c_str());
		sceneGraph.add_node(name, meshes[i].transform, i, meshes[i].sourcePath,
							meshes[i].sourceMeshIndex, std::nullopt);
	}

	sceneGraph.update_world_transforms();
}

void App::do_delete_selected()
{
	if (!selection.selectedNode.has_value()) return;
	uint32_t nodeIdx = selection.selectedNode.value();
	if (nodeIdx >= sceneGraph.nodes.size()) return;

	auto meshIdx = sceneGraph.nodes[nodeIdx].meshIndex;

	// Collect all mesh indices that will be removed (node + descendants)
	std::vector<uint32_t> meshIndicesToRemove;
	{
		std::vector<uint32_t> nodesToVisit;
		nodesToVisit.push_back(nodeIdx);
		for (size_t i = 0; i < nodesToVisit.size(); ++i)
			for (uint32_t child : sceneGraph.nodes[nodesToVisit[i]].children)
				nodesToVisit.push_back(child);
		for (uint32_t n : nodesToVisit)
			if (sceneGraph.nodes[n].meshIndex.has_value())
				meshIndicesToRemove.push_back(
					sceneGraph.nodes[n].meshIndex.value());
	}

	// Remove the node (and descendants) from scene graph
	sceneGraph.remove_node(nodeIdx);

	// Sort mesh indices descending so we delete from back to front
	std::sort(meshIndicesToRemove.begin(), meshIndicesToRemove.end(),
			  std::greater<uint32_t>());

	// Delete each mesh from renderer (each call compacts and fixes indices)
	for (uint32_t mi : meshIndicesToRemove) renderer.delete_mesh(mi);

	// Fix up scene graph meshIndex values for shifted mesh indices
	// After each delete_mesh, meshes shift down. We need to recompute.
	// Since we deleted in descending order, we can compute the cumulative
	// shift.
	for (auto& node : sceneGraph.nodes)
	{
		if (!node.meshIndex.has_value()) continue;
		uint32_t oldIdx = node.meshIndex.value();
		uint32_t shift = 0;
		for (uint32_t removed : meshIndicesToRemove)
			if (removed <= oldIdx) ++shift;
		node.meshIndex = oldIdx - shift;
	}

	selection.selectedNode.reset();

	// Re-sync transforms
	sceneGraph.update_world_transforms();
	auto& meshes = renderer.meshes();
	for (const auto& node : sceneGraph.nodes)
	{
		if (node.meshIndex.has_value() &&
			node.meshIndex.value() < meshes.size())
			meshes[node.meshIndex.value()].transform = node.worldTransform;
	}
}

// =============================================================================
// ImGui
// =============================================================================

void App::init_imgui()
{
	VkDescriptorPoolSize poolSizes[] = {
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10},
	};
	VkDescriptorPoolCreateInfo poolCI{
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	poolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	poolCI.maxSets = 10;
	poolCI.poolSizeCount = 1;
	poolCI.pPoolSizes = poolSizes;
	VK_CHECK(vkCreateDescriptorPool(renderer.vk_device(), &poolCI, nullptr,
									&imguiPool));

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	ImGui_ImplGlfw_InitForVulkan(window, true);

	ImGui_ImplVulkan_InitInfo initInfo{};
	initInfo.Instance = renderer.vk_instance();
	initInfo.PhysicalDevice = renderer.vk_physical_device();
	initInfo.Device = renderer.vk_device();
	initInfo.QueueFamily = renderer.vk_graphics_family();
	initInfo.Queue = renderer.vk_graphics_queue();
	initInfo.DescriptorPool = imguiPool;
	initInfo.MinImageCount = 2;
	initInfo.ImageCount = renderer.swapchain_image_count();
	initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	initInfo.RenderPass = renderer.vk_render_pass();
	initInfo.Subpass = 0;

	ImGui_ImplVulkan_Init(&initInfo);
}

// =============================================================================
// Cleanup
// =============================================================================

void App::cleanup()
{
	save_window_config();

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	vkDestroyDescriptorPool(renderer.vk_device(), imguiPool, nullptr);

	renderer.cleanup();

	glfwDestroyWindow(window);
	glfwTerminate();
}

// =============================================================================
// Window config persistence
// =============================================================================

bool App::load_window_config(int& x, int& y, int& w, int& h)
{
	std::ifstream f(std::string(CONFIG_DIR) + "/window.cfg");
	if (!f) return false;
	int fx, fy, fw, fh;
	if (!(f >> fx >> fy >> fw >> fh)) return false;
	if (fw <= 0 || fh <= 0) return false;
	x = fx;
	y = fy;
	w = fw;
	h = fh;
	return true;
}

void App::save_window_config()
{
	if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) return;

	int x, y, w, h;
	glfwGetWindowPos(window, &x, &y);
	glfwGetWindowSize(window, &w, &h);

	std::ofstream f(std::string(CONFIG_DIR) + "/window.cfg");
	if (f) f << x << ' ' << y << ' ' << w << ' ' << h << '\n';
}
