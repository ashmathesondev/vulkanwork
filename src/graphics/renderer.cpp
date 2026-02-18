#include "renderer.h"

#include "camera.h"
#include "config.h"
#include "cube.h"
#include "debugLines.h"
#include "gltfLoader.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
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

void Renderer::init(GLFWwindow* window, const std::string& modelPath)
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
	create_depth_resources();
	create_framebuffers();
	create_command_pool();
	create_pbr_sampler();
	create_default_textures();
	create_pbr_descriptor_layouts();
	create_light_data_set_layout();
	create_depth_only_render_pass();
	create_depth_only_framebuffer();
	create_depth_prepass_pipeline();
	create_pbr_pipeline();
	create_compute_pipeline();
	create_heatmap_pipeline();
	create_debug_line_pipeline();
	create_debug_line_buffers();
	create_uniform_buffers();
	create_frame_descriptor_pool();
	create_frame_descriptor_sets();
	create_light_buffers();
	create_light_descriptor_pool();
	create_light_descriptor_sets();
	load_scene(modelPath);
	create_command_buffers();
	create_sync_objects();
}

void Renderer::cleanup()
{
	cleanup_swapchain();

	// Uniform buffers
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		vkDestroyBuffer(device_, uniformBuffers_[i], nullptr);
		vkFreeMemory(device_, uniformBuffersMemory_[i], nullptr);
	}

	// Light SSBOs
	cleanup_light_buffers();

	// Meshes
	for (auto& m : meshes_)
	{
		vkDestroyBuffer(device_, m.vertexBuffer, nullptr);
		vkFreeMemory(device_, m.vertexMemory, nullptr);
		vkDestroyBuffer(device_, m.indexBuffer, nullptr);
		vkFreeMemory(device_, m.indexMemory, nullptr);
	}

	// Material factor buffers
	for (auto& m : materials_)
	{
		if (m.factorBuffer) vkDestroyBuffer(device_, m.factorBuffer, nullptr);
		if (m.factorMemory) vkFreeMemory(device_, m.factorMemory, nullptr);
	}

	// Textures
	for (auto& t : textures_) destroy_texture(t);
	destroy_texture(defaultWhite_);
	destroy_texture(defaultNormal_);

	// Samplers
	vkDestroySampler(device_, pbrSampler_, nullptr);
	vkDestroySampler(device_, depthSampler_, nullptr);

	// Descriptor pools
	vkDestroyDescriptorPool(device_, materialDescriptorPool_, nullptr);
	vkDestroyDescriptorPool(device_, frameDescriptorPool_, nullptr);
	vkDestroyDescriptorPool(device_, lightDescriptorPool_, nullptr);

	// Descriptor layouts
	vkDestroyDescriptorSetLayout(device_, materialSetLayout_, nullptr);
	vkDestroyDescriptorSetLayout(device_, frameSetLayout_, nullptr);
	vkDestroyDescriptorSetLayout(device_, lightDataSetLayout_, nullptr);

	// Sync
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		vkDestroySemaphore(device_, imageAvailableSemaphores_[i], nullptr);
		vkDestroyFence(device_, inFlightFences_[i], nullptr);
	}
	for (auto s : renderFinishedSemaphores_)
		vkDestroySemaphore(device_, s, nullptr);

	vkDestroyCommandPool(device_, commandPool_, nullptr);

	// Debug line buffers
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		if (debugLineVertexBuffers_[i])
		{
			vkDestroyBuffer(device_, debugLineVertexBuffers_[i], nullptr);
			vkFreeMemory(device_, debugLineVertexMemory_[i], nullptr);
		}
	}

	// Pipelines
	vkDestroyPipeline(device_, pbrPipeline_, nullptr);
	vkDestroyPipelineLayout(device_, pbrPipelineLayout_, nullptr);
	vkDestroyPipeline(device_, depthPrepassPipeline_, nullptr);
	vkDestroyPipelineLayout(device_, depthPrepassPipelineLayout_, nullptr);
	vkDestroyPipeline(device_, lightCullPipeline_, nullptr);
	vkDestroyPipelineLayout(device_, computePipelineLayout_, nullptr);
	vkDestroyPipeline(device_, heatmapPipeline_, nullptr);
	vkDestroyPipelineLayout(device_, heatmapPipelineLayout_, nullptr);
	vkDestroyPipeline(device_, debugLinePipeline_, nullptr);
	vkDestroyPipelineLayout(device_, debugLinePipelineLayout_, nullptr);

	// Render passes
	vkDestroyRenderPass(device_, renderPass_, nullptr);
	vkDestroyRenderPass(device_, depthOnlyRenderPass_, nullptr);

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

	// ---- 1. Depth pre-pass ----
	if (!debugSkipDepthPrepass_)
		draw_depth_prepass(cmd);
	else
	{
		// Still need to transition depth image for compute read
		// Do a clear + transition via a minimal render pass
		VkClearValue clear{};
		clear.depthStencil = {1.0f, 0};
		VkRenderPassBeginInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
		rpInfo.renderPass = depthOnlyRenderPass_;
		rpInfo.framebuffer = depthOnlyFramebuffer_;
		rpInfo.renderArea = {{0, 0}, swapchainExtent_};
		rpInfo.clearValueCount = 1;
		rpInfo.pClearValues = &clear;
		vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdEndRenderPass(cmd);
	}

	// ---- 2. Barrier: depth attachment -> shader read for compute ----
	{
		VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = depthImage_;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
							 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
							 nullptr, 0, nullptr, 1, &barrier);
	}

	// ---- 3. Light culling compute dispatch ----
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
						  lightCullPipeline_);
		VkDescriptorSet compSets[] = {frameDescriptorSets_[currentFrame_],
									  lightDescriptorSets_[currentFrame_]};
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
								computePipelineLayout_, 0, 2, compSets, 0,
								nullptr);
		vkCmdDispatch(cmd, tileCountX_, tileCountY_, 1);
	}

	// ---- 4. Barriers: compute -> fragment (SSBO + depth back) ----
	{
		VkMemoryBarrier memBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
		memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		VkImageMemoryBarrier depthBarrier{
			VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		depthBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		depthBarrier.newLayout =
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		depthBarrier.image = depthImage_;
		depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		depthBarrier.subresourceRange.baseMipLevel = 0;
		depthBarrier.subresourceRange.levelCount = 1;
		depthBarrier.subresourceRange.baseArrayLayer = 0;
		depthBarrier.subresourceRange.layerCount = 1;
		depthBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		depthBarrier.dstAccessMask =
			VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
							 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
								 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
							 0, 1, &memBarrier, 0, nullptr, 1, &depthBarrier);
	}

	// ---- 5. Begin main shading render pass (depth loadOp=LOAD) ----
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

