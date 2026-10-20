#include "debugWindow.h"

#include <imgui.h>

// ImGuizmo.h is not self-contained and must follow imgui.h. Keep include
// sorting away from it.
// clang-format off
#include <ImGuizmo.h>
// clang-format on

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <vector>

#include "editor/gizmo.h"
#include "editor/sceneGraph.h"
#include "editor/selection.h"
#include "graphics/light.h"
#include "graphics/renderer.h"

// ImGui lets windows overlap the main menu bar, which then swallows clicks
// meant for File. Push the current window back into the viewport work area
// (the region below the menu bar). Also corrects positions saved in imgui.ini.
static void keep_below_menu_bar()
{
	const ImGuiViewport* vp = ImGui::GetMainViewport();
	ImVec2 pos = ImGui::GetWindowPos();
	if (pos.y < vp->WorkPos.y)
		ImGui::SetWindowPos(ImVec2(pos.x, vp->WorkPos.y));
}

// Copy `name` into a fixed ImGui text buffer (truncating if needed).
template <size_t N>
static void fill_buffer(std::array<char, N>& buf, const std::string& name)
{
	size_t n = std::min(name.size(), N - 1);
	std::memcpy(buf.data(), name.data(), n);
	buf[n] = '\0';
}

// Apply an edited name. Surrounding whitespace is trimmed; an empty result
// keeps the old name so nodes can never become unlabeled.
static void commit_name(SceneNode& node, const char* text)
{
	std::string s(text);
	size_t b = s.find_first_not_of(" \t");
	size_t e = s.find_last_not_of(" \t");
	if (b == std::string::npos) return;
	node.name = s.substr(b, e - b + 1);
}

void DebugWindow::begin_rename(const SceneGraph& sceneGraph, uint32_t nodeIdx)
{
	if (nodeIdx >= sceneGraph.nodes.size()) return;
	renamingNode_ = nodeIdx;
	renameFocusPending_ = true;
	fill_buffer(renameBuf_, sceneGraph.nodes[nodeIdx].name);
}

// Recursive helper to draw scene hierarchy tree.
// reveal[i] forces node i open (ancestors of a newly selected node);
// scrollToSelected scrolls the selected row into view.
void DebugWindow::draw_node_tree(SceneGraph& sceneGraph, uint32_t nodeIdx,
								 Selection& selection,
								 const std::vector<bool>& reveal,
								 bool scrollToSelected)
{
	auto& node = sceneGraph.nodes[nodeIdx];
	bool isSelected = selection.selectedNode.has_value() &&
					  selection.selectedNode.value() == nodeIdx;
	bool hasChildren = !node.children.empty();

	// Double-click renames, so only the arrow toggles open/closed.
	ImGuiTreeNodeFlags flags =
		ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
	if (hasChildren)
		flags |= ImGuiTreeNodeFlags_DefaultOpen;
	else
		flags |= ImGuiTreeNodeFlags_Leaf;

	if (nodeIdx < reveal.size() && reveal[nodeIdx])
		ImGui::SetNextItemOpen(true);

	std::string label = node.name;
	if (node.light.has_value())
		label +=
			std::string("  [") + light_type_name(node.light->type) + " light]";
	else if (node.splat.has_value())
		label += "  [Gaussian splat]";
	bool renaming = renamingNode_.has_value() && *renamingNode_ == nodeIdx;
	void* id = reinterpret_cast<void*>(static_cast<uintptr_t>(nodeIdx));
	bool open =
		ImGui::TreeNodeEx(id, flags, "%s", renaming ? "" : label.c_str());

	if (isSelected && scrollToSelected) ImGui::SetScrollHereY(0.5f);

	if (!renaming)
	{
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
			selection.selectedNode = nodeIdx;
		if (ImGui::IsItemHovered() &&
			ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
			!ImGui::IsItemToggledOpen())
		{
			selection.selectedNode = nodeIdx;
			begin_rename(sceneGraph, nodeIdx);
		}

		if (ImGui::BeginPopupContextItem())
		{
			selection.selectedNode = nodeIdx;
			if (ImGui::MenuItem("Rename", "F2"))
				begin_rename(sceneGraph, nodeIdx);
			if (ImGui::MenuItem("Delete", "Del")) deleteRequested = true;
			ImGui::EndPopup();
		}
	}
	else
	{
		// Inline editor on the same row. Enter or clicking away commits;
		// Escape cancels.
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (renameFocusPending_)
		{
			ImGui::SetKeyboardFocusHere();
			renameFocusPending_ = false;
		}
		bool enter =
			ImGui::InputText("##rename", renameBuf_.data(), renameBuf_.size(),
							 ImGuiInputTextFlags_EnterReturnsTrue |
								 ImGuiInputTextFlags_AutoSelectAll);
		if (enter)
		{
			commit_name(node, renameBuf_.data());
			renamingNode_.reset();
		}
		else if (ImGui::IsItemDeactivated())
		{
			if (!ImGui::IsKeyPressed(ImGuiKey_Escape))
				commit_name(node, renameBuf_.data());
			renamingNode_.reset();
		}
	}

	if (open)
	{
		for (uint32_t child : node.children)
			draw_node_tree(sceneGraph, child, selection, reveal,
						   scrollToSelected);
		ImGui::TreePop();
	}
}

