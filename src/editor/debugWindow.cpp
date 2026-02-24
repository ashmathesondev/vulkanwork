#include "debugWindow.h"

#include <ImGuizmo.h>
#include <imgui.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "editor/gizmo.h"
#include "editor/sceneGraph.h"
#include "editor/selection.h"
#include "graphics/light.h"
#include "graphics/renderer.h"

// Recursive helper to draw scene hierarchy tree
static void draw_node_tree(SceneGraph& sceneGraph, uint32_t nodeIdx,
						   Selection& selection)
{
	auto& node = sceneGraph.nodes[nodeIdx];
	bool isSelected = selection.selectedNode.has_value() &&
					  selection.selectedNode.value() == nodeIdx;
	bool hasChildren = !node.children.empty();

	ImGuiTreeNodeFlags flags =
		ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;
	if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

	bool open = ImGui::TreeNodeEx(
		reinterpret_cast<void*>(static_cast<uintptr_t>(nodeIdx)), flags, "%s",
		node.name.c_str());

	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		selection.selectedNode = nodeIdx;

	if (open)
	{
		for (uint32_t child : node.children)
			draw_node_tree(sceneGraph, child, selection);
		ImGui::TreePop();
	}
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
	ImGui::Checkbox("Enable Shadows", &renderer.shadowsEnabled_);
	ImGui::SliderFloat("Shadow Bias", &renderer.shadowBias_, 0.0f, 0.05f,
					   "%.4f");
	ImGui::Separator();
	ImGui::Text("WASD + Space/Ctrl: move");
	ImGui::Text("Right-click + drag: look");
	ImGui::End();

	// --- Depth / Rasterizer Debug ---
	ImGui::SetNextWindowPos(ImVec2(320, 10), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(280, 0), ImGuiCond_FirstUseEver);
	ImGui::Begin("Depth / Rasterizer Debug");

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

	// Ambient
	if (ImGui::CollapsingHeader("Ambient", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::ColorEdit3("Color##Ambient", &lights.ambient.color.x);
		ImGui::SliderFloat("Intensity##Ambient", &lights.ambient.intensity,
						   0.0f, 1.0f);
	}

	// Directional lights
	if (ImGui::CollapsingHeader("Directional Lights",
								ImGuiTreeNodeFlags_DefaultOpen))
	{
		for (size_t i = 0; i < lights.directionals.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			auto& d = lights.directionals[i];
			ImGui::Text("Directional %zu", i);
			ImGui::SliderFloat3("Direction", &d.direction.x, -1.0f, 1.0f);
			ImGui::ColorEdit3("Color", &d.color.x);
			ImGui::SliderFloat("Intensity", &d.intensity, 0.0f, 20.0f);
			if (ImGui::Button("Remove"))
			{
				lights.directionals.erase(lights.directionals.begin() +
										  static_cast<ptrdiff_t>(i));
				ImGui::PopID();
				break;
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		if (ImGui::Button("Add Directional")) lights.directionals.push_back({});
	}

	// Point lights
	if (ImGui::CollapsingHeader("Point Lights"))
	{
		for (size_t i = 0; i < lights.points.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(1000 + i));
			auto& p = lights.points[i];
			ImGui::Text("Point %zu", i);
			ImGui::DragFloat3("Position", &p.position.x, 0.1f);
			ImGui::ColorEdit3("Color", &p.color.x);
			ImGui::SliderFloat("Intensity", &p.intensity, 0.0f, 100.0f);
			ImGui::SliderFloat("Radius", &p.radius, 0.1f, 50.0f);
			if (ImGui::Button("Remove"))
			{
				lights.points.erase(lights.points.begin() +
									static_cast<ptrdiff_t>(i));
				ImGui::PopID();
				break;
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		if (ImGui::Button("Add Point Light")) lights.points.push_back({});
	}

	// Spot lights
	if (ImGui::CollapsingHeader("Spot Lights"))
	{
		for (size_t i = 0; i < lights.spots.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(2000 + i));
			auto& s = lights.spots[i];
			ImGui::Text("Spot %zu", i);
			ImGui::DragFloat3("Position", &s.position.x, 0.1f);
			ImGui::SliderFloat3("Direction", &s.direction.x, -1.0f, 1.0f);
			ImGui::ColorEdit3("Color", &s.color.x);
			ImGui::SliderFloat("Intensity", &s.intensity, 0.0f, 100.0f);
			ImGui::SliderFloat("Radius", &s.radius, 0.1f, 50.0f);
			float innerDeg = glm::degrees(s.innerConeAngle);
			float outerDeg = glm::degrees(s.outerConeAngle);
			if (ImGui::SliderFloat("Inner Cone", &innerDeg, 1.0f, 89.0f))
				s.innerConeAngle = glm::radians(innerDeg);
			if (ImGui::SliderFloat("Outer Cone", &outerDeg, 1.0f, 89.0f))
				s.outerConeAngle = glm::radians(outerDeg);
			if (ImGui::Button("Remove"))
			{
				lights.spots.erase(lights.spots.begin() +
								   static_cast<ptrdiff_t>(i));
				ImGui::PopID();
				break;
			}
			ImGui::Separator();
			ImGui::PopID();
		}
		if (ImGui::Button("Add Spot Light")) lights.spots.push_back({});
	}

	ImGui::End();

	// --- Scene Hierarchy ---
	ImGui::SetNextWindowPos(ImVec2(10, 550), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
	ImGui::Begin("Scene Hierarchy");

	if (ImGui::Button("Import Mesh...")) importRequested = true;

	if (selection.selectedNode.has_value())
	{
		ImGui::SameLine();
		if (ImGui::Button("Delete")) deleteRequested = true;
	}

	ImGui::Separator();

	for (uint32_t root : sceneGraph.roots)
		draw_node_tree(sceneGraph, root, selection);

	ImGui::Separator();

	if (selection.selectedNode.has_value())
	{
		uint32_t nodeIdx = selection.selectedNode.value();
		if (nodeIdx < sceneGraph.nodes.size())
		{
			auto& node = sceneGraph.nodes[nodeIdx];
			ImGui::Text("Selected: %s", node.name.c_str());

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

			// Decompose and display transform
			ImGui::Separator();
			float matrixTranslation[3], matrixRotation[3], matrixScale[3];
			ImGuizmo::DecomposeMatrixToComponents(
				glm::value_ptr(node.localTransform), matrixTranslation,
				matrixRotation, matrixScale);
			ImGui::Text("Position: %.2f, %.2f, %.2f", matrixTranslation[0],
						matrixTranslation[1], matrixTranslation[2]);
			ImGui::Text("Rotation: %.1f, %.1f, %.1f", matrixRotation[0],
						matrixRotation[1], matrixRotation[2]);
			ImGui::Text("Scale:    %.2f, %.2f, %.2f", matrixScale[0],
						matrixScale[1], matrixScale[2]);
		}
	}
	else
	{
		ImGui::Text("No node selected");
		ImGui::Text("Left-click to select a mesh");
	}

	ImGui::End();
}