void Renderer::update_uniforms(const Camera& camera, float time,
							   const LightEnvironment& lights)
{
	FrameUBO ubo{};
	ubo.view = camera.view_matrix();

	float aspect = static_cast<float>(swapchainExtent_.width) /
				   static_cast<float>(swapchainExtent_.height);
	ubo.proj = glm::perspective(glm::radians(camera.fov), aspect, 0.1f, 100.0f);
	ubo.proj[1][1] *= -1.0f;  // Vulkan Y-flip

	// Cache for picking / gizmo (store un-flipped proj for ImGuizmo)
	lastView_ = ubo.view;
	lastProj_ =
		glm::perspective(glm::radians(camera.fov), aspect, 0.1f, 100.0f);
	ubo.invProj = glm::inverse(ubo.proj);

	ubo.cameraPos = camera.position;
	ubo.lightCount = lights.total_light_count();
	ubo.ambientColor = lights.ambient.color * lights.ambient.intensity;
	ubo.tileCountX = tileCountX_;
	ubo.tileCountY = tileCountY_;
	ubo.screenWidth = swapchainExtent_.width;
	ubo.screenHeight = swapchainExtent_.height;

	std::memcpy(uniformBuffersMapped_[currentFrame_], &ubo, sizeof(ubo));

	// Upload light SSBO
	auto gpuLights = lights.pack_gpu_lights();
	uint32_t count = static_cast<uint32_t>(gpuLights.size());
	if (count > MAX_LIGHTS) count = MAX_LIGHTS;
	if (count > 0)
	{
		std::memcpy(lightSSBOMapped_[currentFrame_], gpuLights.data(),
					count * sizeof(GPULight));
	}
}

void Renderer::draw_scene(VkCommandBuffer cmd)
{
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pbrPipeline_);

	// Dynamic rasterizer state (debug toggles)
	vkCmdSetCullMode(
		cmd, debugDisableCulling_ ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
	vkCmdSetFrontFace(cmd, debugFrontFace_ == 1
							   ? VK_FRONT_FACE_CLOCKWISE
							   : VK_FRONT_FACE_COUNTER_CLOCKWISE);

	// Bind frame descriptor set (set 0)
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
							pbrPipelineLayout_, 0, 1,
							&frameDescriptorSets_[currentFrame_], 0, nullptr);

	// Bind light data descriptor set (set 2)
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
							pbrPipelineLayout_, 2, 1,
							&lightDescriptorSets_[currentFrame_], 0, nullptr);

	for (const auto& mesh : meshes_)
	{
		// Push model matrix
		vkCmdPushConstants(cmd, pbrPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT,
						   0, sizeof(glm::mat4), &mesh.transform);

		// Bind material descriptor set (set 1)
		if (mesh.materialIndex < materials_.size())
		{
			vkCmdBindDescriptorSets(
				cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pbrPipelineLayout_, 1, 1,
				&materials_[mesh.materialIndex].descriptorSet, 0, nullptr);
		}

		VkBuffer vbufs[] = {mesh.vertexBuffer};
		VkDeviceSize offs[] = {0};
		vkCmdBindVertexBuffers(cmd, 0, 1, vbufs, offs);
		vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, static_cast<uint32_t>(mesh.indices.size()), 1, 0,
						 0, 0);
	}

	// Heatmap debug overlay
	if (showHeatmap_)
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
						  heatmapPipeline_);
		vkCmdBindDescriptorSets(
			cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, heatmapPipelineLayout_, 0, 1,
			&frameDescriptorSets_[currentFrame_], 0, nullptr);
		vkCmdBindDescriptorSets(
			cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, heatmapPipelineLayout_, 1, 1,
			&lightDescriptorSets_[currentFrame_], 0, nullptr);
		vkCmdDraw(cmd, 3, 1, 0, 0);
	}

	// Debug light wireframes
	if (showDebugLines_ && debugLineVertexCount_ > 0)
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
						  debugLinePipeline_);
		vkCmdBindDescriptorSets(
			cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, debugLinePipelineLayout_, 0,
			1, &frameDescriptorSets_[currentFrame_], 0, nullptr);
		VkBuffer vbufs[] = {debugLineVertexBuffers_[currentFrame_]};
		VkDeviceSize offs[] = {0};
		vkCmdBindVertexBuffers(cmd, 0, 1, vbufs, offs);
		vkCmdDraw(cmd, debugLineVertexCount_, 1, 0, 0);
	}
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

	VkSurfaceFormatKHR fmt = fmts[0];
	for (auto& f : fmts)
		if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
			f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
		{
			fmt = f;
			break;
		}

	VkPresentModeKHR pm = VK_PRESENT_MODE_FIFO_KHR;
	for (auto m : pms)
		if (m == VK_PRESENT_MODE_MAILBOX_KHR)
		{
			pm = m;
			break;
		}

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

	for (auto s : renderFinishedSemaphores_)
		vkDestroySemaphore(device_, s, nullptr);
	renderFinishedSemaphores_.clear();

	create_swapchain();
	create_image_views();
	create_depth_resources();
	create_depth_only_framebuffer();
	create_framebuffers();

	// Recreate tile light SSBOs (size depends on resolution)
	cleanup_light_buffers();
	create_light_buffers();
	create_light_descriptor_pool();
	create_light_descriptor_sets();

	VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	renderFinishedSemaphores_.resize(swapchainImages_.size());
	for (size_t i = 0; i < swapchainImages_.size(); ++i)
		VK_CHECK(vkCreateSemaphore(device_, &sci, nullptr,
								   &renderFinishedSemaphores_[i]));
}