// Add a light node at a sensible default placement and select it.
static void add_light(SceneGraph& sceneGraph, Selection& selection,
					  LightType type)
{
	LightComponent light;
	light.type = type;
	glm::vec3 position(0.0f, 2.0f, 0.0f);
	glm::vec3 direction(0.0f, -1.0f, 0.0f);
	if (type == LightType::Directional)
	{
		light.intensity = 3.0f;
		position = glm::vec3(0.0f, 5.0f, 0.0f);
		direction = glm::vec3(0.5f, -1.0f, 0.3f);
	}
	else if (type == LightType::Spot)
	{
		light.intensity = 10.0f;
		position = glm::vec3(0.0f, 4.0f, 0.0f);
	}
	else
	{
		light.intensity = 5.0f;
	}

	uint32_t count = 0;
	for (const auto& n : sceneGraph.nodes)
		if (n.light.has_value() && n.light->type == type) ++count;
	std::string name =
		std::string(light_type_name(type)) + " Light " + std::to_string(count);

	selection.selectedNode = sceneGraph.add_light_node(
		name, light, make_light_transform(position, direction));
	sceneGraph.update_world_transforms();
}

// Render settings of a Gaussian splat node.
static void draw_splat_inspector(SplatComponent& splat)
{
	ImGui::Separator();
	ImGui::Text("Gaussian Splat");
	ImGui::TextWrapped("File: %s", splat.path.c_str());
	if (splat.count > 0)
		ImGui::Text("Splats: %u", splat.count);
	else
		ImGui::TextDisabled("Not loaded (see log)");

	int mode = static_cast<int>(splat.mode);
	ImGui::RadioButton("Gaussian", &mode,
					   static_cast<int>(SplatRenderMode::Gaussian));
	ImGui::SameLine();
	ImGui::RadioButton("Points", &mode,
					   static_cast<int>(SplatRenderMode::Points));
	splat.mode = static_cast<SplatRenderMode>(mode);

	if (splat.mode == SplatRenderMode::Points)
		ImGui::SliderFloat("Point Size", &splat.pointSize, 1.0f, 16.0f,
						   "%.1f px");
}

