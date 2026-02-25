#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstdint>
#include <vector>

#include "loaders/gaussianSplatLoader.h"
#include "pak/packfile.h"

// =============================================================================
// GaussianSplatSystem
// =============================================================================
// Manages all GPU resources for Gaussian Splat rendering:
//   - Uploads raw splat data to GPU (splatBuffer_)
//   - Preprocess compute: projects 3D Gaussians -> 2D, evaluates SH, builds
//   sort keys
//   - GPU radix sort (4-pass 8-bit, ping-pong between two key+value buffers)
//   - Render: draws alpha-blended 2D Gaussian quads using sorted order
// =============================================================================

class GaussianSplatSystem
{
   public:
	// Call once, before load().  Passes in Vulkan handles and the asset pack so
	// the system can create pipelines by reading compiled SPIR-V shaders.
	void init(VkDevice device, VkPhysicalDevice physDevice,
			  VkCommandPool cmdPool, VkQueue queue, VkRenderPass mainRenderPass,
			  VkDescriptorSetLayout frameSetLayout, uint32_t framesInFlight,
			  pak::PackFile& pack);

	// Upload splat data and (re-)create GPU buffers / descriptors.
	// May be called multiple times to load a different scene.
	void load(const GaussianSplatCloud& cloud);

	bool has_splats() const { return splatCount_ > 0; }

	// Called from Renderer::begin_frame() BEFORE vkCmdBeginRenderPass.
	// Dispatches preprocess + radix-sort compute.
	// frameDS must be the renderer's per-frame descriptor set (set 0 = Frame
	// UBO).
	void dispatch_compute(VkCommandBuffer cmd, uint32_t frameIdx,
						  VkExtent2D extent, VkDescriptorSet frameDS);

	// Called from Renderer::draw_scene() INSIDE the main render pass,
	// after PBR draws.
	// frameDS: same frame descriptor set (not strictly needed here but kept
	//          for symmetry in case we add view-dependent effects later).
	void record_draw(VkCommandBuffer cmd, uint32_t frameIdx);

	void cleanup();

   private:
	// ---- Device handles (non-owning) ----------------------------------------
	VkDevice device_ = VK_NULL_HANDLE;
	VkPhysicalDevice physDevice_ = VK_NULL_HANDLE;
	VkCommandPool cmdPool_ = VK_NULL_HANDLE;
	VkQueue queue_ = VK_NULL_HANDLE;
	VkRenderPass renderPass_ = VK_NULL_HANDLE;
	VkDescriptorSetLayout frameSetLayout_ = VK_NULL_HANDLE;
	uint32_t framesInFlight_ = 0;

	// ---- Splat counts -------------------------------------------------------
	uint32_t splatCount_ = 0;
	uint32_t numSortWG_ = 0;  // ceil(N/256) workgroups for hist/scatter

	// ---- GPU Buffers --------------------------------------------------------
	VkBuffer splatBuffer_ = VK_NULL_HANDLE;
	VkDeviceMemory splatMem_ = VK_NULL_HANDLE;

	VkBuffer preprocessBuf_ = VK_NULL_HANDLE;
	VkDeviceMemory preprocessMem_ = VK_NULL_HANDLE;

	VkBuffer sortKeysA_ = VK_NULL_HANDLE;
	VkDeviceMemory sortKeysAMem_ = VK_NULL_HANDLE;
	VkBuffer sortKeysB_ = VK_NULL_HANDLE;
	VkDeviceMemory sortKeysBMem_ = VK_NULL_HANDLE;

	VkBuffer sortValsA_ = VK_NULL_HANDLE;
	VkDeviceMemory sortValsAMem_ = VK_NULL_HANDLE;
	VkBuffer sortValsB_ = VK_NULL_HANDLE;
	VkDeviceMemory sortValsBMem_ = VK_NULL_HANDLE;

	VkBuffer histogramBuf_ = VK_NULL_HANDLE;
	VkDeviceMemory histogramMem_ = VK_NULL_HANDLE;
	VkBuffer prefixBuf_ = VK_NULL_HANDLE;
	VkDeviceMemory prefixMem_ = VK_NULL_HANDLE;

	// ---- Compute pipeline ---------------------------------------------------
	VkDescriptorSetLayout computeSetLayout_ = VK_NULL_HANDLE;
	VkPipelineLayout computePipeLayout_ = VK_NULL_HANDLE;
	VkPipeline preprocessPipeline_ = VK_NULL_HANDLE;
	VkPipeline sortHistPipeline_ = VK_NULL_HANDLE;
	VkPipeline sortPrefixPipeline_ = VK_NULL_HANDLE;
	VkPipeline sortScatterPipeline_ = VK_NULL_HANDLE;

	// ---- Render pipeline ----------------------------------------------------
	VkDescriptorSetLayout renderSetLayout_ = VK_NULL_HANDLE;
	VkPipelineLayout renderPipeLayout_ = VK_NULL_HANDLE;
	VkPipeline splatPipeline_ = VK_NULL_HANDLE;

	// ---- Descriptor pool & sets ---------------------------------------------
	VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
	VkDescriptorSet computeDS_ = VK_NULL_HANDLE;
	VkDescriptorSet renderDS_ = VK_NULL_HANDLE;

	// Cached screen size (set in dispatch_compute, used in record_draw)
	float lastExtentW_ = 1.f;
	float lastExtentH_ = 1.f;

	// ---- Internal helpers ---------------------------------------------------
	uint32_t find_memory_type(uint32_t filter,
							  VkMemoryPropertyFlags props) const;
	void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
					   VkMemoryPropertyFlags props, VkBuffer& buf,
					   VkDeviceMemory& mem);
	void upload_buffer(const void* src, VkDeviceSize size, VkBuffer dst);
	VkShaderModule create_shader(const std::vector<char>& code);

	void create_compute_descriptor_layout();
	void create_render_descriptor_layout();
	void create_compute_pipelines(pak::PackFile& pack);
	void create_render_pipeline(pak::PackFile& pack);
	void create_descriptor_pool();
	void create_descriptor_sets();
	void update_descriptor_sets();

	void destroy_scene_buffers();
};