void Renderer::cleanup_swapchain()
{
	if (depthOnlyFramebuffer_)
		vkDestroyFramebuffer(device_, depthOnlyFramebuffer_, nullptr);
	depthOnlyFramebuffer_ = VK_NULL_HANDLE;

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
	depthAtt.loadOp =
		VK_ATTACHMENT_LOAD_OP_CLEAR;  // main pass does its own depth test
	depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAtt.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
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
// PBR descriptor layouts
// =============================================================================

void Renderer::create_pbr_descriptor_layouts()
{
	// Set 0: per-frame (UBO with view, proj, cameraPos, lightDir, lightColor)
	{
		VkDescriptorSetLayoutBinding uboBinding{};
		uboBinding.binding = 0;
		uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		uboBinding.descriptorCount = 1;
		uboBinding.stageFlags =
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo ci{
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		ci.bindingCount = 1;
		ci.pBindings = &uboBinding;
		VK_CHECK(vkCreateDescriptorSetLayout(device_, &ci, nullptr,
											 &frameSetLayout_));
	}

	// Set 1: per-material (4 combined image samplers + 1 UBO for factors)
	{
		std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
		for (uint32_t i = 0; i < 4; ++i)
		{
			bindings[i].binding = i;
			bindings[i].descriptorType =
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			bindings[i].descriptorCount = 1;
			bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
		}
		// Binding 4: material factors UBO
		bindings[4].binding = 4;
		bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[4].descriptorCount = 1;
		bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo ci{
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		ci.bindingCount = static_cast<uint32_t>(bindings.size());
		ci.pBindings = bindings.data();
		VK_CHECK(vkCreateDescriptorSetLayout(device_, &ci, nullptr,
											 &materialSetLayout_));
	}
}

// =============================================================================
// PBR pipeline
// =============================================================================

void Renderer::create_pbr_pipeline()
{
	auto vertCode = packFile_->read("shaders/pbr.vert.spv");
	auto fragCode = packFile_->read("shaders/pbr.frag.spv");

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

	VkDynamicState dynStates[] = {
		VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE};
	VkPipelineDynamicStateCreateInfo dyn{
		VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
	dyn.dynamicStateCount = 4;
	dyn.pDynamicStates = dynStates;

	// Pipeline layout: set 0=frame, set 1=material, set 2=lightData, push=model
	VkDescriptorSetLayout setLayouts[] = {frameSetLayout_, materialSetLayout_,
										  lightDataSetLayout_};

	VkPushConstantRange pushRange{};
	pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pushRange.offset = 0;
	pushRange.size = sizeof(glm::mat4);

	VkPipelineLayoutCreateInfo layoutCI{
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	layoutCI.setLayoutCount = 3;
	layoutCI.pSetLayouts = setLayouts;
	layoutCI.pushConstantRangeCount = 1;
	layoutCI.pPushConstantRanges = &pushRange;
	VK_CHECK(vkCreatePipelineLayout(device_, &layoutCI, nullptr,
									&pbrPipelineLayout_));

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
	ci.layout = pbrPipelineLayout_;
	ci.renderPass = renderPass_;
	ci.subpass = 0;

	VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr,
									   &pbrPipeline_));

	vkDestroyShaderModule(device_, fragMod, nullptr);
	vkDestroyShaderModule(device_, vertMod, nullptr);
}

// =============================================================================
// PBR sampler
// =============================================================================

void Renderer::create_pbr_sampler()
{
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(physicalDevice_, &props);

	VkSamplerCreateInfo ci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
	ci.magFilter = VK_FILTER_LINEAR;
	ci.minFilter = VK_FILTER_LINEAR;
	ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	ci.anisotropyEnable = VK_TRUE;
	ci.maxAnisotropy = props.limits.maxSamplerAnisotropy;
	ci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	ci.unnormalizedCoordinates = VK_FALSE;
	ci.compareEnable = VK_FALSE;
	ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	ci.minLod = 0.0f;
	ci.maxLod = VK_LOD_CLAMP_NONE;

	VK_CHECK(vkCreateSampler(device_, &ci, nullptr, &pbrSampler_));
}

// =============================================================================
// Default textures
// =============================================================================

void Renderer::create_default_textures()
{
	defaultWhite_ = Texture::solid_color(255, 255, 255, 255, true);
	upload_texture(defaultWhite_);

	// 1x1 flat normal (0.5, 0.5, 1.0 in unorm = tangent-space up)
	defaultNormal_ = Texture::solid_color(128, 128, 255, 255, false);
	upload_texture(defaultNormal_);
}

// =============================================================================
// Texture upload with mipmaps
// =============================================================================

void Renderer::upload_texture(Texture& tex)
{
	uint32_t mipLevels = static_cast<uint32_t>(std::floor(
							 std::log2(std::max(tex.width, tex.height)))) +
						 1;
	tex.mipLevels = mipLevels;

	VkDeviceSize imageSize =
		static_cast<VkDeviceSize>(tex.width) * tex.height * 4;

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingMemory;
	create_buffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
					  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				  stagingBuffer, stagingMemory);

	void* data;
	vkMapMemory(device_, stagingMemory, 0, imageSize, 0, &data);
	std::memcpy(data, tex.pixels.data(), imageSize);
	vkUnmapMemory(device_, stagingMemory);

	VkFormat format =
		tex.isSrgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

	VkImageCreateInfo imgCI{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
	imgCI.imageType = VK_IMAGE_TYPE_2D;
	imgCI.format = format;
	imgCI.extent = {tex.width, tex.height, 1};
	imgCI.mipLevels = mipLevels;
	imgCI.arrayLayers = 1;
	imgCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imgCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgCI.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
				  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imgCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imgCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VK_CHECK(vkCreateImage(device_, &imgCI, nullptr, &tex.image));

	VkMemoryRequirements memReq;
	vkGetImageMemoryRequirements(device_, tex.image, &memReq);
	VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	allocInfo.allocationSize = memReq.size;
	allocInfo.memoryTypeIndex = find_memory_type(
		memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK(vkAllocateMemory(device_, &allocInfo, nullptr, &tex.memory));
	vkBindImageMemory(device_, tex.image, tex.memory, 0);

	// Transition all mip levels to TRANSFER_DST
	{
		VkCommandBuffer cmd = begin_single_time_commands();
		VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = tex.image;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = mipLevels;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
							 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
							 nullptr, 1, &barrier);
		end_single_time_commands(cmd);
	}

	// Copy staging buffer to mip level 0
	{
		VkCommandBuffer cmd = begin_single_time_commands();
		VkBufferImageCopy region{};
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageExtent = {tex.width, tex.height, 1};
		vkCmdCopyBufferToImage(cmd, stagingBuffer, tex.image,
							   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
							   &region);
		end_single_time_commands(cmd);
	}

	vkDestroyBuffer(device_, stagingBuffer, nullptr);
	vkFreeMemory(device_, stagingMemory, nullptr);

	generate_mipmaps(tex.image, format, tex.width, tex.height, mipLevels);

	tex.view = create_image_view(tex.image, format, VK_IMAGE_ASPECT_COLOR_BIT,
								 mipLevels);
}

void Renderer::generate_mipmaps(VkImage image, VkFormat format, uint32_t width,
								uint32_t height, uint32_t mipLevels)
{
	// Check if format supports linear blit
	VkFormatProperties fmtProps;
	vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &fmtProps);
	if (!(fmtProps.optimalTilingFeatures &
		  VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
	{
		// Fallback: just transition all to shader read
		VkCommandBuffer cmd = begin_single_time_commands();
		VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		barrier.image = image;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = mipLevels;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
							 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
							 nullptr, 0, nullptr, 1, &barrier);
		end_single_time_commands(cmd);
		return;
	}

	VkCommandBuffer cmd = begin_single_time_commands();

	int32_t mipW = static_cast<int32_t>(width);
	int32_t mipH = static_cast<int32_t>(height);

	for (uint32_t i = 1; i < mipLevels; ++i)
	{
		// Transition level i-1 from TRANSFER_DST to TRANSFER_SRC
		VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		barrier.image = image;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
							 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
							 nullptr, 1, &barrier);

		// Blit from level i-1 to level i
		VkImageBlit blit{};
		blit.srcOffsets[0] = {0, 0, 0};
		blit.srcOffsets[1] = {mipW, mipH, 1};
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;
		int32_t nextW = mipW > 1 ? mipW / 2 : 1;
		int32_t nextH = mipH > 1 ? mipH / 2 : 1;
		blit.dstOffsets[0] = {0, 0, 0};
		blit.dstOffsets[1] = {nextW, nextH, 1};
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;
		vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image,
					   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
					   VK_FILTER_LINEAR);

		// Transition level i-1 to SHADER_READ
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
							 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
							 nullptr, 0, nullptr, 1, &barrier);

		mipW = nextW;
		mipH = nextH;
	}

	// Transition last mip level to SHADER_READ
	VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
	barrier.image = image;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = mipLevels - 1;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
						 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
						 0, nullptr, 1, &barrier);

	end_single_time_commands(cmd);
}

// =============================================================================
// Mesh upload
// =============================================================================

