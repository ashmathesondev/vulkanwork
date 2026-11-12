#include "app.h"

#include <BUI_kit.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <glm/glm.hpp>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "config.h"
#include "editor/sceneFile.h"
#include "loaders/gaussianSplatLoader.h"
#include "logger.h"
#include "platform/platform.h"

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
	if (!platform::init())
		throw std::runtime_error("Failed to initialize platform");

	int x, y, w = INITIAL_WIDTH, h = INITIAL_HEIGHT;
	bool hasConfig = load_window_config(x, y, w, h);

	window = platform::create_window(w, h, "vulkanwork");
	if (!window) throw std::runtime_error("Failed to create window");

#ifndef __ANDROID__
	if (hasConfig) glfwSetWindowPos(window, x, y);
#else
	(void)x;
	(void)y;
	(void)hasConfig;
#endif

	platform::set_resize_callback(
		window, this,
		[](void* app) { static_cast<App*>(app)->renderer.notify_resize(); });
}

void App::init_vulkan()
{
	renderer.init(window, modelPath);
	build_scene_graph();
	init_ui();

	// Initialise Gaussian Splat system
	splatSystem_.emplace();
	splatSystem_->init(renderer.vk_device(), renderer.vk_physical_device(),
					   renderer.vk_command_pool_(),
					   renderer.vk_graphics_queue(), renderer.vk_render_pass(),
					   renderer.frame_set_layout(),
					   Renderer::MAX_FRAMES_IN_FLIGHT, renderer.pack_file());
	renderer.set_splat_system(&*splatSystem_);

	add_default_lights();
}

