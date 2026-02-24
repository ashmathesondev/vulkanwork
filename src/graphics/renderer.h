#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

#include "light.h"
#include "material.h"
#include "mesh.h"
#include "pak/packfile.h"
#include "scene.h"
#include "texture.h"

struct Camera;

// =============================================================================
// Forward+ rendering constants
// =============================================================================

static constexpr uint32_t TILE_SIZE = 16;
static constexpr uint32_t MAX_LIGHTS_PER_TILE = 256;
static constexpr uint32_t MAX_LIGHTS = 1024;

// =============================================================================
// Shadow mapping constants
// =============================================================================

static constexpr uint32_t SHADOW_DIR_SIZE = 2048;
static constexpr uint32_t SHADOW_SPOT_SIZE = 1024;
static constexpr uint32_t MAX_SPOT_SHADOWS = 4;

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
	std::optional<FrameContext> begin_frame(const LightEnvironment& lights);
	void update_uniforms(const Camera& camera, float time,
						 const LightEnvironment& lights);
	void update_debug_lines(const LightEnvironment& lights);
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

	// Heatmap toggle (controlled from ImGui)
	bool showHeatmap_ = false;

	// Debug line visualization toggle (controlled from ImGui)
	bool showDebugLines_ = true;

	// Debug toggles (controlled from ImGui)
	bool debugSkipDepthPrepass_ = false;
	bool debugDisableCulling_ = false;
	int debugFrontFace_ = 0;  // 0=CCW, 1=CW

	// Shadow toggles (controlled from ImGui)
	bool shadowsEnabled_ = true;
	float shadowBias_ = 0.005f;

	// Scene accessors (for selection / gizmo)
	const std::vector<Mesh>& meshes() const { return meshes_; }
	std::vector<Mesh>& meshes() { return meshes_; }
	const glm::mat4& last_view() const { return lastView_; }
	const glm::mat4& last_proj() const { return lastProj_; }

	// Scene management
	void load_scene(const std::string& modelPath);
	void unload_scene();
	void load_scene_empty();
	void import_gltf(const std::string& path);
	void delete_mesh(uint32_t meshIdx);

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
	VkSampler depthSampler_ = VK_NULL_HANDLE;

	// Render passes
	VkRenderPass renderPass_ = VK_NULL_HANDLE;
	VkRenderPass depthOnlyRenderPass_ = VK_NULL_HANDLE;

	// Depth pre-pass
	VkFramebuffer depthOnlyFramebuffer_ = VK_NULL_HANDLE;
	VkPipelineLayout depthPrepassPipelineLayout_ = VK_NULL_HANDLE;
	VkPipeline depthPrepassPipeline_ = VK_NULL_HANDLE;

	// Shadow mapping
	VkRenderPass shadowRenderPass_ = VK_NULL_HANDLE;
	VkPipelineLayout shadowPipelineLayout_ = VK_NULL_HANDLE;
	VkPipeline shadowPipeline_ = VK_NULL_HANDLE;
	VkSampler shadowSampler_ = VK_NULL_HANDLE;

	VkImage dirShadowImage_[MAX_FRAMES_IN_FLIGHT] = {};
	VkDeviceMemory dirShadowMemory_[MAX_FRAMES_IN_FLIGHT] = {};
	VkImageView dirShadowView_[MAX_FRAMES_IN_FLIGHT] = {};
	VkFramebuffer dirShadowFramebuffer_[MAX_FRAMES_IN_FLIGHT] = {};

	VkImage spotShadowImages_[MAX_FRAMES_IN_FLIGHT][MAX_SPOT_SHADOWS] = {};
	VkDeviceMemory spotShadowMemory_[MAX_FRAMES_IN_FLIGHT][MAX_SPOT_SHADOWS] =
		{};
	VkImageView spotShadowViews_[MAX_FRAMES_IN_FLIGHT][MAX_SPOT_SHADOWS] = {};
	VkFramebuffer spotShadowFramebuffers_[MAX_FRAMES_IN_FLIGHT]
										 [MAX_SPOT_SHADOWS] = {};

	VkDescriptorSetLayout shadowSetLayout_ = VK_NULL_HANDLE;
	VkDescriptorPool shadowDescriptorPool_ = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> shadowDescriptorSets_;

	struct ShadowUBO
	{
		alignas(16) glm::mat4 dirLightVP;
		alignas(16) glm::mat4 spotLightVP[MAX_SPOT_SHADOWS];
		alignas(4) int32_t dirShadowEnabled;
		alignas(4) int32_t spotShadowCount;
		alignas(4) float shadowBias;
		alignas(4) int32_t _pad;
		alignas(16) glm::ivec4 spotLightIdx;
	};
	std::vector<VkBuffer> shadowUBOBuffers_;
	std::vector<VkDeviceMemory> shadowUBOMemory_;
	std::vector<void*> shadowUBOMapped_;

	// PBR pipeline
	VkDescriptorSetLayout frameSetLayout_ = VK_NULL_HANDLE;
	VkDescriptorSetLayout materialSetLayout_ = VK_NULL_HANDLE;
	VkDescriptorSetLayout lightDataSetLayout_ = VK_NULL_HANDLE;
	VkPipelineLayout pbrPipelineLayout_ = VK_NULL_HANDLE;
	VkPipeline pbrPipeline_ = VK_NULL_HANDLE;

	// Light culling compute
	VkPipelineLayout computePipelineLayout_ = VK_NULL_HANDLE;
	VkPipeline lightCullPipeline_ = VK_NULL_HANDLE;

	// Heatmap debug overlay
	VkPipelineLayout heatmapPipelineLayout_ = VK_NULL_HANDLE;
	VkPipeline heatmapPipeline_ = VK_NULL_HANDLE;

	// Debug line visualization
	VkPipelineLayout debugLinePipelineLayout_ = VK_NULL_HANDLE;
	VkPipeline debugLinePipeline_ = VK_NULL_HANDLE;
	VkBuffer debugLineVertexBuffers_[MAX_FRAMES_IN_FLIGHT] = {};
	VkDeviceMemory debugLineVertexMemory_[MAX_FRAMES_IN_FLIGHT] = {};
	void* debugLineVertexMapped_[MAX_FRAMES_IN_FLIGHT] = {};
	uint32_t debugLineVertexCount_ = 0;

	// Light / tile SSBOs (per frame-in-flight)
	std::vector<VkBuffer> lightSSBOs_;
	std::vector<VkDeviceMemory> lightSSBOMemory_;
	std::vector<void*> lightSSBOMapped_;
	std::vector<VkBuffer> tileLightSSBOs_;
	std::vector<VkDeviceMemory> tileLightSSBOMemory_;
	uint32_t tileCountX_ = 0;
	uint32_t tileCountY_ = 0;

	// Light data descriptors (per frame-in-flight)
	VkDescriptorPool lightDescriptorPool_ = VK_NULL_HANDLE;
	std::vector<VkDescriptorSet> lightDescriptorSets_;

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

	// Frame UBO (Forward+)
	struct FrameUBO
	{
		alignas(16) glm::mat4 view;
		alignas(16) glm::mat4 proj;
		alignas(16) glm::mat4 invProj;
		alignas(16) glm::vec3 cameraPos;
		alignas(4) uint32_t lightCount;
		alignas(16) glm::vec3 ambientColor;
		alignas(4) uint32_t tileCountX;
		alignas(4) uint32_t tileCountY;
		alignas(4) uint32_t screenWidth;
		alignas(4) uint32_t screenHeight;
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

	// Cached view/proj for picking/gizmo
	glm::mat4 lastView_{1.0f};
	glm::mat4 lastProj_{1.0f};

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

	// Forward+ setup
	void create_depth_only_render_pass();
	void create_depth_only_framebuffer();
	void create_depth_prepass_pipeline();
	void create_light_data_set_layout();
	void create_light_buffers();
	void create_light_descriptor_pool();
	void create_light_descriptor_sets();
	void create_compute_pipeline();
	void create_heatmap_pipeline();
	void create_debug_line_pipeline();
	void create_debug_line_buffers();

	// Forward+ per-frame
	void draw_depth_prepass(VkCommandBuffer cmd);

	// Shadow mapping setup
	void create_shadow_render_pass();
	void create_shadow_resources();
	void create_shadow_sampler();
	void create_shadow_pipeline();
	void create_shadow_descriptor_layout();
	void create_shadow_descriptor_pool();
	void create_shadow_descriptor_sets();
	void create_shadow_ubo_buffers();

	// Shadow mapping per-frame
	void draw_shadow_pass(VkCommandBuffer cmd, const LightEnvironment& lights);
	glm::mat4 compute_dir_light_vp(const DirectionalLight& l) const;
	glm::mat4 compute_spot_light_vp(const SpotLight& l) const;

	// Scene helpers
	void add_cube_to_scene(Scene& scene);

	// Texture upload
	void upload_texture(Texture& tex);
	void generate_mipmaps(VkImage image, VkFormat format, uint32_t width,
						  uint32_t height, uint32_t mipLevels);

	// Mesh upload
	void upload_mesh(Mesh& mesh);

	// Material descriptors
	void create_material_descriptor_pool(uint32_t materialCount);
	void create_material_descriptor(Material& mat);
	void rebuild_material_descriptors();

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
	void cleanup_light_buffers();
};