void Renderer::upload_mesh(Mesh& mesh)
{
	// Vertex buffer
	{
		VkDeviceSize sz = sizeof(Vertex) * mesh.vertices.size();
		VkBuffer staging;
		VkDeviceMemory stagingMem;
		create_buffer(sz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
						  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					  staging, stagingMem);
		void* data;
		vkMapMemory(device_, stagingMem, 0, sz, 0, &data);
		std::memcpy(data, mesh.vertices.data(), sz);
		vkUnmapMemory(device_, stagingMem);
		create_buffer(sz,
					  VK_BUFFER_USAGE_TRANSFER_DST_BIT |
						  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
					  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.vertexBuffer,
					  mesh.vertexMemory);
		copy_buffer(staging, mesh.vertexBuffer, sz);
		vkDestroyBuffer(device_, staging, nullptr);
		vkFreeMemory(device_, stagingMem, nullptr);
	}

	// Index buffer
	{
		VkDeviceSize sz = sizeof(uint32_t) * mesh.indices.size();
		VkBuffer staging;
		VkDeviceMemory stagingMem;
		create_buffer(sz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
					  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
						  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					  staging, stagingMem);
		void* data;
		vkMapMemory(device_, stagingMem, 0, sz, 0, &data);
		std::memcpy(data, mesh.indices.data(), sz);
		vkUnmapMemory(device_, stagingMem);
		create_buffer(
			sz,
			VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mesh.indexBuffer,
			mesh.indexMemory);
		copy_buffer(staging, mesh.indexBuffer, sz);
		vkDestroyBuffer(device_, staging, nullptr);
		vkFreeMemory(device_, stagingMem, nullptr);
	}
}

// =============================================================================
// Material descriptors
// =============================================================================

void Renderer::create_material_descriptor_pool(uint32_t materialCount)
{
	if (materialCount == 0) materialCount = 1;

	std::array<VkDescriptorPoolSize, 2> poolSizes{};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[0].descriptorCount =
		materialCount * 4;	// 4 samplers per material
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[1].descriptorCount = materialCount;  // 1 factor UBO per material

	VkDescriptorPoolCreateInfo ci{
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	ci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	ci.pPoolSizes = poolSizes.data();
	ci.maxSets = materialCount;
	VK_CHECK(vkCreateDescriptorPool(device_, &ci, nullptr,
									&materialDescriptorPool_));
}

void Renderer::create_material_descriptor(Material& mat)
{
	VkDescriptorSetAllocateInfo ai{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
	ai.descriptorPool = materialDescriptorPool_;
	ai.descriptorSetCount = 1;
	ai.pSetLayouts = &materialSetLayout_;
	VK_CHECK(vkAllocateDescriptorSets(device_, &ai, &mat.descriptorSet));

	// Create factor UBO
	MaterialFactorsGPU factors{};
	factors.baseColorFactor = mat.baseColorFactor;
	factors.metallicFactor = mat.metallicFactor;
	factors.roughnessFactor = mat.roughnessFactor;
	factors.emissiveFactor = glm::vec4(mat.emissiveFactor, 0.0f);

	create_buffer(sizeof(MaterialFactorsGPU),
				  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
					  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				  mat.factorBuffer, mat.factorMemory);

	void* data;
	vkMapMemory(device_, mat.factorMemory, 0, sizeof(MaterialFactorsGPU), 0,
				&data);
	std::memcpy(data, &factors, sizeof(MaterialFactorsGPU));
	vkUnmapMemory(device_, mat.factorMemory);

	auto get_view = [&](int32_t texIndex,
						const Texture& fallback) -> VkImageView
	{
		if (texIndex >= 0 && texIndex < static_cast<int32_t>(textures_.size()))
			return textures_[texIndex].view;
		return fallback.view;
	};

	VkDescriptorImageInfo imageInfos[4]{};
	// 0: baseColor
	imageInfos[0].sampler = pbrSampler_;
	imageInfos[0].imageView = get_view(mat.baseColorTexture, defaultWhite_);
	imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	// 1: metallicRoughness
	imageInfos[1].sampler = pbrSampler_;
	imageInfos[1].imageView =
		get_view(mat.metallicRoughnessTexture, defaultWhite_);
	imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	// 2: normal
	imageInfos[2].sampler = pbrSampler_;
	imageInfos[2].imageView = get_view(mat.normalTexture, defaultNormal_);
	imageInfos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	// 3: emissive
	imageInfos[3].sampler = pbrSampler_;
	imageInfos[3].imageView = get_view(mat.emissiveTexture, defaultWhite_);
	imageInfos[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorBufferInfo factorBufInfo{mat.factorBuffer, 0,
										 sizeof(MaterialFactorsGPU)};

	std::array<VkWriteDescriptorSet, 5> writes{};
	for (uint32_t i = 0; i < 4; ++i)
	{
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstSet = mat.descriptorSet;
		writes[i].dstBinding = i;
		writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[i].descriptorCount = 1;
		writes[i].pImageInfo = &imageInfos[i];
	}
	// Binding 4: material factors UBO
	writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[4].dstSet = mat.descriptorSet;
	writes[4].dstBinding = 4;
	writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	writes[4].descriptorCount = 1;
	writes[4].pBufferInfo = &factorBufInfo;

	vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()),
						   writes.data(), 0, nullptr);
}

// =============================================================================
// Uniform buffers & frame descriptors
// =============================================================================

void Renderer::create_uniform_buffers()
{
	VkDeviceSize sz = sizeof(FrameUBO);
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

void Renderer::create_frame_descriptor_pool()
{
	VkDescriptorPoolSize poolSize{};
	poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSize.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

	VkDescriptorPoolCreateInfo ci{
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	ci.poolSizeCount = 1;
	ci.pPoolSizes = &poolSize;
	ci.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	VK_CHECK(
		vkCreateDescriptorPool(device_, &ci, nullptr, &frameDescriptorPool_));
}

void Renderer::create_frame_descriptor_sets()
{
	std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT,
											   frameSetLayout_);
	VkDescriptorSetAllocateInfo ai{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
	ai.descriptorPool = frameDescriptorPool_;
	ai.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	ai.pSetLayouts = layouts.data();
	frameDescriptorSets_.resize(MAX_FRAMES_IN_FLIGHT);
	VK_CHECK(
		vkAllocateDescriptorSets(device_, &ai, frameDescriptorSets_.data()));

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		VkDescriptorBufferInfo bufInfo{uniformBuffers_[i], 0, sizeof(FrameUBO)};

		VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		write.dstSet = frameDescriptorSets_[i];
		write.dstBinding = 0;
		write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		write.descriptorCount = 1;
		write.pBufferInfo = &bufInfo;

		vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
	}
}

// =============================================================================
// Scene loading
// =============================================================================

void Renderer::add_cube_to_scene(Scene& scene)
{
	// Load the BlueGrid texture from the pack file for the cube
	{
		auto png = packFile_->read("textures/grids/1024/BlueGrid.png");
		int w, h, ch;
		stbi_uc* pixels = stbi_load_from_memory(
			reinterpret_cast<const stbi_uc*>(png.data()),
			static_cast<int>(png.size()), &w, &h, &ch, STBI_rgb_alpha);
		if (!pixels)
			throw std::runtime_error("Failed to decode BlueGrid texture");

		Texture gridTex;
		gridTex.width = static_cast<uint32_t>(w);
		gridTex.height = static_cast<uint32_t>(h);
		gridTex.isSrgb = true;
		gridTex.pixels.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
		stbi_image_free(pixels);

		upload_texture(gridTex);
		scene.textures.push_back(std::move(gridTex));
	}
	int32_t gridTexIdx = static_cast<int32_t>(scene.textures.size()) - 1;

	// Add cube material that references the BlueGrid texture
	uint32_t cubeMaterialIdx = static_cast<uint32_t>(scene.materials.size());
	Material cubeMat{};
	cubeMat.baseColorTexture = gridTexIdx;
	cubeMat.metallicFactor = 0.0f;
	cubeMat.roughnessFactor = 0.5f;
	scene.materials.push_back(cubeMat);

	// Append cube mesh
	Mesh cube = make_cube_mesh();
	cube.name = "Cube";
	cube.materialIndex = cubeMaterialIdx;
	cube.transform =
		glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 0.0f, 0.0f));
	upload_mesh(cube);
	scene.meshes.push_back(std::move(cube));
}

