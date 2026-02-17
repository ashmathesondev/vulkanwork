#include "debugWindow.h"

#include <imgui.h>

#include <glm/glm.hpp>

#include "graphics/light.h"
#include "graphics/renderer.h"

void DebugWindow::draw(Renderer& renderer, LightEnvironment& lights)
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
	ImGui::Separator();
	ImGui::Text("WASD + Space/Ctrl: move");
	ImGui::Text("Right-click + drag: look");
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
}