// Editable properties of a light node.
static void draw_light_inspector(LightComponent& light)
{
	ImGui::Separator();
	ImGui::Text("Light");

	int typeInt = static_cast<int>(light.type);
	const char* types[] = {"Directional", "Point", "Spot"};
	if (ImGui::Combo("Type", &typeInt, types, IM_ARRAYSIZE(types)))
		light.type = static_cast<LightType>(typeInt);

	ImGui::ColorEdit3("Color", &light.color.x);
	ImGui::DragFloat("Intensity", &light.intensity, 0.05f, 0.0f, 1000.0f);

	if (light.type != LightType::Directional)
		ImGui::DragFloat("Radius", &light.radius, 0.05f, 0.1f, 500.0f);

	if (light.type == LightType::Spot)
	{
		float innerDeg = glm::degrees(light.innerConeAngle);
		float outerDeg = glm::degrees(light.outerConeAngle);
		if (ImGui::SliderFloat("Inner Cone", &innerDeg, 1.0f, 89.0f))
		{
			light.innerConeAngle = glm::radians(innerDeg);
			light.outerConeAngle =
				std::max(light.outerConeAngle, light.innerConeAngle);
		}
		if (ImGui::SliderFloat("Outer Cone", &outerDeg, 1.0f, 89.0f))
		{
			light.outerConeAngle = glm::radians(outerDeg);
			light.innerConeAngle =
				std::min(light.innerConeAngle, light.outerConeAngle);
		}
	}

	if (light.type != LightType::Point)
		ImGui::TextDisabled("Direction follows the node's -Z axis (rotate)");
}