void App::main_loop()
{
	while (!platform::should_close(window))
	{
		platform::poll_events();

		float now = static_cast<float>(platform::time_seconds());
		deltaTime = now - lastFrameTime;
		lastFrameTime = now;

		process_input();

		debugWindow.draw(renderer, lights, selection, gizmo, sceneGraph);
		if (newSceneRequested_)
		{
			newSceneRequested_ = false;
			new_scene();
		}
		if (saveRequested_)
		{
			saveRequested_ = false;
			if (currentScenePath.empty())
				showSaveDialog_ = true;
			else
				do_save_scene(currentScenePath);
		}
		if (debugWindow.deleteRequested)
		{
			debugWindow.deleteRequested = false;
			do_delete_selected();
		}
		if (debugWindow.importRequested)
		{
			debugWindow.importRequested = false;
			showImportDialog_ = true;
		}
		if (debugWindow.importSplatRequested)
		{
			debugWindow.importSplatRequested = false;
			showSplatImportDialog_ = true;
		}
		auto dialog = [&](const char* title, int mode, const char* filter,
						  const std::string& dir, const std::string& file,
						  auto action)
		{
			char** paths = nullptr;
			const char* filters[] = {filter};
			int count = BK_file_dialog(title, mode, dir.c_str(), file.c_str(),
									   filters, 1, 0, &paths);
			if (count > 0 && paths && paths[0]) action(std::string(paths[0]));
			BK_file_dialog_free(paths);
			// A modal loop may have consumed the release of a viewport key.
			gizmo.cancel();
		};
		if (showLoadDialog_)
		{
			showLoadDialog_ = false;
			dialog("Load Scene", 0, "Scene\t*.scene", scene_dialog_dir(), "",
				   [this](auto p) { do_load_scene(p); });
		}
		if (showSaveDialog_)
		{
			showSaveDialog_ = false;
			dialog("Save Scene As", 1, "Scene\t*.scene", scene_dialog_dir(),
				   currentScenePath.empty()
					   ? "Untitled.scene"
					   : std::filesystem::path(currentScenePath)
							 .filename()
							 .string(),
				   [this](auto p)
				   {
					   if (std::filesystem::path(p).extension().empty())
						   p += ".scene";
					   do_save_scene(p);
				   });
		}
		if (showImportDialog_)
		{
			showImportDialog_ = false;
			dialog("Import Mesh", 0, "Meshes\t*.gltf *.glb *.fbx *.dae", ".",
				   "", [this](auto p) { do_import_mesh(p); });
		}
		if (showSplatImportDialog_)
		{
			showSplatImportDialog_ = false;
			dialog("Import Gaussian Splat", 0, "Gaussian splat\t*.ply", ".", "",
				   [this](auto p) { do_import_splat(p); });
		}
		if (debugWindow.textureBrowseRequested)
		{
			textureTarget_ = *debugWindow.textureBrowseRequested;
			debugWindow.textureBrowseRequested.reset();
			debugWindow.textureError.clear();
			dialog("Select Texture", 0,
				   "Images\t*.png *.jpg *.jpeg *.tga *.bmp", ".", "",
				   [this](auto p) { do_pick_texture(p); });
		}
		gizmo.begin_frame();
		double mx, my;
		platform::cursor_pos(window, mx, my);
		int ww, wh;
		glfwGetWindowSize(window, &ww, &wh);
		int fw, fh;
		platform::framebuffer_size(window, fw, fh);
		renderer.set_editor_width(
			ww > 0 ? uint32_t(double(debugWindow.pane_width()) * fw / ww) : 0);
		ww = std::max(1, ww - debugWindow.pane_width());
		auto events = platform::take_pointer_events();
		events.push_back(
			{mx, my, platform::mouse_down(window, platform::MouseButton::Left),
			 false});
		for (const auto& event : events)
		{
			gizmo.begin_frame();
			bool inViewport =
				event.x >= 0 && event.x < ww && event.y >= 0 && event.y < wh;
			bool enabled = !mouseCaptured && !debugWindow.captures_keyboard() &&
						   (inViewport || gizmo.is_using());
			gizmo.input(glm::vec2(event.x, event.y), event.down, enabled);
			// --- Gizmo manipulation ------------------------------------------
			if (selection.selectedNode.has_value())
			{
				uint32_t nodeIdx = selection.selectedNode.value();
				if (nodeIdx < sceneGraph.nodes.size())
				{
					auto& node = sceneGraph.nodes[nodeIdx];
					// The gizmo works in world space, node transforms are
					// relative to the parent.
					glm::mat4 parentWorld{1.0f};
					if (node.parent.has_value() &&
						*node.parent < sceneGraph.nodes.size())
						parentWorld =
							sceneGraph.nodes[*node.parent].worldTransform;
					glm::mat4 world = parentWorld * node.localTransform;
					bool changed = gizmo.manipulate(
						renderer.last_view(), renderer.last_proj(), world, 0.0f,
						0.0f, static_cast<float>(ww), static_cast<float>(wh));
					// A zero-scale parent has no inverse, and would write NaNs
					if (changed && std::abs(glm::determinant(parentWorld)) >
									   std::numeric_limits<float>::min())
						node.localTransform = glm::inverse(parentWorld) * world;
				}
			}

			if (event.pressed && inViewport && enabled && !gizmo.is_over() &&
				!gizmo.is_using())
				selection.pick(float(event.x), float(event.y), float(ww),
							   float(wh), renderer.last_view(),
							   renderer.last_proj(), sceneGraph,
							   renderer.meshes());
		}
		// --- Sync scene graph → mesh transforms and lights ---------------
		sceneGraph.update_world_transforms();
		sceneGraph.collect_lights(lights);
		auto& meshes = renderer.meshes();
		for (const auto& node : sceneGraph.nodes)
		{
			if (node.meshIndex.has_value() &&
				node.meshIndex.value() < meshes.size())
				meshes[node.meshIndex.value()].transform = node.worldTransform;
		}
		sync_splats();

		// --- Draw ------------------------------------------------------------
		renderer.update_shadows(camera, lights);
		auto frame = renderer.begin_frame();
		if (!frame) continue;  // swapchain was recreated

		float time = static_cast<float>(platform::time_seconds());
		renderer.update_uniforms(camera, time, lights);
		std::optional<uint32_t> selectedMesh;
		if (selection.selectedNode.has_value() &&
			*selection.selectedNode < sceneGraph.nodes.size())
			selectedMesh = sceneGraph.nodes[*selection.selectedNode].meshIndex;
		renderer.update_debug_lines(lights, selectedMesh, gizmo.lines());
		renderer.draw_scene(frame->cmd);

		renderer.end_frame(*frame);
	}
	vkDeviceWaitIdle(renderer.vk_device());
}

