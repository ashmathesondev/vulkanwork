#include "debugWindow.h"

#include <imgui.h>

#include "renderer.h"

void DebugWindow::draw(const Renderer& renderer)
{
	ImGuiIO& io = ImGui::GetIO();
	VkExtent2D extent = renderer.swapchain_extent();

	ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
	ImGui::Begin("Frame Statistics");
	ImGui::Text("FPS:        %.1f", io.Framerate);
	ImGui::Text("Frame Time: %.3f ms", 1000.0f / io.Framerate);
	ImGui::Separator();
	ImGui::Text("GPU: %s", renderer.gpu_name());
	ImGui::Text("Resolution: %u x %u", extent.width, extent.height);
	ImGui::Separator();
	ImGui::Text("WASD + Space/Ctrl: move");
	ImGui::Text("Right-click + drag: look");
	ImGui::End();
}
