#include "app.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <cmath>
#include <cstdio>
#include <glm/glm.hpp>
#include <stdexcept>

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
	window = glfwCreateWindow(INITIAL_WIDTH, INITIAL_HEIGHT, "vulkanwork",
							  nullptr, nullptr);
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
	renderer.init(window);
	init_imgui();
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

		debugWindow.draw(renderer);

		ImGui::Render();

		// --- Draw ------------------------------------------------------------
		auto frame = renderer.begin_frame();
		if (!frame) continue;  // swapchain was recreated

		float time = static_cast<float>(glfwGetTime());
		renderer.update_uniforms(camera, time);
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

	// --- Keyboard movement ---------------------------------------------------
	if (!io.WantCaptureKeyboard)
	{
		float v = camera.speed * deltaTime;
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
		if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
			camera.position += camera.up * v;
		if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
			camera.position -= camera.up * v;
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
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	vkDestroyDescriptorPool(renderer.vk_device(), imguiPool, nullptr);

	renderer.cleanup();

	glfwDestroyWindow(window);
	glfwTerminate();
}