void Renderer::load_scene(const std::string& modelPath)
{
	Scene scene = load_gltf(modelPath);

	// Upload glTF textures in-place
	for (auto& tex : scene.textures) upload_texture(tex);

	// Upload glTF meshes in-place
	for (auto& mesh : scene.meshes) upload_mesh(mesh);

	// Add the cube (BlueGrid texture + material + mesh)
	add_cube_to_scene(scene);

	// Move scene data into renderer members
	textures_ = std::move(scene.textures);
	meshes_ = std::move(scene.meshes);

	// Create material descriptors (includes the cube's material)
	create_material_descriptor_pool(
		static_cast<uint32_t>(scene.materials.size()));
	for (auto& mat : scene.materials) create_material_descriptor(mat);
	materials_ = std::move(scene.materials);
}

void Renderer::unload_scene()
{
	vkDeviceWaitIdle(device_);

	// Free mesh GPU buffers
	for (auto& m : meshes_)
	{
		vkDestroyBuffer(device_, m.vertexBuffer, nullptr);
		vkFreeMemory(device_, m.vertexMemory, nullptr);
		vkDestroyBuffer(device_, m.indexBuffer, nullptr);
		vkFreeMemory(device_, m.indexMemory, nullptr);
	}
	meshes_.clear();

	// Free material factor buffers
	for (auto& m : materials_)
	{
		if (m.factorBuffer) vkDestroyBuffer(device_, m.factorBuffer, nullptr);
		if (m.factorMemory) vkFreeMemory(device_, m.factorMemory, nullptr);
	}
	materials_.clear();

	// Free scene textures (but not default textures)
	for (auto& t : textures_) destroy_texture(t);
	textures_.clear();

	// Destroy material descriptor pool
	if (materialDescriptorPool_)
	{
		vkDestroyDescriptorPool(device_, materialDescriptorPool_, nullptr);
		materialDescriptorPool_ = VK_NULL_HANDLE;
	}
}

void Renderer::load_scene_empty()
{
	Scene scene;

	// Add the cube (BlueGrid texture + material + mesh)
	add_cube_to_scene(scene);

	// Move scene data into renderer members
	textures_ = std::move(scene.textures);
	meshes_ = std::move(scene.meshes);

	// Create material descriptors
	create_material_descriptor_pool(
		static_cast<uint32_t>(scene.materials.size()));
	for (auto& mat : scene.materials) create_material_descriptor(mat);
	materials_ = std::move(scene.materials);
}

// =============================================================================
// Import / Delete / Rebuild helpers
// =============================================================================

void Renderer::import_gltf(const std::string& path)
{
	vkDeviceWaitIdle(device_);

	Scene scene = load_gltf(path);

	uint32_t texOffset = static_cast<uint32_t>(textures_.size());
	uint32_t matOffset = static_cast<uint32_t>(materials_.size());

	// Upload and append textures
	for (auto& tex : scene.textures) upload_texture(tex);
	textures_.insert(textures_.end(),
					 std::make_move_iterator(scene.textures.begin()),
					 std::make_move_iterator(scene.textures.end()));

	// Offset material texture indices, then append
	for (auto& mat : scene.materials)
	{
		if (mat.baseColorTexture >= 0)
			mat.baseColorTexture += static_cast<int32_t>(texOffset);
		if (mat.metallicRoughnessTexture >= 0)
			mat.metallicRoughnessTexture += static_cast<int32_t>(texOffset);
		if (mat.normalTexture >= 0)
			mat.normalTexture += static_cast<int32_t>(texOffset);
		if (mat.emissiveTexture >= 0)
			mat.emissiveTexture += static_cast<int32_t>(texOffset);
	}

	// Offset mesh material indices, upload, then append
	for (auto& mesh : scene.meshes)
	{
		mesh.materialIndex += matOffset;
		upload_mesh(mesh);
	}
	meshes_.insert(meshes_.end(), std::make_move_iterator(scene.meshes.begin()),
				   std::make_move_iterator(scene.meshes.end()));

	// Append materials (without GPU descriptors yet)
	materials_.insert(materials_.end(),
					  std::make_move_iterator(scene.materials.begin()),
					  std::make_move_iterator(scene.materials.end()));

	rebuild_material_descriptors();
}

void Renderer::delete_mesh(uint32_t meshIdx)
{
	if (meshIdx >= meshes_.size()) return;

	vkDeviceWaitIdle(device_);

	// 1. Destroy mesh GPU buffers and erase
	auto& mesh = meshes_[meshIdx];
	vkDestroyBuffer(device_, mesh.vertexBuffer, nullptr);
	vkFreeMemory(device_, mesh.vertexMemory, nullptr);
	vkDestroyBuffer(device_, mesh.indexBuffer, nullptr);
	vkFreeMemory(device_, mesh.indexMemory, nullptr);

	uint32_t deletedMatIdx = mesh.materialIndex;
	meshes_.erase(meshes_.begin() + meshIdx);

	// 2. Fix up remaining mesh materialIndex for the erased mesh's shift
	//    (materialIndex doesn't shift yet — we handle that after removing mats)

	// 3. Collect unreferenced material indices
	std::vector<bool> matReferenced(materials_.size(), false);
	for (const auto& m : meshes_) matReferenced[m.materialIndex] = true;

	// Collect unreferenced texture indices from unreferenced materials
	std::vector<bool> texReferencedByUnrefMat(textures_.size(), false);
	for (uint32_t i = 0; i < materials_.size(); ++i)
	{
		if (!matReferenced[i])
		{
			auto markTex = [&](int32_t idx)
			{
				if (idx >= 0 && idx < static_cast<int32_t>(textures_.size()))
					texReferencedByUnrefMat[idx] = true;
			};
			markTex(materials_[i].baseColorTexture);
			markTex(materials_[i].metallicRoughnessTexture);
			markTex(materials_[i].normalTexture);
			markTex(materials_[i].emissiveTexture);
		}
	}

	// 4. Destroy unreferenced materials (reverse order to keep indices valid)
	std::vector<uint32_t> removedMatIndices;
	for (int32_t i = static_cast<int32_t>(materials_.size()) - 1; i >= 0; --i)
	{
		if (!matReferenced[i])
		{
			if (materials_[i].factorBuffer)
				vkDestroyBuffer(device_, materials_[i].factorBuffer, nullptr);
			if (materials_[i].factorMemory)
				vkFreeMemory(device_, materials_[i].factorMemory, nullptr);
			materials_.erase(materials_.begin() + i);
			removedMatIndices.push_back(static_cast<uint32_t>(i));
		}
	}

	// 5. Check which textures referenced by removed materials are truly
	//    unreferenced (not used by any remaining material)
	std::vector<bool> texReferenced(texReferencedByUnrefMat.size(), false);
	for (const auto& mat : materials_)
	{
		auto markTex = [&](int32_t idx)
		{
			if (idx >= 0 && idx < static_cast<int32_t>(texReferenced.size()))
				texReferenced[idx] = true;
		};
		markTex(mat.baseColorTexture);
		markTex(mat.metallicRoughnessTexture);
		markTex(mat.normalTexture);
		markTex(mat.emissiveTexture);
	}

	// Destroy unreferenced textures (reverse order)
	std::vector<uint32_t> removedTexIndices;
	for (int32_t i = static_cast<int32_t>(texReferencedByUnrefMat.size()) - 1;
		 i >= 0; --i)
	{
		if (texReferencedByUnrefMat[i] && !texReferenced[i])
		{
			destroy_texture(textures_[i]);
			textures_.erase(textures_.begin() + i);
			removedTexIndices.push_back(static_cast<uint32_t>(i));
		}
	}

	// 6. Fix up material indices in remaining meshes
	for (auto& m : meshes_)
	{
		uint32_t shift = 0;
		for (uint32_t removed : removedMatIndices)
			if (removed <= m.materialIndex) ++shift;
		m.materialIndex -= shift;
	}

	// 7. Fix up texture indices in remaining materials
	for (auto& mat : materials_)
	{
		auto fixTex = [&](int32_t& idx)
		{
			if (idx < 0) return;
			uint32_t shift = 0;
			for (uint32_t removed : removedTexIndices)
				if (removed <= static_cast<uint32_t>(idx)) ++shift;
			idx -= static_cast<int32_t>(shift);
		};
		fixTex(mat.baseColorTexture);
		fixTex(mat.metallicRoughnessTexture);
		fixTex(mat.normalTexture);
		fixTex(mat.emissiveTexture);
	}

	rebuild_material_descriptors();
}

