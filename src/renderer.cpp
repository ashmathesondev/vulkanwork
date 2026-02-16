#include "renderer.h"

#include "camera.h"
#include "config.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

// =============================================================================
// Macros & constants
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

static constexpr const char* VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";

#ifdef NDEBUG
static constexpr bool ENABLE_VALIDATION = false;
#else
static constexpr bool ENABLE_VALIDATION = true;
#endif

// =============================================================================
// Vertex & UBO types
// =============================================================================

struct Vertex
{
	glm::vec3 pos;
	glm::vec3 color;
	glm::vec2 uv;

	static VkVertexInputBindingDescription binding_desc()
	{
		return {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
	}
	static std::array<VkVertexInputAttributeDescription, 3> attrib_descs()
	{
		return {{
			{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
			{1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)},
			{2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
		}};
	}
};

struct UniformBufferObject
{
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
};

// =============================================================================
// Cube geometry (24 verts — 4 per face for proper UVs, 36 indices)
// =============================================================================

static const std::vector<Vertex> CUBE_VERTS = {
	// Front face (+Z) — red tint
	{{-0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},  //  0
	{{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},   //  1
	{{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},	   //  2
	{{-0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},   //  3

	// Back face (-Z) — green tint
	{{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},	//  4
	{{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},	//  5
	{{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},	//  6
	{{0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},	//  7

	// Left face (-X) — blue tint
	{{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},	//  8
	{{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},	//  9
	{{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},	// 10
	{{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},	// 11

	// Right face (+X) — yellow tint
	{{0.5f, -0.5f, 0.5f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},   // 12
	{{0.5f, -0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},  // 13
	{{0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},   // 14
	{{0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},	   // 15

	// Top face (+Y) — magenta tint
	{{-0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},   // 16
	{{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},	   // 17
	{{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},   // 18
	{{-0.5f, 0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},  // 19

	// Bottom face (-Y) — cyan tint
	{{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},	// 20
	{{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},	// 21
	{{0.5f, -0.5f, 0.5f}, {0.0f, 1.0f, 1.0f}, {1.0f, 0.0f}},	// 22
	{{-0.5f, -0.5f, 0.5f}, {0.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},	// 23
};

static const std::vector<uint16_t> CUBE_INDICES = {
	0,	1,	2,	2,	3,	0,	 // front
	4,	5,	6,	6,	7,	4,	 // back
	8,	9,	10, 10, 11, 8,	 // left
	12, 13, 14, 14, 15, 12,	 // right
	16, 17, 18, 18, 19, 16,	 // top
	20, 21, 22, 22, 23, 20,	 // bottom
};

// =============================================================================
// Utility helpers
// =============================================================================

static VKAPI_ATTR VkBool32 VKAPI_CALL
debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			   VkDebugUtilsMessageTypeFlagsEXT,
			   const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
{
	if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		std::fprintf(stderr, "[Vulkan] %s\n", data->pMessage);
	return VK_FALSE;
}

static bool check_validation_support()
{
	uint32_t cnt;
	vkEnumerateInstanceLayerProperties(&cnt, nullptr);
	std::vector<VkLayerProperties> layers(cnt);
	vkEnumerateInstanceLayerProperties(&cnt, layers.data());
	for (auto& l : layers)
		if (std::strcmp(l.layerName, VALIDATION_LAYER) == 0) return true;
	return false;
}

// =============================================================================
// Accessors
// =============================================================================

const char* Renderer::gpu_name() const { return gpuName_; }
VkExtent2D Renderer::swapchain_extent() const { return swapchainExtent_; }
VkInstance Renderer::vk_instance() const { return instance_; }
VkPhysicalDevice Renderer::vk_physical_device() const
{
	return physicalDevice_;
}
VkDevice Renderer::vk_device() const { return device_; }
uint32_t Renderer::vk_graphics_family() const { return graphicsFamily_; }
VkQueue Renderer::vk_graphics_queue() const { return graphicsQueue_; }
uint32_t Renderer::swapchain_image_count() const
{
	return static_cast<uint32_t>(swapchainImages_.size());
}
VkRenderPass Renderer::vk_render_pass() const { return renderPass_; }

void Renderer::notify_resize() { framebufferResized_ = true; }

// =============================================================================
// Lifecycle
// =============================================================================

void Renderer::init(GLFWwindow* window)
{
	window_ = window;
	packFile_.emplace(PAK_FILE);
	create_instance();
	setup_debug_messenger();
	create_surface();
	pick_physical_device();
	create_logical_device();
	create_swapchain();
	create_image_views();
	create_render_pass();
	create_descriptor_set_layout();
	create_graphics_pipeline();
	create_depth_resources();
	create_framebuffers();
	create_command_pool();
	create_texture_image();
	create_texture_image_view();
	create_texture_sampler();
	create_vertex_buffer();
	create_index_buffer();
	create_uniform_buffers();
	create_descriptor_pool();
	create_descriptor_sets();
	create_command_buffers();
	create_sync_objects();
}

void Renderer::cleanup()
{
	cleanup_swapchain();

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		vkDestroyBuffer(device_, uniformBuffers_[i], nullptr);
		vkFreeMemory(device_, uniformBuffersMemory_[i], nullptr);
	}
	vkDestroySampler(device_, textureSampler_, nullptr);
	vkDestroyImageView(device_, textureImageView_, nullptr);
	vkDestroyImage(device_, textureImage_, nullptr);
	vkFreeMemory(device_, textureImageMemory_, nullptr);

	vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
	vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);

	vkDestroyBuffer(device_, indexBuffer_, nullptr);
	vkFreeMemory(device_, indexBufferMemory_, nullptr);
	vkDestroyBuffer(device_, vertexBuffer_, nullptr);
	vkFreeMemory(device_, vertexBufferMemory_, nullptr);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		vkDestroySemaphore(device_, imageAvailableSemaphores_[i], nullptr);
		vkDestroyFence(device_, inFlightFences_[i], nullptr);
	}
	for (auto s : renderFinishedSemaphores_)
		vkDestroySemaphore(device_, s, nullptr);
	vkDestroyCommandPool(device_, commandPool_, nullptr);

	vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
	vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
	vkDestroyRenderPass(device_, renderPass_, nullptr);

	vkDestroyDevice(device_, nullptr);

	if (debugMessenger_)
	{
		auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
			vkGetInstanceProcAddr(instance_,
								  "vkDestroyDebugUtilsMessengerEXT"));
		if (fn) fn(instance_, debugMessenger_, nullptr);
	}
	vkDestroySurfaceKHR(instance_, surface_, nullptr);
	vkDestroyInstance(instance_, nullptr);
}

// =============================================================================
// Per-frame rendering
// =============================================================================

std::optional<Renderer::FrameContext> Renderer::begin_frame()
{
	vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE,
					UINT64_MAX);

	uint32_t imageIndex;
	VkResult result = vkAcquireNextImageKHR(
		device_, swapchain_, UINT64_MAX,
		imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR)
	{
		recreate_swapchain();
		return std::nullopt;
	}
	if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		throw std::runtime_error("Failed to acquire swapchain image");

	vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);

	VkCommandBuffer cmd = commandBuffers_[currentFrame_];
	vkResetCommandBuffer(cmd, 0);

	VkCommandBufferBeginInfo beginInfo{
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

	std::array<VkClearValue, 2> clears{};
	clears[0].color = {{0.1f, 0.1f, 0.1f, 1.0f}};
	clears[1].depthStencil = {1.0f, 0};

	VkRenderPassBeginInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
	rpInfo.renderPass = renderPass_;
	rpInfo.framebuffer = framebuffers_[imageIndex];
	rpInfo.renderArea = {{0, 0}, swapchainExtent_};
	rpInfo.clearValueCount = static_cast<uint32_t>(clears.size());
	rpInfo.pClearValues = clears.data();

	vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport vp{0,
				  0,
				  static_cast<float>(swapchainExtent_.width),
				  static_cast<float>(swapchainExtent_.height),
				  0.0f,
				  1.0f};
	vkCmdSetViewport(cmd, 0, 1, &vp);

	VkRect2D scissor{{0, 0}, swapchainExtent_};
	vkCmdSetScissor(cmd, 0, 1, &scissor);

	return FrameContext{cmd, imageIndex};
}

void Renderer::update_uniforms(const Camera& camera, float time)
{
	UniformBufferObject ubo{};
	ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(45.0f),
							glm::vec3(0.0f, 1.0f, 0.0f));
	ubo.model = glm::rotate(ubo.model, time * glm::radians(30.0f),
							glm::vec3(1.0f, 0.0f, 0.0f));

	ubo.view = camera.view_matrix();

	float aspect = static_cast<float>(swapchainExtent_.width) /
				   static_cast<float>(swapchainExtent_.height);
	ubo.proj = glm::perspective(glm::radians(camera.fov), aspect, 0.1f, 100.0f);
	ubo.proj[1][1] *= -1.0f;  // Vulkan Y-flip

	std::memcpy(uniformBuffersMapped_[currentFrame_], &ubo, sizeof(ubo));
}

void Renderer::draw_scene(VkCommandBuffer cmd)
{
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);

	VkBuffer vbufs[] = {vertexBuffer_};
	VkDeviceSize offs[] = {0};
	vkCmdBindVertexBuffers(cmd, 0, 1, vbufs, offs);
	vkCmdBindIndexBuffer(cmd, indexBuffer_, 0, VK_INDEX_TYPE_UINT16);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
							pipelineLayout_, 0, 1,
							&descriptorSets_[currentFrame_], 0, nullptr);

	vkCmdDrawIndexed(cmd, indexCount_, 1, 0, 0, 0);
}

void Renderer::end_frame(const FrameContext& ctx)
{
	vkCmdEndRenderPass(ctx.cmd);
	VK_CHECK(vkEndCommandBuffer(ctx.cmd));

	VkSemaphore waitSems[] = {imageAvailableSemaphores_[currentFrame_]};
	VkPipelineStageFlags waitStages[] = {
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	VkSemaphore sigSems[] = {renderFinishedSemaphores_[ctx.imageIndex]};

	VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
	si.waitSemaphoreCount = 1;
	si.pWaitSemaphores = waitSems;
	si.pWaitDstStageMask = waitStages;
	si.commandBufferCount = 1;
	si.pCommandBuffers = &ctx.cmd;
	si.signalSemaphoreCount = 1;
	si.pSignalSemaphores = sigSems;
	VK_CHECK(
		vkQueueSubmit(graphicsQueue_, 1, &si, inFlightFences_[currentFrame_]));

	VkSwapchainKHR swapchains[] = {swapchain_};
	VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
	pi.waitSemaphoreCount = 1;
	pi.pWaitSemaphores = sigSems;
	pi.swapchainCount = 1;
	pi.pSwapchains = swapchains;
	pi.pImageIndices = &ctx.imageIndex;

	VkResult result = vkQueuePresentKHR(presentQueue_, &pi);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
		framebufferResized_)
	{
		framebufferResized_ = false;
		recreate_swapchain();
	}
	else if (result != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to present");
	}

	currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

// =============================================================================
// Instance & debug
// =============================================================================

void Renderer::create_instance()
{
	bool useValidation = ENABLE_VALIDATION && check_validation_support();
	if (ENABLE_VALIDATION && !useValidation)
		std::fprintf(
			stderr, "Warning: validation layers requested but not available\n");

	VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
	appInfo.pApplicationName = "vulkanwork";
	appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
	appInfo.pEngineName = "none";
	appInfo.apiVersion = VK_API_VERSION_1_3;

	uint32_t glfwExtCnt;
	const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCnt);
	std::vector<const char*> exts(glfwExts, glfwExts + glfwExtCnt);
	if (useValidation) exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

	VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
	ci.pApplicationInfo = &appInfo;
	ci.enabledExtensionCount = static_cast<uint32_t>(exts.size());
	ci.ppEnabledExtensionNames = exts.data();
	if (useValidation)
	{
		ci.enabledLayerCount = 1;
		ci.ppEnabledLayerNames = &VALIDATION_LAYER;
	}
	VK_CHECK(vkCreateInstance(&ci, nullptr, &instance_));
}

void Renderer::setup_debug_messenger()
{
	if (!ENABLE_VALIDATION) return;

	VkDebugUtilsMessengerCreateInfoEXT ci{
		VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
	ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
						 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
					 VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
					 VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	ci.pfnUserCallback = debug_callback;

	auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
		vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
	if (fn) fn(instance_, &ci, nullptr, &debugMessenger_);
}

void Renderer::create_surface()
{
	VK_CHECK(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_));
}

// =============================================================================
// Device selection
// =============================================================================

void Renderer::pick_physical_device()
{
	uint32_t cnt;
	vkEnumeratePhysicalDevices(instance_, &cnt, nullptr);
	if (cnt == 0) throw std::runtime_error("No Vulkan GPU found");
	std::vector<VkPhysicalDevice> devs(cnt);
	vkEnumeratePhysicalDevices(instance_, &cnt, devs.data());

	for (auto pd : devs)
	{
		// Queue families
		uint32_t qfCnt;
		vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfCnt, nullptr);
		std::vector<VkQueueFamilyProperties> qfs(qfCnt);
		vkGetPhysicalDeviceQueueFamilyProperties(pd, &qfCnt, qfs.data());

		int gf = -1, pf = -1;
		for (uint32_t i = 0; i < qfCnt; ++i)
		{
			if (qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
				gf = static_cast<int>(i);
			VkBool32 present = VK_FALSE;
			vkGetPhysicalDeviceSurfaceSupportKHR(pd, i, surface_, &present);
			if (present) pf = static_cast<int>(i);
		}
		if (gf < 0 || pf < 0) continue;

		// Check swapchain extension
		uint32_t extCnt;
		vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCnt, nullptr);
		std::vector<VkExtensionProperties> exts(extCnt);
		vkEnumerateDeviceExtensionProperties(pd, nullptr, &extCnt, exts.data());
		bool hasSwapchain = false;
		for (auto& e : exts)
			if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) ==
				0)
				hasSwapchain = true;
		if (!hasSwapchain) continue;

		// Check swapchain adequacy
		uint32_t fmtCnt, pmCnt;
		vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface_, &fmtCnt, nullptr);
		vkGetPhysicalDeviceSurfacePresentModesKHR(pd, surface_, &pmCnt,
												  nullptr);
		if (fmtCnt == 0 || pmCnt == 0) continue;

		physicalDevice_ = pd;
		graphicsFamily_ = static_cast<uint32_t>(gf);
		presentFamily_ = static_cast<uint32_t>(pf);

		VkPhysicalDeviceProperties props;
		vkGetPhysicalDeviceProperties(pd, &props);
		std::strncpy(gpuName_, props.deviceName, sizeof(gpuName_) - 1);

		// Prefer discrete GPU
		if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) break;
	}
	if (physicalDevice_ == VK_NULL_HANDLE)
		throw std::runtime_error("No suitable GPU found");
}

void Renderer::create_logical_device()
{
	std::set<uint32_t> uniqueFamilies = {graphicsFamily_, presentFamily_};
	float prio = 1.0f;
	std::vector<VkDeviceQueueCreateInfo> queueCIs;
	for (uint32_t fam : uniqueFamilies)
	{
		VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
		qi.queueFamilyIndex = fam;
		qi.queueCount = 1;
		qi.pQueuePriorities = &prio;
		queueCIs.push_back(qi);
	}

	VkPhysicalDeviceFeatures features{};
	features.samplerAnisotropy = VK_TRUE;

	const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
	ci.queueCreateInfoCount = static_cast<uint32_t>(queueCIs.size());
	ci.pQueueCreateInfos = queueCIs.data();
	ci.pEnabledFeatures = &features;
	ci.enabledExtensionCount = 1;
	ci.ppEnabledExtensionNames = devExts;

	VK_CHECK(vkCreateDevice(physicalDevice_, &ci, nullptr, &device_));
	vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
	vkGetDeviceQueue(device_, presentFamily_, 0, &presentQueue_);
}

// =============================================================================
// Swapchain
// =============================================================================

void Renderer::create_swapchain()
{
	VkSurfaceCapabilitiesKHR caps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, surface_, &caps);

	uint32_t fmtCnt;
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCnt,
										 nullptr);
	std::vector<VkSurfaceFormatKHR> fmts(fmtCnt);
	vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, surface_, &fmtCnt,
										 fmts.data());

	uint32_t pmCnt;
	vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &pmCnt,
											  nullptr);
	std::vector<VkPresentModeKHR> pms(pmCnt);
	vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice_, surface_, &pmCnt,
											  pms.data());

	// Pick format
	VkSurfaceFormatKHR fmt = fmts[0];
	for (auto& f : fmts)
		if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
			f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		{
			fmt = f;
			break;
		}

	// Pick present mode
	VkPresentModeKHR pm = VK_PRESENT_MODE_FIFO_KHR;
	for (auto m : pms)
		if (m == VK_PRESENT_MODE_MAILBOX_KHR)
		{
			pm = m;
			break;
		}

	// Pick extent
	VkExtent2D extent;
	if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
	{
		extent = caps.currentExtent;
	}
	else
	{
		int w, h;
		glfwGetFramebufferSize(window_, &w, &h);
		extent.width =
			std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width,
					   caps.maxImageExtent.width);
		extent.height =
			std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height,
					   caps.maxImageExtent.height);
	}

	uint32_t imgCount = caps.minImageCount + 1;
	if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount)
		imgCount = caps.maxImageCount;

	VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
	ci.surface = surface_;
	ci.minImageCount = imgCount;
	ci.imageFormat = fmt.format;
	ci.imageColorSpace = fmt.colorSpace;
	ci.imageExtent = extent;
	ci.imageArrayLayers = 1;
	ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	uint32_t families[] = {graphicsFamily_, presentFamily_};
	if (graphicsFamily_ != presentFamily_)
	{
		ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		ci.queueFamilyIndexCount = 2;
		ci.pQueueFamilyIndices = families;
	}
	else
	{
		ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	}

	ci.preTransform = caps.currentTransform;
	ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	ci.presentMode = pm;
	ci.clipped = VK_TRUE;

	VK_CHECK(vkCreateSwapchainKHR(device_, &ci, nullptr, &swapchain_));

	vkGetSwapchainImagesKHR(device_, swapchain_, &imgCount, nullptr);
	swapchainImages_.resize(imgCount);
	vkGetSwapchainImagesKHR(device_, swapchain_, &imgCount,
							swapchainImages_.data());
	swapchainFormat_ = fmt.format;
	swapchainExtent_ = extent;
}

void Renderer::create_image_views()
{
	swapchainImageViews_.resize(swapchainImages_.size());
	for (size_t i = 0; i < swapchainImages_.size(); ++i)
		swapchainImageViews_[i] = create_image_view(
			swapchainImages_[i], swapchainFormat_, VK_IMAGE_ASPECT_COLOR_BIT);
}

void Renderer::recreate_swapchain()
{
	int w = 0, h = 0;
	glfwGetFramebufferSize(window_, &w, &h);
	while (w == 0 || h == 0)
	{
		glfwGetFramebufferSize(window_, &w, &h);
		glfwWaitEvents();
	}
	vkDeviceWaitIdle(device_);

	cleanup_swapchain();

	// Recreate render-finished semaphores (count may change with new swapchain)
	for (auto s : renderFinishedSemaphores_)
		vkDestroySemaphore(device_, s, nullptr);
	renderFinishedSemaphores_.clear();

	create_swapchain();
	create_image_views();
	create_depth_resources();
	create_framebuffers();

	VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	renderFinishedSemaphores_.resize(swapchainImages_.size());
	for (size_t i = 0; i < swapchainImages_.size(); ++i)
		VK_CHECK(vkCreateSemaphore(device_, &sci, nullptr,
								   &renderFinishedSemaphores_[i]));
}

void Renderer::cleanup_swapchain()
{
	vkDestroyImageView(device_, depthView_, nullptr);
	vkDestroyImage(device_, depthImage_, nullptr);
	vkFreeMemory(device_, depthMemory_, nullptr);
	for (auto fb : framebuffers_) vkDestroyFramebuffer(device_, fb, nullptr);
	for (auto iv : swapchainImageViews_)
		vkDestroyImageView(device_, iv, nullptr);
	vkDestroySwapchainKHR(device_, swapchain_, nullptr);
}

// =============================================================================
// Render pass
// =============================================================================

void Renderer::create_render_pass()
{
	VkAttachmentDescription colorAtt{};
	colorAtt.format = swapchainFormat_;
	colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentDescription depthAtt{};
	depthAtt.format = find_depth_format();
	depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkAttachmentReference depthRef{
		1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;
	subpass.pDepthStencilAttachment = &depthRef;

	VkSubpassDependency dep{};
	dep.srcSubpass = VK_SUBPASS_EXTERNAL;
	dep.dstSubpass = 0;
	dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
					   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dep.srcAccessMask = 0;
	dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
					   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
						VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	std::array<VkAttachmentDescription, 2> attachments = {colorAtt, depthAtt};

	VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
	ci.attachmentCount = static_cast<uint32_t>(attachments.size());
	ci.pAttachments = attachments.data();
	ci.subpassCount = 1;
	ci.pSubpasses = &subpass;
	ci.dependencyCount = 1;
	ci.pDependencies = &dep;

	VK_CHECK(vkCreateRenderPass(device_, &ci, nullptr, &renderPass_));
}

// =============================================================================
// Descriptor layout & graphics pipeline
// =============================================================================

void Renderer::create_descriptor_set_layout()
{
	VkDescriptorSetLayoutBinding uboBinding{};
	uboBinding.binding = 0;
	uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboBinding.descriptorCount = 1;
	uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	VkDescriptorSetLayoutBinding samplerBinding{};
	samplerBinding.binding = 1;
	samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerBinding.descriptorCount = 1;
	samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboBinding,
															samplerBinding};

	VkDescriptorSetLayoutCreateInfo ci{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
	ci.bindingCount = static_cast<uint32_t>(bindings.size());
	ci.pBindings = bindings.data();
	VK_CHECK(vkCreateDescriptorSetLayout(device_, &ci, nullptr,
										 &descriptorSetLayout_));
}

void Renderer::create_graphics_pipeline()
{
	auto vertCode = packFile_->read("shaders/cube.vert.spv");
	auto fragCode = packFile_->read("shaders/cube.frag.spv");

	VkShaderModule vertMod = create_shader_module(vertCode);
	VkShaderModule fragMod = create_shader_module(fragCode);

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vertMod;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fragMod;
	stages[1].pName = "main";

	auto bindDesc = Vertex::binding_desc();
	auto attDescs = Vertex::attrib_descs();

	VkPipelineVertexInputStateCreateInfo vertInput{
		VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
	vertInput.vertexBindingDescriptionCount = 1;
	vertInput.pVertexBindingDescriptions = &bindDesc;
	vertInput.vertexAttributeDescriptionCount =
		static_cast<uint32_t>(attDescs.size());
	vertInput.pVertexAttributeDescriptions = attDescs.data();

	VkPipelineInputAssemblyStateCreateInfo inputAsm{
		VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
	inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo vpState{
		VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
	vpState.viewportCount = 1;
	vpState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo raster{
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.lineWidth = 1.0f;
	raster.cullMode = VK_CULL_MODE_BACK_BIT;
	raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	VkPipelineMultisampleStateCreateInfo ms{
		VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo ds{
		VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
	ds.depthTestEnable = VK_TRUE;
	ds.depthWriteEnable = VK_TRUE;
	ds.depthCompareOp = VK_COMPARE_OP_LESS;

	VkPipelineColorBlendAttachmentState blendAtt{};
	blendAtt.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo blend{
		VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
	blend.attachmentCount = 1;
	blend.pAttachments = &blendAtt;

	VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
								  VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dyn{
		VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
	dyn.dynamicStateCount = 2;
	dyn.pDynamicStates = dynStates;

	VkPipelineLayoutCreateInfo layoutCI{
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	layoutCI.setLayoutCount = 1;
	layoutCI.pSetLayouts = &descriptorSetLayout_;
	VK_CHECK(
		vkCreatePipelineLayout(device_, &layoutCI, nullptr, &pipelineLayout_));

	VkGraphicsPipelineCreateInfo ci{
		VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
	ci.stageCount = 2;
	ci.pStages = stages;
	ci.pVertexInputState = &vertInput;
	ci.pInputAssemblyState = &inputAsm;
	ci.pViewportState = &vpState;
	ci.pRasterizationState = &raster;
	ci.pMultisampleState = &ms;
	ci.pDepthStencilState = &ds;
	ci.pColorBlendState = &blend;
	ci.pDynamicState = &dyn;
	ci.layout = pipelineLayout_;
	ci.renderPass = renderPass_;
	ci.subpass = 0;

	VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr,
									   &graphicsPipeline_));

	vkDestroyShaderModule(device_, fragMod, nullptr);
	vkDestroyShaderModule(device_, vertMod, nullptr);
}

// =============================================================================
// Depth resources
// =============================================================================

void Renderer::create_depth_resources()
{
	VkFormat depthFmt = find_depth_format();

	VkImageCreateInfo imgCI{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
	imgCI.imageType = VK_IMAGE_TYPE_2D;
	imgCI.format = depthFmt;
	imgCI.extent = {swapchainExtent_.width, swapchainExtent_.height, 1};
	imgCI.mipLevels = 1;
	imgCI.arrayLayers = 1;
	imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	VK_CHECK(vkCreateImage(device_, &imgCI, nullptr, &depthImage_));

	VkMemoryRequirements memReq;
	vkGetImageMemoryRequirements(device_, depthImage_, &memReq);
	VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	allocInfo.allocationSize = memReq.size;
	allocInfo.memoryTypeIndex = find_memory_type(
		memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK(vkAllocateMemory(device_, &allocInfo, nullptr, &depthMemory_));
	vkBindImageMemory(device_, depthImage_, depthMemory_, 0);

	depthView_ =
		create_image_view(depthImage_, depthFmt, VK_IMAGE_ASPECT_DEPTH_BIT);
}

// =============================================================================
// Framebuffers
// =============================================================================

void Renderer::create_framebuffers()
{
	framebuffers_.resize(swapchainImageViews_.size());
	for (size_t i = 0; i < swapchainImageViews_.size(); ++i)
	{
		std::array<VkImageView, 2> att = {swapchainImageViews_[i], depthView_};
		VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
		ci.renderPass = renderPass_;
		ci.attachmentCount = static_cast<uint32_t>(att.size());
		ci.pAttachments = att.data();
		ci.width = swapchainExtent_.width;
		ci.height = swapchainExtent_.height;
		ci.layers = 1;
		VK_CHECK(vkCreateFramebuffer(device_, &ci, nullptr, &framebuffers_[i]));
	}
}

// =============================================================================
// Command pool & buffers
// =============================================================================

void Renderer::create_command_pool()
{
	VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
	ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	ci.queueFamilyIndex = graphicsFamily_;
	VK_CHECK(vkCreateCommandPool(device_, &ci, nullptr, &commandPool_));
}

void Renderer::create_command_buffers()
{
	commandBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
	VkCommandBufferAllocateInfo ai{
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
	ai.commandPool = commandPool_;
	ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	ai.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
	VK_CHECK(vkAllocateCommandBuffers(device_, &ai, commandBuffers_.data()));
}

// =============================================================================
// Geometry & uniform buffers
// =============================================================================

void Renderer::create_vertex_buffer()
{
	VkDeviceSize sz = sizeof(CUBE_VERTS[0]) * CUBE_VERTS.size();

	VkBuffer staging;
	VkDeviceMemory stagingMem;
	create_buffer(sz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
					  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				  staging, stagingMem);

	void* data;
	vkMapMemory(device_, stagingMem, 0, sz, 0, &data);
	std::memcpy(data, CUBE_VERTS.data(), sz);
	vkUnmapMemory(device_, stagingMem);

	create_buffer(
		sz,
		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer_,
		vertexBufferMemory_);
	copy_buffer(staging, vertexBuffer_, sz);

	vkDestroyBuffer(device_, staging, nullptr);
	vkFreeMemory(device_, stagingMem, nullptr);
}

void Renderer::create_index_buffer()
{
	indexCount_ = static_cast<uint32_t>(CUBE_INDICES.size());
	VkDeviceSize sz = sizeof(CUBE_INDICES[0]) * CUBE_INDICES.size();

	VkBuffer staging;
	VkDeviceMemory stagingMem;
	create_buffer(sz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
					  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				  staging, stagingMem);

	void* data;
	vkMapMemory(device_, stagingMem, 0, sz, 0, &data);
	std::memcpy(data, CUBE_INDICES.data(), sz);
	vkUnmapMemory(device_, stagingMem);

	create_buffer(
		sz, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer_, indexBufferMemory_);
	copy_buffer(staging, indexBuffer_, sz);

	vkDestroyBuffer(device_, staging, nullptr);
	vkFreeMemory(device_, stagingMem, nullptr);
}

void Renderer::create_uniform_buffers()
{
	VkDeviceSize sz = sizeof(UniformBufferObject);
	uniformBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
	uniformBuffersMemory_.resize(MAX_FRAMES_IN_FLIGHT);
	uniformBuffersMapped_.resize(MAX_FRAMES_IN_FLIGHT);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		create_buffer(sz, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
					  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
						  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					  uniformBuffers_[i], uniformBuffersMemory_[i]);
		vkMapMemory(device_, uniformBuffersMemory_[i], 0, sz, 0,
					&uniformBuffersMapped_[i]);
	}
}

// =============================================================================
// Descriptors
// =============================================================================

void Renderer::create_descriptor_pool()
{
	std::array<VkDescriptorPoolSize, 2> poolSizes = {{
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		 static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT)},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		 static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT)},
	}};

	VkDescriptorPoolCreateInfo ci{
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	ci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	ci.pPoolSizes = poolSizes.data();
	ci.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	VK_CHECK(vkCreateDescriptorPool(device_, &ci, nullptr, &descriptorPool_));
}

void Renderer::create_descriptor_sets()
{
	std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT,
											   descriptorSetLayout_);
	VkDescriptorSetAllocateInfo ai{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
	ai.descriptorPool = descriptorPool_;
	ai.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	ai.pSetLayouts = layouts.data();
	descriptorSets_.resize(MAX_FRAMES_IN_FLIGHT);
	VK_CHECK(vkAllocateDescriptorSets(device_, &ai, descriptorSets_.data()));

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		VkDescriptorBufferInfo bufInfo{uniformBuffers_[i], 0,
									   sizeof(UniformBufferObject)};

		VkDescriptorImageInfo imageInfo{};
		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo.imageView = textureImageView_;
		imageInfo.sampler = textureSampler_;

		std::array<VkWriteDescriptorSet, 2> writes{};

		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = descriptorSets_[i];
		writes[0].dstBinding = 0;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[0].descriptorCount = 1;
		writes[0].pBufferInfo = &bufInfo;

		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = descriptorSets_[i];
		writes[1].dstBinding = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[1].descriptorCount = 1;
		writes[1].pImageInfo = &imageInfo;

		vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()),
							   writes.data(), 0, nullptr);
	}
}

// =============================================================================
// Sync objects
// =============================================================================

void Renderer::create_sync_objects()
{
	imageAvailableSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);
	inFlightFences_.resize(MAX_FRAMES_IN_FLIGHT);
	// One render-finished semaphore per swapchain image to avoid reuse
	// conflicts
	renderFinishedSemaphores_.resize(swapchainImages_.size());

	VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		VK_CHECK(vkCreateSemaphore(device_, &sci, nullptr,
								   &imageAvailableSemaphores_[i]));
		VK_CHECK(vkCreateFence(device_, &fci, nullptr, &inFlightFences_[i]));
	}
	for (size_t i = 0; i < swapchainImages_.size(); ++i)
		VK_CHECK(vkCreateSemaphore(device_, &sci, nullptr,
								   &renderFinishedSemaphores_[i]));
}

// =============================================================================
// Helpers
// =============================================================================

uint32_t Renderer::find_memory_type(uint32_t filter,
									VkMemoryPropertyFlags props)
{
	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
	for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
		if ((filter & (1 << i)) &&
			(memProps.memoryTypes[i].propertyFlags & props) == props)
			return i;
	throw std::runtime_error("No suitable memory type");
}

void Renderer::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
							 VkMemoryPropertyFlags props, VkBuffer& buffer,
							 VkDeviceMemory& memory)
{
	VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	ci.size = size;
	ci.usage = usage;
	ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CHECK(vkCreateBuffer(device_, &ci, nullptr, &buffer));

	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(device_, buffer, &req);
	VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	ai.allocationSize = req.size;
	ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits, props);
	VK_CHECK(vkAllocateMemory(device_, &ai, nullptr, &memory));
	vkBindBufferMemory(device_, buffer, memory, 0);
}

void Renderer::copy_buffer(VkBuffer src, VkBuffer dst, VkDeviceSize size)
{
	VkCommandBuffer cmd = begin_single_time_commands();
	VkBufferCopy region{0, 0, size};
	vkCmdCopyBuffer(cmd, src, dst, 1, &region);
	end_single_time_commands(cmd);
}

VkCommandBuffer Renderer::begin_single_time_commands()
{
	VkCommandBufferAllocateInfo ai{
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
	ai.commandPool = commandPool_;
	ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	ai.commandBufferCount = 1;
	VkCommandBuffer cmd;
	vkAllocateCommandBuffers(device_, &ai, &cmd);
	VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(cmd, &bi);
	return cmd;
}

void Renderer::end_single_time_commands(VkCommandBuffer cmd)
{
	vkEndCommandBuffer(cmd);
	VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
	si.commandBufferCount = 1;
	si.pCommandBuffers = &cmd;
	vkQueueSubmit(graphicsQueue_, 1, &si, VK_NULL_HANDLE);
	vkQueueWaitIdle(graphicsQueue_);
	vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
}

// =============================================================================
// Texture loading
// =============================================================================

void Renderer::create_texture_image()
{
	auto png_data = packFile_->read("textures/grids/1024/BlueGrid.png");
	int texW, texH, texCh;
	stbi_uc* pixels =
		stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(png_data.data()),
							  static_cast<int>(png_data.size()), &texW, &texH,
							  &texCh, STBI_rgb_alpha);
	if (!pixels) throw std::runtime_error("Failed to decode texture from pack");

	VkDeviceSize imageSize = static_cast<VkDeviceSize>(texW) * texH * 4;

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingMemory;
	create_buffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
					  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				  stagingBuffer, stagingMemory);

	void* data;
	vkMapMemory(device_, stagingMemory, 0, imageSize, 0, &data);
	std::memcpy(data, pixels, imageSize);
	vkUnmapMemory(device_, stagingMemory);

	stbi_image_free(pixels);

	VkImageCreateInfo imgCI{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
	imgCI.imageType = VK_IMAGE_TYPE_2D;
	imgCI.format = VK_FORMAT_R8G8B8A8_SRGB;
	imgCI.extent = {static_cast<uint32_t>(texW), static_cast<uint32_t>(texH),
					1};
	imgCI.mipLevels = 1;
	imgCI.arrayLayers = 1;
	imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgCI.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imgCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK(vkCreateImage(device_, &imgCI, nullptr, &textureImage_));

	VkMemoryRequirements memReq;
	vkGetImageMemoryRequirements(device_, textureImage_, &memReq);
	VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	allocInfo.allocationSize = memReq.size;
	allocInfo.memoryTypeIndex = find_memory_type(
		memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK(
		vkAllocateMemory(device_, &allocInfo, nullptr, &textureImageMemory_));
	vkBindImageMemory(device_, textureImage_, textureImageMemory_, 0);

	transition_image_layout(textureImage_, VK_IMAGE_LAYOUT_UNDEFINED,
							VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	copy_buffer_to_image(stagingBuffer, textureImage_,
						 static_cast<uint32_t>(texW),
						 static_cast<uint32_t>(texH));
	transition_image_layout(textureImage_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
							VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	vkDestroyBuffer(device_, stagingBuffer, nullptr);
	vkFreeMemory(device_, stagingMemory, nullptr);
}

void Renderer::create_texture_image_view()
{
	textureImageView_ = create_image_view(
		textureImage_, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);
}

void Renderer::create_texture_sampler()
{
	VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
	ci.magFilter = VK_FILTER_LINEAR;
	ci.minFilter = VK_FILTER_LINEAR;
	ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(physicalDevice_, &props);
	ci.anisotropyEnable = VK_TRUE;
	ci.maxAnisotropy = props.limits.maxSamplerAnisotropy;

	ci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	ci.unnormalizedCoordinates = VK_FALSE;
	ci.compareEnable = VK_FALSE;
	ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

	VK_CHECK(vkCreateSampler(device_, &ci, nullptr, &textureSampler_));
}

void Renderer::transition_image_layout(VkImage image, VkImageLayout oldLayout,
									   VkImageLayout newLayout)
{
	VkCommandBuffer cmd = begin_single_time_commands();

	VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	VkPipelineStageFlags srcStage, dstStage;

	if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
		newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
	{
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
		dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	}
	else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
			 newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
	{
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	}
	else
	{
		throw std::runtime_error("Unsupported layout transition");
	}

	vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1,
						 &barrier);

	end_single_time_commands(cmd);
}

void Renderer::copy_buffer_to_image(VkBuffer buffer, VkImage image,
									uint32_t width, uint32_t height)
{
	VkCommandBuffer cmd = begin_single_time_commands();

	VkBufferImageCopy region{};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = {0, 0, 0};
	region.imageExtent = {width, height, 1};

	vkCmdCopyBufferToImage(cmd, buffer, image,
						   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	end_single_time_commands(cmd);
}

VkImageView Renderer::create_image_view(VkImage image, VkFormat format,
										VkImageAspectFlags aspect)
{
	VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
	ci.image = image;
	ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	ci.format = format;
	ci.subresourceRange.aspectMask = aspect;
	ci.subresourceRange.baseMipLevel = 0;
	ci.subresourceRange.levelCount = 1;
	ci.subresourceRange.baseArrayLayer = 0;
	ci.subresourceRange.layerCount = 1;
	VkImageView view;
	VK_CHECK(vkCreateImageView(device_, &ci, nullptr, &view));
	return view;
}

VkFormat Renderer::find_supported_format(
	const std::vector<VkFormat>& candidates, VkImageTiling tiling,
	VkFormatFeatureFlags features)
{
	for (VkFormat fmt : candidates)
	{
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(physicalDevice_, fmt, &props);
		if (tiling == VK_IMAGE_TILING_LINEAR &&
			(props.linearTilingFeatures & features) == features)
			return fmt;
		if (tiling == VK_IMAGE_TILING_OPTIMAL &&
			(props.optimalTilingFeatures & features) == features)
			return fmt;
	}
	throw std::runtime_error("No supported format found");
}

VkFormat Renderer::find_depth_format()
{
	return find_supported_format(
		{VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
		 VK_FORMAT_D24_UNORM_S8_UINT},
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

VkShaderModule Renderer::create_shader_module(const std::vector<char>& code)
{
	VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
	ci.codeSize = code.size();
	ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
	VkShaderModule mod;
	VK_CHECK(vkCreateShaderModule(device_, &ci, nullptr, &mod));
	return mod;
}
