#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

#include "material.h"
#include "mesh.h"
#include "packfile.h"
#include "texture.h"

struct Camera;

// =============================================================================
// Renderer
// =============================================================================

struct Renderer
{
	static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

	// Lifecycle
	void init(GLFWwindow* window, const std::string& modelPath);
	void cleanup();

	// Per-frame rendering
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

	// Render pass
	VkRenderPass renderPass_ = VK_NULL_HANDLE;

	// PBR pipeline
	VkDescriptorSetLayout frameSetLayout_ = VK_NULL_HANDLE;
	VkDescriptorSetLayout materialSetLayout_ = VK_NULL_HANDLE;
	VkPipelineLayout pbrPipelineLayout_ = VK_NULL_HANDLE;
	VkPipeline pbrPipeline_ = VK_NULL_HANDLE;

	// Framebuffers
	std::vector<VkFramebuffer> framebuffers_;

	// Commands
	VkCommandPool commandPool_ = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> commandBuffers_;

	// PBR sampler
	VkSampler pbrSampler_ = VK_NULL_HANDLE;

	// Default textures
	Texture defaultWhite_;
	Texture defaultNormal_;

	// Scene data (unified CPU+GPU)
	std::vector<Mesh> meshes_;
	std::vector<Texture> textures_;
	std::vector<Material> materials_;

	// Frame UBO
	struct FrameUBO
	{
		alignas(16) glm::mat4 view;
		alignas(16) glm::mat4 proj;
		alignas(16) glm::vec3 cameraPos;
		alignas(16) glm::vec3 lightDir;
		alignas(16) glm::vec3 lightColor;
	};
	std::vector<VkBuffer> uniformBuffers_;
	std::vector<VkDeviceMemory> uniformBuffersMemory_;
	std::vector<void*> uniformBuffersMapped_;

	// Descriptors
	VkDescriptorPool frameDescriptorPool_ = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> frameDescriptorSets_;
	VkDescriptorPool materialDescriptorPool_ = VK_NULL_HANDLE;

	// Sync
	std::vector<VkSemaphore> imageAvailableSemaphores_;
	std::vector<VkSemaphore> renderFinishedSemaphores_;
	std::vector<VkFence> inFlightFences_;

	// State
	uint32_t currentFrame_ = 0;
	bool framebufferResized_ = false;
	char gpuName_[256] = {};

	// Spinning cube
	uint32_t cubeMeshIndex_ = 0;
	uint32_t cubeMaterialIndex_ = 0;

	// Vulkan setup
	void create_instance();
	void setup_debug_messenger();
	void create_surface();
	void pick_physical_device();
	void create_logical_device();
	void create_swapchain();
	void create_image_views();
	void create_render_pass();
	void create_depth_resources();
	void create_framebuffers();
	void create_command_pool();
	void create_command_buffers();
	void create_sync_objects();

	// PBR setup
	void create_pbr_descriptor_layouts();
	void create_pbr_pipeline();
	void create_pbr_sampler();
	void create_default_textures();
	void create_uniform_buffers();
	void create_frame_descriptor_pool();
	void create_frame_descriptor_sets();
	void load_scene(const std::string& modelPath);

	// Texture upload
	void upload_texture(Texture& tex);
	void generate_mipmaps(VkImage image, VkFormat format, uint32_t width,
						  uint32_t height, uint32_t mipLevels);

	// Mesh upload
	void upload_mesh(Mesh& mesh);

	// Material descriptors
	void create_material_descriptor_pool(uint32_t materialCount);
	void create_material_descriptor(Material& mat);

	// Swapchain management
	void recreate_swapchain();
	void cleanup_swapchain();

	// Helpers
	uint32_t find_memory_type(uint32_t filter, VkMemoryPropertyFlags props);
	void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
					   VkMemoryPropertyFlags props, VkBuffer& buffer,
					   VkDeviceMemory& memory);
	void copy_buffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
	VkCommandBuffer begin_single_time_commands();
	void end_single_time_commands(VkCommandBuffer cmd);
	VkImageView create_image_view(VkImage image, VkFormat format,
								  VkImageAspectFlags aspect,
								  uint32_t mipLevels = 1);
	VkFormat find_supported_format(const std::vector<VkFormat>& candidates,
								   VkImageTiling tiling,
								   VkFormatFeatureFlags features);
	VkFormat find_depth_format();
	VkShaderModule create_shader_module(const std::vector<char>& code);
	void destroy_texture(Texture& tex);
};