void Renderer::rebuild_material_descriptors()
{
	// Destroy old factor buffers and descriptor pool
	for (auto& mat : materials_)
	{
		if (mat.factorBuffer)
		{
			vkDestroyBuffer(device_, mat.factorBuffer, nullptr);
			mat.factorBuffer = VK_NULL_HANDLE;
		}
		if (mat.factorMemory)
		{
			vkFreeMemory(device_, mat.factorMemory, nullptr);
			mat.factorMemory = VK_NULL_HANDLE;
		}
		mat.descriptorSet = VK_NULL_HANDLE;
	}

	if (materialDescriptorPool_)
	{
		vkDestroyDescriptorPool(device_, materialDescriptorPool_, nullptr);
		materialDescriptorPool_ = VK_NULL_HANDLE;
	}

	if (materials_.empty()) return;

	create_material_descriptor_pool(static_cast<uint32_t>(materials_.size()));
	for (auto& mat : materials_) create_material_descriptor(mat);
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
	imgCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
				  VK_IMAGE_USAGE_SAMPLED_BIT;
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

	// Depth sampler (nearest, clamp-to-edge) for compute light culling
	if (depthSampler_ == VK_NULL_HANDLE)
	{
		VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
		sci.magFilter = VK_FILTER_NEAREST;
		sci.minFilter = VK_FILTER_NEAREST;
		sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
		VK_CHECK(vkCreateSampler(device_, &sci, nullptr, &depthSampler_));
	}
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
// Sync objects
// =============================================================================

void Renderer::create_sync_objects()
{
	imageAvailableSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);
	inFlightFences_.resize(MAX_FRAMES_IN_FLIGHT);
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

VkImageView Renderer::create_image_view(VkImage image, VkFormat format,
										VkImageAspectFlags aspect,
										uint32_t mipLevels)
{
	VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
	ci.image = image;
	ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
	ci.format = format;
	ci.subresourceRange.aspectMask = aspect;
	ci.subresourceRange.baseMipLevel = 0;
	ci.subresourceRange.levelCount = mipLevels;
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
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
			VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
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

// =============================================================================
// Forward+ : Depth-only render pass
// =============================================================================

void Renderer::create_depth_only_render_pass()
{
	VkAttachmentDescription depthAtt{};
	depthAtt.format = find_depth_format();
	depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthRef{
		0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 0;
	subpass.pDepthStencilAttachment = &depthRef;

	VkSubpassDependency dep{};
	dep.srcSubpass = VK_SUBPASS_EXTERNAL;
	dep.dstSubpass = 0;
	dep.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
					   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dep.srcAccessMask = 0;
	dep.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
					   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dep.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
	ci.attachmentCount = 1;
	ci.pAttachments = &depthAtt;
	ci.subpassCount = 1;
	ci.pSubpasses = &subpass;
	ci.dependencyCount = 1;
	ci.pDependencies = &dep;

	VK_CHECK(vkCreateRenderPass(device_, &ci, nullptr, &depthOnlyRenderPass_));
}

void Renderer::create_depth_only_framebuffer()
{
	VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
	ci.renderPass = depthOnlyRenderPass_;
	ci.attachmentCount = 1;
	ci.pAttachments = &depthView_;
	ci.width = swapchainExtent_.width;
	ci.height = swapchainExtent_.height;
	ci.layers = 1;
	VK_CHECK(
		vkCreateFramebuffer(device_, &ci, nullptr, &depthOnlyFramebuffer_));
}

void Renderer::create_depth_prepass_pipeline()
{
	auto vertCode = packFile_->read("shaders/pbr.vert.spv");
	VkShaderModule vertMod = create_shader_module(vertCode);

	VkPipelineShaderStageCreateInfo stage{
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
	stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
	stage.module = vertMod;
	stage.pName = "main";

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

	VkPipelineColorBlendStateCreateInfo blend{
		VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
	blend.attachmentCount = 0;	// no color attachments

	VkDynamicState dynStates[] = {
		VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
		VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE};
	VkPipelineDynamicStateCreateInfo dyn{
		VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
	dyn.dynamicStateCount = 4;
	dyn.pDynamicStates = dynStates;

	// Layout: set 0 = frame UBO, push constant = model matrix
	VkPushConstantRange pushRange{};
	pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pushRange.offset = 0;
	pushRange.size = sizeof(glm::mat4);

	VkPipelineLayoutCreateInfo layoutCI{
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	layoutCI.setLayoutCount = 1;
	layoutCI.pSetLayouts = &frameSetLayout_;
	layoutCI.pushConstantRangeCount = 1;
	layoutCI.pPushConstantRanges = &pushRange;
	VK_CHECK(vkCreatePipelineLayout(device_, &layoutCI, nullptr,
									&depthPrepassPipelineLayout_));

	VkGraphicsPipelineCreateInfo ci{
		VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
	ci.stageCount = 1;	// vertex only
	ci.pStages = &stage;
	ci.pVertexInputState = &vertInput;
	ci.pInputAssemblyState = &inputAsm;
	ci.pViewportState = &vpState;
	ci.pRasterizationState = &raster;
	ci.pMultisampleState = &ms;
	ci.pDepthStencilState = &ds;
	ci.pColorBlendState = &blend;
	ci.pDynamicState = &dyn;
	ci.layout = depthPrepassPipelineLayout_;
	ci.renderPass = depthOnlyRenderPass_;
	ci.subpass = 0;

	VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr,
									   &depthPrepassPipeline_));

	vkDestroyShaderModule(device_, vertMod, nullptr);
}

void Renderer::draw_depth_prepass(VkCommandBuffer cmd)
{
	VkClearValue clear{};
	clear.depthStencil = {1.0f, 0};

	VkRenderPassBeginInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
	rpInfo.renderPass = depthOnlyRenderPass_;
	rpInfo.framebuffer = depthOnlyFramebuffer_;
	rpInfo.renderArea = {{0, 0}, swapchainExtent_};
	rpInfo.clearValueCount = 1;
	rpInfo.pClearValues = &clear;

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

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
					  depthPrepassPipeline_);

	// Dynamic rasterizer state (debug toggles)
	vkCmdSetCullMode(
		cmd, debugDisableCulling_ ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
	vkCmdSetFrontFace(cmd, debugFrontFace_ == 1
							   ? VK_FRONT_FACE_CLOCKWISE
							   : VK_FRONT_FACE_COUNTER_CLOCKWISE);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
							depthPrepassPipelineLayout_, 0, 1,
							&frameDescriptorSets_[currentFrame_], 0, nullptr);

	for (const auto& mesh : meshes_)
	{
		vkCmdPushConstants(cmd, depthPrepassPipelineLayout_,
						   VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4),
						   &mesh.transform);

		VkBuffer vbufs[] = {mesh.vertexBuffer};
		VkDeviceSize offs[] = {0};
		vkCmdBindVertexBuffers(cmd, 0, 1, vbufs, offs);
		vkCmdBindIndexBuffer(cmd, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmd, static_cast<uint32_t>(mesh.indices.size()), 1, 0,
						 0, 0);
	}

	vkCmdEndRenderPass(cmd);
}

// =============================================================================
// Forward+ : Light data descriptor set layout
// =============================================================================

void Renderer::create_light_data_set_layout()
{
	std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
	// binding 0: GPULight[] SSBO
	bindings[0].binding = 0;
	bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[0].descriptorCount = 1;
	bindings[0].stageFlags =
		VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	// binding 1: tile light indices SSBO
	bindings[1].binding = 1;
	bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[1].descriptorCount = 1;
	bindings[1].stageFlags =
		VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	// binding 2: depth texture (for compute culling)
	bindings[2].binding = 2;
	bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[2].descriptorCount = 1;
	bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

	VkDescriptorSetLayoutCreateInfo ci{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
	ci.bindingCount = static_cast<uint32_t>(bindings.size());
	ci.pBindings = bindings.data();
	VK_CHECK(vkCreateDescriptorSetLayout(device_, &ci, nullptr,
										 &lightDataSetLayout_));
}

// =============================================================================
// Forward+ : Light and tile SSBOs
// =============================================================================

void Renderer::create_light_buffers()
{
	tileCountX_ = (swapchainExtent_.width + TILE_SIZE - 1) / TILE_SIZE;
	tileCountY_ = (swapchainExtent_.height + TILE_SIZE - 1) / TILE_SIZE;
	uint32_t numTiles = tileCountX_ * tileCountY_;

	VkDeviceSize lightBufSize =
		static_cast<VkDeviceSize>(MAX_LIGHTS) * sizeof(GPULight);
	VkDeviceSize tileBufSize = static_cast<VkDeviceSize>(numTiles) *
							   (1 + MAX_LIGHTS_PER_TILE) * sizeof(uint32_t);

	lightSSBOs_.resize(MAX_FRAMES_IN_FLIGHT);
	lightSSBOMemory_.resize(MAX_FRAMES_IN_FLIGHT);
	lightSSBOMapped_.resize(MAX_FRAMES_IN_FLIGHT);
	tileLightSSBOs_.resize(MAX_FRAMES_IN_FLIGHT);
	tileLightSSBOMemory_.resize(MAX_FRAMES_IN_FLIGHT);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		// Light SSBO: host-visible mapped for CPU write
		create_buffer(lightBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
					  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
						  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					  lightSSBOs_[i], lightSSBOMemory_[i]);
		vkMapMemory(device_, lightSSBOMemory_[i], 0, lightBufSize, 0,
					&lightSSBOMapped_[i]);

		// Tile light SSBO: device-local for compute write / fragment read
		create_buffer(tileBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
					  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, tileLightSSBOs_[i],
					  tileLightSSBOMemory_[i]);
	}
}

void Renderer::cleanup_light_buffers()
{
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		if (i < static_cast<int>(lightSSBOs_.size()))
		{
			if (lightSSBOMapped_[i])
				vkUnmapMemory(device_, lightSSBOMemory_[i]);
			vkDestroyBuffer(device_, lightSSBOs_[i], nullptr);
			vkFreeMemory(device_, lightSSBOMemory_[i], nullptr);
		}
		if (i < static_cast<int>(tileLightSSBOs_.size()))
		{
			vkDestroyBuffer(device_, tileLightSSBOs_[i], nullptr);
			vkFreeMemory(device_, tileLightSSBOMemory_[i], nullptr);
		}
	}
	lightSSBOs_.clear();
	lightSSBOMemory_.clear();
	lightSSBOMapped_.clear();
	tileLightSSBOs_.clear();
	tileLightSSBOMemory_.clear();

	if (lightDescriptorPool_)
	{
		vkDestroyDescriptorPool(device_, lightDescriptorPool_, nullptr);
		lightDescriptorPool_ = VK_NULL_HANDLE;
	}
	lightDescriptorSets_.clear();
}

// =============================================================================
// Forward+ : Light descriptor pool & sets
// =============================================================================

void Renderer::create_light_descriptor_pool()
{
	std::array<VkDescriptorPoolSize, 2> poolSizes{};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	poolSizes[0].descriptorCount =
		static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT) * 2;  // light + tile
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[1].descriptorCount =
		static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);  // depth

	VkDescriptorPoolCreateInfo ci{
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	ci.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	ci.pPoolSizes = poolSizes.data();
	ci.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	VK_CHECK(
		vkCreateDescriptorPool(device_, &ci, nullptr, &lightDescriptorPool_));
}

void Renderer::create_light_descriptor_sets()
{
	std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT,
											   lightDataSetLayout_);
	VkDescriptorSetAllocateInfo ai{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
	ai.descriptorPool = lightDescriptorPool_;
	ai.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	ai.pSetLayouts = layouts.data();
	lightDescriptorSets_.resize(MAX_FRAMES_IN_FLIGHT);
	VK_CHECK(
		vkAllocateDescriptorSets(device_, &ai, lightDescriptorSets_.data()));

	uint32_t numTiles = tileCountX_ * tileCountY_;
	VkDeviceSize lightBufSize =
		static_cast<VkDeviceSize>(MAX_LIGHTS) * sizeof(GPULight);
	VkDeviceSize tileBufSize = static_cast<VkDeviceSize>(numTiles) *
							   (1 + MAX_LIGHTS_PER_TILE) * sizeof(uint32_t);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		VkDescriptorBufferInfo lightBufInfo{lightSSBOs_[i], 0, lightBufSize};
		VkDescriptorBufferInfo tileBufInfo{tileLightSSBOs_[i], 0, tileBufSize};
		VkDescriptorImageInfo depthImgInfo{};
		depthImgInfo.sampler = depthSampler_;
		depthImgInfo.imageView = depthView_;
		depthImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		std::array<VkWriteDescriptorSet, 3> writes{};
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = lightDescriptorSets_[i];
		writes[0].dstBinding = 0;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[0].descriptorCount = 1;
		writes[0].pBufferInfo = &lightBufInfo;

		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = lightDescriptorSets_[i];
		writes[1].dstBinding = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[1].descriptorCount = 1;
		writes[1].pBufferInfo = &tileBufInfo;

		writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[2].dstSet = lightDescriptorSets_[i];
		writes[2].dstBinding = 2;
		writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[2].descriptorCount = 1;
		writes[2].pImageInfo = &depthImgInfo;

		vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()),
							   writes.data(), 0, nullptr);
	}
}