// =============================================================================
// Input
// =============================================================================

void App::process_input()
{
	// Quit on an Escape press in the viewport. Escape also cancels
	// text edits and dialogs; reacting to the held key would quit on the
	// frame after the edit closed.
	bool escapeDown = platform::key_down(window, platform::Key::Escape);
	if (escapeDown && !escapeWasDown && !debugWindow.captures_keyboard())
		platform::request_close(window);
	escapeWasDown = escapeDown;

	// --- Mouse look ----------------------------------------------------------
	bool wantCapture =
		platform::mouse_down(window, platform::MouseButton::Right) &&
		(mouseCaptured ||
		 (!debugWindow.captures_mouse() && !debugWindow.captures_keyboard()));
	if (wantCapture && !mouseCaptured)
	{
		platform::set_mouse_capture(window, true);
		mouseCaptured = true;
		firstMouse = true;
	}
	else if (!wantCapture && mouseCaptured)
	{
		platform::set_mouse_capture(window, false);
		mouseCaptured = false;
	}

	if (mouseCaptured)
	{
		double mx, my;
		platform::cursor_pos(window, mx, my);

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

		// Keep yaw in [-180, 180): unbounded growth makes saved scenes hard
		// to read and slowly loses float precision.
		camera.yaw = std::remainder(camera.yaw + xoff, 360.0f);
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

	// --- Keyboard movement ---------------------------------------------------
	if (!debugWindow.captures_keyboard())
	{
		float v = camera.speed * deltaTime;
		if (mouseCaptured)
		{
			// WASD camera movement only when right-click is held
			if (platform::key_down(window, platform::Key::W))
				camera.position += camera.front * v;
			if (platform::key_down(window, platform::Key::S))
				camera.position -= camera.front * v;
			if (platform::key_down(window, platform::Key::A))
				camera.position -=
					glm::normalize(glm::cross(camera.front, camera.up)) * v;
			if (platform::key_down(window, platform::Key::D))
				camera.position +=
					glm::normalize(glm::cross(camera.front, camera.up)) * v;
		}
		else
		{
			// Gizmo mode shortcuts (only when camera not captured)
			if (platform::key_down(window, platform::Key::W))
				gizmo.operation = Gizmo::Op::Translate;
			if (platform::key_down(window, platform::Key::E))
				gizmo.operation = Gizmo::Op::Rotate;
			if (platform::key_down(window, platform::Key::R))
				gizmo.operation = Gizmo::Op::Scale;
		}
		if (platform::key_down(window, platform::Key::Space))
			camera.position += camera.up * v;
		if (platform::key_down(window, platform::Key::LeftControl))
			camera.position -= camera.up * v;
	}
}

// =============================================================================
// Scene graph
// =============================================================================

void App::add_default_lights()
{
	lights = LightEnvironment{};
	lights.ambient.color = glm::vec3(1.0f);
	lights.ambient.intensity = 0.03f;

	LightComponent sun;
	sun.type = LightType::Directional;
	sun.color = glm::vec3(1.0f);
	sun.intensity = 3.0f;
	sceneGraph.add_light_node(
		"Sun", sun,
		make_light_transform(glm::vec3(0.0f, 5.0f, 0.0f),
							 glm::vec3(0.5f, -1.0f, 0.3f)));
	sceneGraph.update_world_transforms();
	sceneGraph.collect_lights(lights);
}

// Recreate an imported model's node tree in the scene graph. Returns the new
// root node indices. A SceneNode holds one mesh, so a node drawing several
// primitives keeps its transform and gets one child per primitive.
std::vector<uint32_t> App::add_imported_hierarchy(
	const std::vector<ImportedNode>& nodes)
{
	const auto& meshes = renderer.meshes();
	std::vector<uint32_t> roots;

	std::function<void(uint32_t, std::optional<uint32_t>)> add =
		[&](uint32_t idx, std::optional<uint32_t> parent)
	{
		const ImportedNode& in = nodes[idx];

		auto mesh_node = [&](const std::string& name, const glm::mat4& local,
							 uint32_t mi, std::optional<uint32_t> p)
		{
			return sceneGraph.add_node(name, local, mi, meshes[mi].sourcePath,
									   meshes[mi].sourceMeshIndex, p);
		};

		uint32_t self;
		bool singleMesh = in.meshes.size() == 1 && in.meshes[0] < meshes.size();
		if (singleMesh)
		{
			self = mesh_node(in.name, in.localTransform, in.meshes[0], parent);
		}
		else
		{
			self = sceneGraph.add_node(in.name, in.localTransform, std::nullopt,
									   {}, 0, parent);
			for (uint32_t mi : in.meshes)
			{
				if (mi >= meshes.size()) continue;
				std::string name = meshes[mi].name.empty() ? in.name + " [mesh]"
														   : meshes[mi].name;
				mesh_node(name, glm::mat4{1.0f}, mi, self);
			}
		}
		if (!parent.has_value()) roots.push_back(self);

		for (uint32_t child : in.children)
			if (child < nodes.size()) add(child, self);
	};

	for (uint32_t i = 0; i < static_cast<uint32_t>(nodes.size()); ++i)
		if (nodes[i].parent < 0) add(i, std::nullopt);

	return roots;
}

void App::build_scene_graph()
{
	const auto& meshes = renderer.meshes();
	LOG_INFO("build_scene_graph: %zu meshes", meshes.size());

	add_imported_hierarchy(renderer.initial_hierarchy());

	// Meshes outside the model's hierarchy (the built-in cube) become roots
	std::vector<bool> covered(meshes.size(), false);
	for (const auto& node : renderer.initial_hierarchy())
		for (uint32_t mi : node.meshes)
			if (mi < covered.size()) covered[mi] = true;
	for (uint32_t i = 0; i < static_cast<uint32_t>(meshes.size()); ++i)
	{
		if (covered[i]) continue;
		std::string name = meshes[i].name;
		if (name.empty()) name = "Mesh " + std::to_string(i);
		sceneGraph.add_node(name, meshes[i].transform, i, meshes[i].sourcePath,
							meshes[i].sourceMeshIndex, std::nullopt);
	}

	sceneGraph.update_world_transforms();
	LOG_INFO("build_scene_graph: %zu nodes", sceneGraph.nodes.size());
}

// =============================================================================
// Scene file operations
// =============================================================================

std::string App::scene_dialog_dir() const
{
	namespace fs = std::filesystem;
	std::error_code ec;
	if (!currentScenePath.empty())
	{
		fs::path dir = fs::path(currentScenePath).parent_path();
		if (!dir.empty() && fs::is_directory(dir, ec)) return dir.string();
	}
	if (fs::is_directory("Scenes", ec)) return "Scenes";
	return ".";
}

void App::new_scene()
{
	debugWindow.invalidate();
	gizmo.cancel();
	renderer.unload_scene();
	renderer.load_scene_empty();
	sceneGraph.clear();
	build_scene_graph();
	camera = Camera{};
	add_default_lights();
	if (splatSystem_) splatSystem_->clear();
	selection.selectedNode.reset();
	currentScenePath.clear();
	modelPath.clear();
}

void App::do_pick_texture(const std::string& path)
{
	debugWindow.invalidate();
	gizmo.cancel();
	auto [matIdx, slot] = textureTarget_;
	if (!renderer.set_material_texture(matIdx, slot, path))
	{
		debugWindow.textureError = "Could not load " + path + " (see log)";
		return;
	}

	// The emissive map is multiplied by this factor, so black would hide it.
	Material& mat = renderer.materials()[matIdx];
	if (slot == TextureSlot::Emissive && mat.emissiveFactor == glm::vec3(0.0f))
	{
		mat.emissiveFactor = glm::vec3(1.0f);
		renderer.update_material(matIdx);
	}
}

void App::do_save_scene(const std::string& path)
{
	LOG_INFO("Saving scene to: %s", path.c_str());
	SceneFileData data;
	data.modelPath = modelPath;
	data.sceneGraph = sceneGraph;
	data.camera = camera;
	LOG_INFO(
		"Saving camera: pos (%.3f, %.3f, %.3f) yaw %.2f pitch %.2f fov %.1f",
		camera.position.x, camera.position.y, camera.position.z, camera.yaw,
		camera.pitch, camera.fov);
	data.lights = lights;
	const auto& meshes = renderer.meshes();
	const auto& materials = renderer.materials();
	for (uint32_t i = 0; i < static_cast<uint32_t>(sceneGraph.nodes.size());
		 ++i)
	{
		const auto& meshIdx = sceneGraph.nodes[i].meshIndex;
		if (!meshIdx.has_value() || *meshIdx >= meshes.size()) continue;
		uint32_t matIdx = meshes[*meshIdx].materialIndex;
		if (matIdx < materials.size() && materials[matIdx].edited)
			data.nodeMaterials[i] = materials[matIdx];
	}
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
	debugWindow.invalidate();
	gizmo.cancel();
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
			bool looksLikeLegacyCube =
				node.name == "Cube" &&
				(data.modelPath.empty() || node.meshIndex.value() != 0);
			if (looksLikeLegacyCube)
			{
				node.modelPath = "internal://cube";
				node.meshIndexInModel = 0;
			}
			else
			{
				node.modelPath = data.modelPath;
				node.meshIndexInModel = node.meshIndex.value();
			}
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

	// Materials come from the source models, so saved edits go on top.
	for (const auto& [nodeIdx, params] : data.nodeMaterials)
	{
		if (nodeIdx >= sceneGraph.nodes.size()) continue;
		const auto& meshIdx = sceneGraph.nodes[nodeIdx].meshIndex;
		if (!meshIdx.has_value() || *meshIdx >= renderer.meshes().size())
			continue;
		uint32_t matIdx = renderer.meshes()[*meshIdx].materialIndex;
		if (matIdx >= renderer.materials().size()) continue;
		Material& mat = renderer.materials()[matIdx];
		static_cast<MaterialParams&>(mat) = params;
		mat.edited = true;
		renderer.update_material(matIdx);

		for (size_t i = 0; i < TEXTURE_SLOT_COUNT; ++i)
		{
			const auto& texPath = params.textureOverrides[i];
			if (texPath.has_value() &&
				!renderer.set_material_texture(
					matIdx, static_cast<TextureSlot>(i), *texPath))
				LOG_WARN("Keeping model texture, cannot load '%s'",
						 texPath->c_str());
		}
	}

	camera = data.camera;
	LOG_INFO(
		"Restored camera: pos (%.3f, %.3f, %.3f) yaw %.2f pitch %.2f fov %.1f",
		camera.position.x, camera.position.y, camera.position.z, camera.yaw,
		camera.pitch, camera.fov);
	lights = data.lights;
	modelPath = data.modelPath;

	// Gaussian splats: one cloud can be resident at a time.
	splatSystem_->clear();
	bool splatLoaded = false;
	for (auto& node : sceneGraph.nodes)
	{
		if (!node.splat.has_value()) continue;
		if (splatLoaded)
		{
			LOG_WARN("Only one Gaussian splat cloud is supported; '%s' ignored",
					 node.splat->path.c_str());
			continue;
		}
		splatLoaded = upload_splat_cloud(*node.splat);
	}

	// Sync scene graph → mesh transforms and lights
	sceneGraph.update_world_transforms();
	sceneGraph.collect_lights(lights);
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
	debugWindow.invalidate();
	gizmo.cancel();
	LOG_INFO("Importing mesh: %s", path.c_str());
	uint32_t prevMeshCount = static_cast<uint32_t>(renderer.meshes().size());
	uint32_t prevNodeCount = static_cast<uint32_t>(sceneGraph.nodes.size());
	std::vector<ImportedNode> hierarchy;
	try
	{
		hierarchy = renderer.import_gltf(path);
	}
	catch (const std::exception& e)
	{
		// A bad model file must not take down the editor and unsaved work
		LOG_ERROR("Import failed: %s", e.what());
		return;
	}

	uint32_t newMeshCount = static_cast<uint32_t>(renderer.meshes().size());
	std::vector<uint32_t> roots = add_imported_hierarchy(hierarchy);
	sceneGraph.update_world_transforms();

	LOG_INFO("Import complete: %u mesh(es), %u node(s) in %zu root(s)",
			 newMeshCount - prevMeshCount,
			 static_cast<uint32_t>(sceneGraph.nodes.size()) - prevNodeCount,
			 roots.size());
	if (!roots.empty()) selection.selectedNode = roots.front();
}

bool App::upload_splat_cloud(SplatComponent& splat)
{
	LOG_INFO("Loading Gaussian Splat: %s", splat.path.c_str());
	splat.count = 0;
	GaussianSplatCloud cloud = load_gaussian_splat_ply(splat.path);
	if (cloud.splats.empty())
	{
		LOG_WARN("Gaussian Splat load failed or empty: %s", splat.path.c_str());
		return false;
	}
	LOG_INFO("Loaded %zu splats, uploading to GPU...", cloud.splats.size());
	splatSystem_->load(cloud);
	splat.count = splatSystem_->splat_count();
	LOG_INFO("Gaussian Splat upload complete.");
	return true;
}

void App::do_import_splat(const std::string& path)
{
	debugWindow.invalidate();
	gizmo.cancel();
	SplatComponent splat;
	splat.path = path;
	if (!upload_splat_cloud(splat)) return;

	// The GPU now holds the new cloud. Drop splat links from older nodes and
	// delete those nodes when nothing else hangs off them.
	for (bool removed = true; removed;)
	{
		removed = false;
		for (uint32_t i = 0; i < static_cast<uint32_t>(sceneGraph.nodes.size());
			 ++i)
		{
			auto& node = sceneGraph.nodes[i];
			if (!node.splat.has_value()) continue;
			node.splat.reset();
			if (node.children.empty() && !node.meshIndex.has_value() &&
				!node.light.has_value())
			{
				delete_node(i);
				removed = true;
				break;	// indices shifted; rescan
			}
		}
	}

	std::string name = std::filesystem::path(path).stem().string();
	uint32_t idx = sceneGraph.add_node(name, colmap_to_y_up(), std::nullopt, {},
									   0, std::nullopt);
	sceneGraph.nodes[idx].splat = splat;
	sceneGraph.update_world_transforms();
	selection.selectedNode = idx;
}

void App::sync_splats()
{
	if (!splatSystem_) return;

	const SceneNode* active = nullptr;
	for (const auto& node : sceneGraph.nodes)
	{
		if (node.splat.has_value() && node.splat->count > 0)
		{
			active = &node;
			break;
		}
	}

	if (!active)
	{
		// The splat node was deleted: free the GPU cloud.
		if (splatSystem_->has_splats()) splatSystem_->clear();
		return;
	}

	splatSystem_->set_transform(active->worldTransform);
	splatSystem_->set_render_mode(active->splat->mode,
								  active->splat->pointSize);
}

void App::do_delete_selected()
{
	debugWindow.invalidate();
	gizmo.cancel();
	if (!selection.selectedNode.has_value()) return;
	delete_node(selection.selectedNode.value());
}

void App::delete_node(uint32_t nodeIdx)
{
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
// BUI editor
// =============================================================================

void App::init_ui()
{
	debugWindow.init(window);
	debugWindow.newScene = [this] { newSceneRequested_ = true; };
	debugWindow.loadScene = [this] { showLoadDialog_ = true; };
	debugWindow.saveScene = [this] { saveRequested_ = true; };
	debugWindow.saveAs = [this] { showSaveDialog_ = true; };
	debugWindow.exit = [this] { platform::request_close(window); };
}

// =============================================================================
// Cleanup
// =============================================================================

void App::cleanup()
{
	save_window_config();

	debugWindow.cleanup();

	if (splatSystem_) splatSystem_->cleanup();

	renderer.cleanup();

	platform::destroy_window(window);
	platform::shutdown();
}

// =============================================================================
// Window config persistence
// =============================================================================

bool App::load_window_config(int& x, int& y, int& w, int& h)
{
	std::ifstream f(platform::config_dir() + "/window.cfg");
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
	int x, y, w, h;
#ifndef __ANDROID__
	glfwGetWindowPos(window, &x, &y);
	glfwGetWindowSize(window, &w, &h);
#else
	x = 0;
	y = 0;
	platform::framebuffer_size(window, w, h);
#endif

	std::ofstream f(platform::config_dir() + "/window.cfg");
	if (f) f << x << ' ' << y << ' ' << w << ' ' << h << '\n';
}