void DebugWindow::draw(Renderer& renderer, LightEnvironment& lights,
					   Selection& selection, Gizmo& gizmo,
					   SceneGraph& sceneGraph)
{
	ImGuiIO& io = ImGui::GetIO();
	VkExtent2D extent = renderer.swapchain_extent();

	// --- Frame Statistics ---
	ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
	ImGui::Begin("Frame Statistics");
	keep_below_menu_bar();
	ImGui::Text("FPS:        %.1f", io.Framerate);
	ImGui::Text("Frame Time: %.3f ms", 1000.0f / io.Framerate);
	ImGui::Separator();
	ImGui::Text("GPU: %s", renderer.gpu_name());
	ImGui::Text("Resolution: %u x %u", extent.width, extent.height);

	uint32_t tileX = (extent.width + 15) / 16;
	uint32_t tileY = (extent.height + 15) / 16;
	ImGui::Text("Tiles: %u x %u (%u total)", tileX, tileY, tileX * tileY);
	ImGui::Text("Total lights: %u", lights.total_light_count());
	ImGui::Separator();
	ImGui::Checkbox("Show Tile Heatmap", &renderer.showHeatmap_);
	ImGui::Checkbox("Show Light Wireframes", &renderer.showDebugLines_);
	ImGui::Separator();
	ImGui::Text("WASD + Space/Ctrl: move");
	ImGui::Text("Right-click + drag: look");
	ImGui::End();

	// --- Depth / Rasterizer Debug ---
	ImGui::SetNextWindowPos(ImVec2(320, 10), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(280, 0), ImGuiCond_FirstUseEver);
	ImGui::Begin("Depth / Rasterizer Debug");
	keep_below_menu_bar();

	ImGui::Checkbox("Skip Depth Prepass", &renderer.debugSkipDepthPrepass_);
	ImGui::Checkbox("Disable Backface Culling", &renderer.debugDisableCulling_);
	ImGui::RadioButton("Front Face: CCW", &renderer.debugFrontFace_, 0);
	ImGui::SameLine();
	ImGui::RadioButton("CW", &renderer.debugFrontFace_, 1);

	ImGui::Separator();
	ImGui::Text("Pipeline settings (read-only):");
	ImGui::Text("  PBR depth test: ON");
	ImGui::Text("  PBR depth write: OFF (prepass fills)");
	ImGui::Text("  PBR depth compare: LESS_OR_EQUAL");
	ImGui::Text("  Prepass depth test: ON");
	ImGui::Text("  Prepass depth write: ON");
	ImGui::Text("  Prepass depth compare: LESS");

	ImGui::End();

	// --- Lighting ---
	ImGui::SetNextWindowPos(ImVec2(10, 300), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(350, 0), ImGuiCond_FirstUseEver);
	ImGui::Begin("Lighting");
	keep_below_menu_bar();

	// Ambient
	if (ImGui::CollapsingHeader("Ambient", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::ColorEdit3("Color##Ambient", &lights.ambient.color.x);
		ImGui::SliderFloat("Intensity##Ambient", &lights.ambient.intensity,
						   0.0f, 1.0f);
	}

	// Directional / point / spot lights live in the scene graph.
	if (ImGui::CollapsingHeader("Scene Lights", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (ImGui::Button("Add Directional"))
			add_light(sceneGraph, selection, LightType::Directional);
		ImGui::SameLine();
		if (ImGui::Button("Add Point"))
			add_light(sceneGraph, selection, LightType::Point);
		ImGui::SameLine();
		if (ImGui::Button("Add Spot"))
			add_light(sceneGraph, selection, LightType::Spot);

		bool any = false;
		for (uint32_t i = 0; i < static_cast<uint32_t>(sceneGraph.nodes.size());
			 ++i)
		{
			const auto& node = sceneGraph.nodes[i];
			if (!node.light.has_value()) continue;
			any = true;
			ImGui::PushID(static_cast<int>(i));
			bool isSelected = selection.selectedNode.has_value() &&
							  selection.selectedNode.value() == i;
			std::string label =
				node.name + "  [" + light_type_name(node.light->type) + "]";
			if (ImGui::Selectable(label.c_str(), isSelected))
				selection.selectedNode = i;
			ImGui::PopID();
		}
		if (!any) ImGui::TextDisabled("No lights in scene");
		ImGui::TextDisabled("Select a light to edit it in Scene Hierarchy");
	}

	ImGui::End();

	// --- Scene Hierarchy ---
	ImGui::SetNextWindowPos(ImVec2(10, 550), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
	ImGui::Begin("Scene Hierarchy");
	keep_below_menu_bar();

	if (ImGui::Button("Import Mesh...")) importRequested = true;
	ImGui::SameLine();
	if (ImGui::Button("Import Splat...")) importSplatRequested = true;

	if (selection.selectedNode.has_value())
	{
		ImGui::SameLine();
		if (ImGui::Button("Delete")) deleteRequested = true;
	}

	ImGui::Separator();

	// When the selection changed outside the tree (viewport pick, import),
	// open every ancestor so the selected node is visible.
	bool selectionChanged = selection.selectedNode != lastSelected_;
	lastSelected_ = selection.selectedNode;
	std::vector<bool> reveal(sceneGraph.nodes.size(), false);
	if (selectionChanged && selection.selectedNode.has_value() &&
		*selection.selectedNode < sceneGraph.nodes.size())
	{
		auto p = sceneGraph.nodes[*selection.selectedNode].parent;
		while (p.has_value() && *p < sceneGraph.nodes.size() && !reveal[*p])
		{
			reveal[*p] = true;
			p = sceneGraph.nodes[*p].parent;
		}
	}

	// Tree in its own scrollable, resizable region so deep hierarchies do
	// not push the inspector below out of view.
	ImGui::BeginChild("##SceneTree", ImVec2(0.0f, 220.0f),
					  ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
	for (uint32_t root : sceneGraph.roots)
		draw_node_tree(sceneGraph, root, selection, reveal, selectionChanged);
	// F2 renames the selected node while the tree has focus
	if (!renamingNode_.has_value() && selection.selectedNode.has_value() &&
		ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_F2))
		begin_rename(sceneGraph, *selection.selectedNode);
	ImGui::EndChild();
	// A rename target that vanished (node deleted) must not linger
	if (renamingNode_.has_value() && *renamingNode_ >= sceneGraph.nodes.size())
		renamingNode_.reset();

	ImGui::Separator();

	if (selection.selectedNode.has_value())
	{
		uint32_t nodeIdx = selection.selectedNode.value();
		if (nodeIdx < sceneGraph.nodes.size())
		{
			auto& node = sceneGraph.nodes[nodeIdx];
			// Editable name. Refresh from the node unless the user is typing.
			if (nameFieldNode_ != nodeIdx || !nameFieldActive_)
				fill_buffer(nameBuf_, node.name);
			nameFieldNode_ = nodeIdx;
			ImGui::InputText("Name", nameBuf_.data(), nameBuf_.size());
			nameFieldActive_ = ImGui::IsItemActive();
			if (ImGui::IsItemDeactivatedAfterEdit())
				commit_name(node, nameBuf_.data());
			if (node.parent.has_value() &&
				*node.parent < sceneGraph.nodes.size())
				ImGui::TextDisabled(
					"Parent: %s", sceneGraph.nodes[*node.parent].name.c_str());
			if (!node.children.empty())
				ImGui::TextDisabled("Children: %zu", node.children.size());

			if (ImGui::Button("Deselect")) selection.selectedNode.reset();

			ImGui::Separator();

			// Gizmo operation
			int opInt = static_cast<int>(gizmo.operation);
			ImGui::Text("Gizmo Mode (W/E/R):");
			ImGui::RadioButton("Translate", &opInt, 0);
			ImGui::SameLine();
			ImGui::RadioButton("Rotate", &opInt, 1);
			ImGui::SameLine();
			ImGui::RadioButton("Scale", &opInt, 2);
			gizmo.operation = static_cast<Gizmo::Op>(opInt);

			// Gizmo space
			int spaceInt = static_cast<int>(gizmo.space);
			ImGui::RadioButton("World", &spaceInt, 0);
			ImGui::SameLine();
			ImGui::RadioButton("Local", &spaceInt, 1);
			gizmo.space = static_cast<Gizmo::Space>(spaceInt);

			// Snap
			ImGui::Checkbox("Snap", &gizmo.useSnap);
			if (gizmo.useSnap)
			{
				switch (gizmo.operation)
				{
					case Gizmo::Op::Translate:
						ImGui::SliderFloat("Snap##T", &gizmo.snapTranslate,
										   0.1f, 5.0f);
						break;
					case Gizmo::Op::Rotate:
						ImGui::SliderFloat("Snap##R", &gizmo.snapRotate, 1.0f,
										   90.0f);
						break;
					case Gizmo::Op::Scale:
						ImGui::SliderFloat("Snap##S", &gizmo.snapScale, 0.01f,
										   1.0f);
						break;
				}
			}

			// Decompose and display transform (relative to the parent node)
			ImGui::Separator();
			ImGui::TextDisabled("Local transform (relative to parent)");
			float matrixTranslation[3], matrixRotation[3], matrixScale[3];
			ImGuizmo::DecomposeMatrixToComponents(
				glm::value_ptr(node.localTransform), matrixTranslation,
				matrixRotation, matrixScale);
			if (node.light.has_value())
			{
				// Light nodes: editable position/rotation, scale is unused.
				bool changed =
					ImGui::DragFloat3("Position", matrixTranslation, 0.05f);
				changed |= ImGui::DragFloat3("Rotation", matrixRotation, 0.5f);
				if (changed)
				{
					float unitScale[3] = {1.0f, 1.0f, 1.0f};
					ImGuizmo::RecomposeMatrixFromComponents(
						matrixTranslation, matrixRotation, unitScale,
						glm::value_ptr(node.localTransform));
				}
				draw_light_inspector(*node.light);
			}
			else if (node.splat.has_value())
			{
				ImGui::Text("Position: %.2f, %.2f, %.2f", matrixTranslation[0],
							matrixTranslation[1], matrixTranslation[2]);
				ImGui::Text("Rotation: %.1f, %.1f, %.1f", matrixRotation[0],
							matrixRotation[1], matrixRotation[2]);
				ImGui::Text("Scale:    %.2f, %.2f, %.2f", matrixScale[0],
							matrixScale[1], matrixScale[2]);
				draw_splat_inspector(*node.splat);
			}
			else
			{
				ImGui::Text("Position: %.2f, %.2f, %.2f", matrixTranslation[0],
							matrixTranslation[1], matrixTranslation[2]);
				ImGui::Text("Rotation: %.1f, %.1f, %.1f", matrixRotation[0],
							matrixRotation[1], matrixRotation[2]);
				ImGui::Text("Scale:    %.2f, %.2f, %.2f", matrixScale[0],
							matrixScale[1], matrixScale[2]);
			}
		}
	}
	else
	{
		ImGui::Text("No node selected");
		ImGui::Text("Left-click to select a mesh or light");
	}

	ImGui::End();
}