// =============================================================================
// Forward+ : Compute pipeline (light culling)
// =============================================================================

void Renderer::create_compute_pipeline()
{
	auto compCode = packFile_->read("shaders/light_cull.comp.spv");
	VkShaderModule compMod = create_shader_module(compCode);

	VkPipelineShaderStageCreateInfo stage{
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
	stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stage.module = compMod;
	stage.pName = "main";

	// Layout: set 0 = frame UBO, set 1 = light data
	VkDescriptorSetLayout setLayouts[] = {frameSetLayout_, lightDataSetLayout_};
	VkPipelineLayoutCreateInfo layoutCI{
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	layoutCI.setLayoutCount = 2;
	layoutCI.pSetLayouts = setLayouts;
	VK_CHECK(vkCreatePipelineLayout(device_, &layoutCI, nullptr,
									&computePipelineLayout_));

	VkComputePipelineCreateInfo ci{
		VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
	ci.stage = stage;
	ci.layout = computePipelineLayout_;
	VK_CHECK(vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr,
									  &lightCullPipeline_));

	vkDestroyShaderModule(device_, compMod, nullptr);
}

// =============================================================================
// Forward+ : Heatmap debug pipeline
// =============================================================================

void Renderer::create_heatmap_pipeline()
{
	auto vertCode = packFile_->read("shaders/debug_heatmap.vert.spv");
	auto fragCode = packFile_->read("shaders/debug_heatmap.frag.spv");
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

	VkPipelineVertexInputStateCreateInfo vertInput{
		VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

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
	raster.cullMode = VK_CULL_MODE_NONE;

	VkPipelineMultisampleStateCreateInfo ms{
		VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo ds{
		VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
	ds.depthTestEnable = VK_FALSE;
	ds.depthWriteEnable = VK_FALSE;

	VkPipelineColorBlendAttachmentState blendAtt{};
	blendAtt.blendEnable = VK_TRUE;
	blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendAtt.colorBlendOp = VK_BLEND_OP_ADD;
	blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	blendAtt.alphaBlendOp = VK_BLEND_OP_ADD;
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

	// Layout: set 0 = frame UBO, set 1 = light data
	VkDescriptorSetLayout heatLayouts[] = {frameSetLayout_,
										   lightDataSetLayout_};
	VkPipelineLayoutCreateInfo layoutCI{
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	layoutCI.setLayoutCount = 2;
	layoutCI.pSetLayouts = heatLayouts;
	VK_CHECK(vkCreatePipelineLayout(device_, &layoutCI, nullptr,
									&heatmapPipelineLayout_));

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
	ci.layout = heatmapPipelineLayout_;
	ci.renderPass = renderPass_;
	ci.subpass = 0;

	VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr,
									   &heatmapPipeline_));

	vkDestroyShaderModule(device_, fragMod, nullptr);
	vkDestroyShaderModule(device_, vertMod, nullptr);
}

// =============================================================================
// Debug line visualization
// =============================================================================

void Renderer::create_debug_line_pipeline()
{
	auto vertCode = packFile_->read("shaders/debug_lines.vert.spv");
	auto fragCode = packFile_->read("shaders/debug_lines.frag.spv");
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

	auto bindingDesc = LineVertex::binding_desc();
	auto attribDescs = LineVertex::attrib_descs();
	VkPipelineVertexInputStateCreateInfo vertInput{
		VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
	vertInput.vertexBindingDescriptionCount = 1;
	vertInput.pVertexBindingDescriptions = &bindingDesc;
	vertInput.vertexAttributeDescriptionCount =
		static_cast<uint32_t>(attribDescs.size());
	vertInput.pVertexAttributeDescriptions = attribDescs.data();

	VkPipelineInputAssemblyStateCreateInfo inputAsm{
		VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
	inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

	VkPipelineViewportStateCreateInfo vpState{
		VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
	vpState.viewportCount = 1;
	vpState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo raster{
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
	raster.polygonMode = VK_POLYGON_MODE_FILL;
	raster.lineWidth = 1.0f;
	raster.cullMode = VK_CULL_MODE_NONE;

	VkPipelineMultisampleStateCreateInfo ms{
		VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
	ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineDepthStencilStateCreateInfo ds{
		VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
	ds.depthTestEnable = VK_TRUE;
	ds.depthWriteEnable = VK_FALSE;
	ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

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

	// Layout: set 0 = frame UBO only
	VkPipelineLayoutCreateInfo layoutCI{
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	layoutCI.setLayoutCount = 1;
	layoutCI.pSetLayouts = &frameSetLayout_;
	VK_CHECK(vkCreatePipelineLayout(device_, &layoutCI, nullptr,
									&debugLinePipelineLayout_));

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
	ci.layout = debugLinePipelineLayout_;
	ci.renderPass = renderPass_;
	ci.subpass = 0;

	VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &ci, nullptr,
									   &debugLinePipeline_));

	vkDestroyShaderModule(device_, fragMod, nullptr);
	vkDestroyShaderModule(device_, vertMod, nullptr);
}

void Renderer::create_debug_line_buffers()
{
	// 64KB per frame — enough for ~1300 line segments (2 vertices each)
	constexpr VkDeviceSize bufSize = 64 * 1024;
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
	{
		create_buffer(bufSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
					  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
						  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
					  debugLineVertexBuffers_[i], debugLineVertexMemory_[i]);
		vkMapMemory(device_, debugLineVertexMemory_[i], 0, bufSize, 0,
					&debugLineVertexMapped_[i]);
	}
}

void Renderer::update_debug_lines(const LightEnvironment& lights)
{
	auto verts = generate_light_lines(lights);
	constexpr VkDeviceSize bufSize = 64 * 1024;
	uint32_t maxVerts = static_cast<uint32_t>(bufSize / sizeof(LineVertex));
	debugLineVertexCount_ = static_cast<uint32_t>(
		std::min(static_cast<uint32_t>(verts.size()), maxVerts));
	if (debugLineVertexCount_ > 0)
	{
		std::memcpy(debugLineVertexMapped_[currentFrame_], verts.data(),
					debugLineVertexCount_ * sizeof(LineVertex));
	}
}

// =============================================================================
// Cleanup helpers
// =============================================================================

void Renderer::destroy_texture(Texture& tex)
{
	if (tex.view) vkDestroyImageView(device_, tex.view, nullptr);
	if (tex.image) vkDestroyImage(device_, tex.image, nullptr);
	if (tex.memory) vkFreeMemory(device_, tex.memory, nullptr);
	tex.image = VK_NULL_HANDLE;
	tex.memory = VK_NULL_HANDLE;
	tex.view = VK_NULL_HANDLE;
}
