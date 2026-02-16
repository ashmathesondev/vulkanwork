#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "packfile.h"

struct Camera;

struct Renderer
{
	static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

	// Lifecycle
	void init(GLFWwindow* window);
	void cleanup();

	// Per-frame rendering (granular)
	struct FrameContext
	{
		VkCommandBuffer cmd;
		uint32_t imageIndex;
	};
	std::optional<FrameContext> begin_frame();
	void update_uniforms(const Camera& camera, float time);
	void draw_scene(VkCommandBuffer cmd);
	void end_frame(const FrameContext& ctx);

	// Swapchain
	void notify_resize();

	// Accessors for App/ImGui
	const char* gpu_name() const;
	VkExtent2D swapchain_extent() const;

	// ImGui needs these for init
	VkInstance vk_instance() const;
	VkPhysicalDevice vk_physical_device() const;
	VkDevice vk_device() const;
	uint32_t vk_graphics_family() const;
	VkQueue vk_graphics_queue() const;
	uint32_t swapchain_image_count() const;
	VkRenderPass vk_render_pass() const;

   private:
	// Asset pack
	std::optional<pak::PackFile> packFile_;

	// Window (non-owning)
	GLFWwindow* window_ = nullptr;

	// Core Vulkan
	VkInstance instance_ = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
	VkSurfaceKHR surface_ = VK_NULL_HANDLE;
	VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
	VkDevice device_ = VK_NULL_HANDLE;
	VkQueue graphicsQueue_ = VK_NULL_HANDLE;
	VkQueue presentQueue_ = VK_NULL_HANDLE;
	uint32_t graphicsFamily_ = 0;
	uint32_t presentFamily_ = 0;

	// Swapchain
	VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
	VkFormat swapchainFormat_{};
	VkExtent2D swapchainExtent_{};
	std::vector<VkImage> swapchainImages_;
	std::vector<VkImageView> swapchainImageViews_;

	// Depth
	VkImage depthImage_ = VK_NULL_HANDLE;
	VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
	VkImageView depthView_ = VK_NULL_HANDLE;

	// Pipeline
	VkRenderPass renderPass_ = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
	VkPipeline graphicsPipeline_ = VK_NULL_HANDLE;

	// Framebuffers
	std::vector<VkFramebuffer> framebuffers_;

	// Commands
	VkCommandPool commandPool_ = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> commandBuffers_;

	// Texture
	VkImage textureImage_ = VK_NULL_HANDLE;
	VkDeviceMemory textureImageMemory_ = VK_NULL_HANDLE;
	VkImageView textureImageView_ = VK_NULL_HANDLE;
	VkSampler textureSampler_ = VK_NULL_HANDLE;

	// Geometry buffers
	VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
	VkDeviceMemory vertexBufferMemory_ = VK_NULL_HANDLE;
	VkBuffer indexBuffer_ = VK_NULL_HANDLE;
	VkDeviceMemory indexBufferMemory_ = VK_NULL_HANDLE;
	uint32_t indexCount_ = 0;

	// Uniform buffers (per frame-in-flight)
	std::vector<VkBuffer> uniformBuffers_;
	std::vector<VkDeviceMemory> uniformBuffersMemory_;
	std::vector<void*> uniformBuffersMapped_;

	// Descriptors
	VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> descriptorSets_;

	// Sync
	std::vector<VkSemaphore> imageAvailableSemaphores_;
	std::vector<VkSemaphore> renderFinishedSemaphores_;
	std::vector<VkFence> inFlightFences_;

	// State
	uint32_t currentFrame_ = 0;
	bool framebufferResized_ = false;
	char gpuName_[256] = {};

	// Vulkan setup
	void create_instance();
	void setup_debug_messenger();
	void create_surface();
	void pick_physical_device();
	void create_logical_device();
	void create_swapchain();
	void create_image_views();
	void create_render_pass();
	void create_descriptor_set_layout();
	void create_graphics_pipeline();
	void create_depth_resources();
	void create_framebuffers();
	void create_command_pool();
	void create_texture_image();
	void create_texture_image_view();
	void create_texture_sampler();
	void create_vertex_buffer();
	void create_index_buffer();
	void create_uniform_buffers();
	void create_descriptor_pool();
	void create_descriptor_sets();
	void create_command_buffers();
	void create_sync_objects();

	// Swapchain management
	void recreate_swapchain();
	void cleanup_swapchain();

	// Helpers
	uint32_t find_memory_type(uint32_t filter, VkMemoryPropertyFlags props);
	void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
					   VkMemoryPropertyFlags props, VkBuffer& buffer,
					   VkDeviceMemory& memory);
	void copy_buffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
	void transition_image_layout(VkImage image, VkImageLayout oldLayout,
								 VkImageLayout newLayout);
	void copy_buffer_to_image(VkBuffer buffer, VkImage image, uint32_t width,
							  uint32_t height);
	VkCommandBuffer begin_single_time_commands();
	void end_single_time_commands(VkCommandBuffer cmd);
	VkImageView create_image_view(VkImage image, VkFormat format,
								  VkImageAspectFlags aspect);
	VkFormat find_supported_format(const std::vector<VkFormat>& candidates,
								   VkImageTiling tiling,
								   VkFormatFeatureFlags features);
	VkFormat find_depth_format();
	VkShaderModule create_shader_module(const std::vector<char>& code);
};
