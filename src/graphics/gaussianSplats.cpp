#include "gaussianSplats.h"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

// =============================================================================
// Macros
// =============================================================================

#define VK_CHECK(x)                                                            \
	do                                                                         \
	{                                                                          \
		VkResult _r = (x);                                                     \
		if (_r != VK_SUCCESS)                                                  \
		{                                                                      \
			std::fprintf(stderr, "[GaussianSplat] Vulkan error %d at %s:%d\n", \
						 _r, __FILE__, __LINE__);                              \
			std::abort();                                                      \
		}                                                                      \
	} while (0)

// Push-constant layouts (must match shaders)
struct PreprocessPC
{
	uint32_t N;
};
struct SortPC
{
	uint32_t N;
	uint32_t shift;
	uint32_t ping;
	uint32_t numWG;
};
struct RenderPC
{
	uint32_t N;
	float screenW;
	float screenH;
};

static constexpr uint32_t SORT_WG_SIZE = 256;  // threads per workgroup
static constexpr uint32_t RADIX_BITS = 8;
static constexpr uint32_t RADIX_BUCKETS = 256;	// 2^8
static constexpr uint32_t NUM_PASSES = 4;		// 32-bit key / 8 bits

// =============================================================================
// Memory helpers
// =============================================================================

uint32_t GaussianSplatSystem::find_memory_type(
	uint32_t filter, VkMemoryPropertyFlags props) const
{
	VkPhysicalDeviceMemoryProperties mp;
	vkGetPhysicalDeviceMemoryProperties(physDevice_, &mp);
	for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
		if ((filter & (1u << i)) &&
			(mp.memoryTypes[i].propertyFlags & props) == props)
			return i;
	throw std::runtime_error("[GaussianSplat] No suitable memory type");
}

void GaussianSplatSystem::create_buffer(VkDeviceSize size,
										VkBufferUsageFlags usage,
										VkMemoryPropertyFlags props,
										VkBuffer& buf, VkDeviceMemory& mem)
{
	VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
	bi.size = size;
	bi.usage = usage;
	bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VK_CHECK(vkCreateBuffer(device_, &bi, nullptr, &buf));

	VkMemoryRequirements mr;
	vkGetBufferMemoryRequirements(device_, buf, &mr);

	VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
	ai.allocationSize = mr.size;
	ai.memoryTypeIndex = find_memory_type(mr.memoryTypeBits, props);
	VK_CHECK(vkAllocateMemory(device_, &ai, nullptr, &mem));
	VK_CHECK(vkBindBufferMemory(device_, buf, mem, 0));
}

void GaussianSplatSystem::upload_buffer(const void* src, VkDeviceSize size,
										VkBuffer dst)
{
	// Staging buffer
	VkBuffer staging;
	VkDeviceMemory stagingMem;
	create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
					  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
				  staging, stagingMem);

	void* mapped;
	VK_CHECK(vkMapMemory(device_, stagingMem, 0, size, 0, &mapped));
	std::memcpy(mapped, src, static_cast<size_t>(size));
	vkUnmapMemory(device_, stagingMem);

	// One-time command buffer
	VkCommandBufferAllocateInfo ai{
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
	ai.commandPool = cmdPool_;
	ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	ai.commandBufferCount = 1;
	VkCommandBuffer cmd;
	VK_CHECK(vkAllocateCommandBuffers(device_, &ai, &cmd));

	VkCommandBufferBeginInfo bi2{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
	bi2.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(vkBeginCommandBuffer(cmd, &bi2));

	VkBufferCopy region{0, 0, size};
	vkCmdCopyBuffer(cmd, staging, dst, 1, &region);

	VK_CHECK(vkEndCommandBuffer(cmd));

	VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
	si.commandBufferCount = 1;
	si.pCommandBuffers = &cmd;
	VK_CHECK(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE));
	VK_CHECK(vkQueueWaitIdle(queue_));

	vkFreeCommandBuffers(device_, cmdPool_, 1, &cmd);
	vkDestroyBuffer(device_, staging, nullptr);
	vkFreeMemory(device_, stagingMem, nullptr);
}

VkShaderModule GaussianSplatSystem::create_shader(const std::vector<char>& code)
{
	VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
	ci.codeSize = code.size();
	ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
	VkShaderModule mod;
	VK_CHECK(vkCreateShaderModule(device_, &ci, nullptr, &mod));
	return mod;
}

// =============================================================================
// Descriptor layouts
// =============================================================================

void GaussianSplatSystem::create_compute_descriptor_layout()
{
	// 8 SSBOs: splatBuf, preprocessBuf, keysA, keysB, valsA, valsB, hist,
	// prefix
	std::array<VkDescriptorSetLayoutBinding, 8> binds{};
	for (uint32_t i = 0; i < 8; ++i)
	{
		binds[i].binding = i;
		binds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		binds[i].descriptorCount = 1;
		binds[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}

	VkDescriptorSetLayoutCreateInfo ci{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
	ci.bindingCount = static_cast<uint32_t>(binds.size());
	ci.pBindings = binds.data();
	VK_CHECK(
		vkCreateDescriptorSetLayout(device_, &ci, nullptr, &computeSetLayout_));
}

void GaussianSplatSystem::create_render_descriptor_layout()
{
	// 2 SSBOs: preprocessBuf (binding 0), sortValsA (binding 1)
	std::array<VkDescriptorSetLayoutBinding, 2> binds{};
	for (uint32_t i = 0; i < 2; ++i)
	{
		binds[i].binding = i;
		binds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		binds[i].descriptorCount = 1;
		binds[i].stageFlags =
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	}

	VkDescriptorSetLayoutCreateInfo ci{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
	ci.bindingCount = static_cast<uint32_t>(binds.size());
	ci.pBindings = binds.data();
	VK_CHECK(
		vkCreateDescriptorSetLayout(device_, &ci, nullptr, &renderSetLayout_));
}

// =============================================================================
// Pipeline creation
// =============================================================================

void GaussianSplatSystem::create_compute_pipelines(pak::PackFile& pack)
{
	// Pipeline layout: set 0 = frameSetLayout_, set 1 = computeSetLayout_
	// Push constants: 16 bytes (SortPC is the largest)
	VkDescriptorSetLayout setLayouts[] = {frameSetLayout_, computeSetLayout_};

	VkPushConstantRange pcRange{};
	pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	pcRange.offset = 0;
	pcRange.size = sizeof(SortPC);	// 16 bytes, largest

	VkPipelineLayoutCreateInfo li{
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	li.setLayoutCount = 2;
	li.pSetLayouts = setLayouts;
	li.pushConstantRangeCount = 1;
	li.pPushConstantRanges = &pcRange;
	VK_CHECK(
		vkCreatePipelineLayout(device_, &li, nullptr, &computePipeLayout_));

	auto make_compute = [&](const char* spvKey) -> VkPipeline
	{
		auto code = pack.read(spvKey);
		VkShaderModule mod = create_shader(code);

		VkComputePipelineCreateInfo ci{
			VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
		ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		ci.stage.module = mod;
		ci.stage.pName = "main";
		ci.layout = computePipeLayout_;

		VkPipeline pipe;
		VK_CHECK(vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &ci,
										  nullptr, &pipe));
		vkDestroyShaderModule(device_, mod, nullptr);
		return pipe;
	};

	preprocessPipeline_ = make_compute("shaders/gsplat_preprocess.comp.spv");
	sortHistPipeline_ = make_compute("shaders/gsplat_sort_hist.comp.spv");
	sortPrefixPipeline_ = make_compute("shaders/gsplat_sort_prefix.comp.spv");
	sortScatterPipeline_ = make_compute("shaders/gsplat_sort_scatter.comp.spv");
}

void GaussianSplatSystem::create_render_pipeline(pak::PackFile& pack)
{
	// Layout: set 0 = renderSetLayout_
	VkPushConstantRange pcRange{};
	pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pcRange.offset = 0;
	pcRange.size = sizeof(RenderPC);

	VkPipelineLayoutCreateInfo li{
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
	li.setLayoutCount = 1;
	li.pSetLayouts = &renderSetLayout_;
	li.pushConstantRangeCount = 1;
	li.pPushConstantRanges = &pcRange;
	VK_CHECK(vkCreatePipelineLayout(device_, &li, nullptr, &renderPipeLayout_));

	auto vertCode = pack.read("shaders/gsplat.vert.spv");
	auto fragCode = pack.read("shaders/gsplat.frag.spv");
	VkShaderModule vertMod = create_shader(vertCode);
	VkShaderModule fragMod = create_shader(fragCode);

	VkPipelineShaderStageCreateInfo stages[2]{};
	stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	stages[0].module = vertMod;
	stages[0].pName = "main";
	stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	stages[1].module = fragMod;
	stages[1].pName = "main";

	// No vertex input — positions generated in shader
	VkPipelineVertexInputStateCreateInfo viState{
		VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

	VkPipelineInputAssemblyStateCreateInfo iaState{
		VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
	iaState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo vpState{
		VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
	vpState.viewportCount = 1;
	vpState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rsState{
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
	rsState.polygonMode = VK_POLYGON_MODE_FILL;
	rsState.cullMode = VK_CULL_MODE_NONE;
	rsState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rsState.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo msState{
		VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
	msState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// Alpha blending: src * srcAlpha + dst * (1 - srcAlpha)
	VkPipelineColorBlendAttachmentState blendAttach{};
	blendAttach.blendEnable = VK_TRUE;
	blendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendAttach.colorBlendOp = VK_BLEND_OP_ADD;
	blendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	blendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendAttach.alphaBlendOp = VK_BLEND_OP_ADD;
	blendAttach.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	VkPipelineColorBlendStateCreateInfo cbState{
		VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
	cbState.attachmentCount = 1;
	cbState.pAttachments = &blendAttach;

	// Depth test enabled (occluded by opaque geometry) but no depth write
	VkPipelineDepthStencilStateCreateInfo dsState{
		VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
	dsState.depthTestEnable = VK_TRUE;
	dsState.depthWriteEnable = VK_FALSE;
	dsState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

	VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
								  VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynState{
		VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
	dynState.dynamicStateCount = 2;
	dynState.pDynamicStates = dynStates;

	VkGraphicsPipelineCreateInfo pci{
		VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
	pci.stageCount = 2;
	pci.pStages = stages;
	pci.pVertexInputState = &viState;
	pci.pInputAssemblyState = &iaState;
	pci.pViewportState = &vpState;
	pci.pRasterizationState = &rsState;
	pci.pMultisampleState = &msState;
	pci.pDepthStencilState = &dsState;
	pci.pColorBlendState = &cbState;
	pci.pDynamicState = &dynState;
	pci.layout = renderPipeLayout_;
	pci.renderPass = renderPass_;
	pci.subpass = 0;

	VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pci,
									   nullptr, &splatPipeline_));

	vkDestroyShaderModule(device_, vertMod, nullptr);
	vkDestroyShaderModule(device_, fragMod, nullptr);
}

// =============================================================================
// Descriptor pool & sets
// =============================================================================

void GaussianSplatSystem::create_descriptor_pool()
{
	// Compute: 8 SSBOs; Render: 2 SSBOs
	VkDescriptorPoolSize poolSize{};
	poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	poolSize.descriptorCount = 10;

	VkDescriptorPoolCreateInfo ci{
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
	ci.maxSets = 2;
	ci.poolSizeCount = 1;
	ci.pPoolSizes = &poolSize;
	VK_CHECK(vkCreateDescriptorPool(device_, &ci, nullptr, &descriptorPool_));
}

void GaussianSplatSystem::create_descriptor_sets()
{
	VkDescriptorSetLayout layouts[] = {computeSetLayout_, renderSetLayout_};
	VkDescriptorSetAllocateInfo ai{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
	ai.descriptorPool = descriptorPool_;
	ai.descriptorSetCount = 2;
	ai.pSetLayouts = layouts;

	VkDescriptorSet sets[2];
	VK_CHECK(vkAllocateDescriptorSets(device_, &ai, sets));
	computeDS_ = sets[0];
	renderDS_ = sets[1];
}

void GaussianSplatSystem::update_descriptor_sets()
{
	auto buf_info = [](VkBuffer b)
	{
		VkDescriptorBufferInfo i{};
		i.buffer = b;
		i.offset = 0;
		i.range = VK_WHOLE_SIZE;
		return i;
	};

	// Compute set (8 bindings)
	VkDescriptorBufferInfo compInfos[8] = {
		buf_info(splatBuffer_),	 buf_info(preprocessBuf_), buf_info(sortKeysA_),
		buf_info(sortKeysB_),	 buf_info(sortValsA_),	   buf_info(sortValsB_),
		buf_info(histogramBuf_), buf_info(prefixBuf_),
	};

	VkWriteDescriptorSet compWrites[8]{};
	for (uint32_t i = 0; i < 8; ++i)
	{
		compWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		compWrites[i].dstSet = computeDS_;
		compWrites[i].dstBinding = i;
		compWrites[i].descriptorCount = 1;
		compWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		compWrites[i].pBufferInfo = &compInfos[i];
	}
	vkUpdateDescriptorSets(device_, 8, compWrites, 0, nullptr);

	// Render set (2 bindings): preprocessBuf + sortValsA (final sorted output)
	VkDescriptorBufferInfo renderInfos[2] = {
		buf_info(preprocessBuf_),
		buf_info(sortValsA_),
	};
	VkWriteDescriptorSet renderWrites[2]{};
	for (uint32_t i = 0; i < 2; ++i)
	{
		renderWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		renderWrites[i].dstSet = renderDS_;
		renderWrites[i].dstBinding = i;
		renderWrites[i].descriptorCount = 1;
		renderWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		renderWrites[i].pBufferInfo = &renderInfos[i];
	}
	vkUpdateDescriptorSets(device_, 2, renderWrites, 0, nullptr);
}

// =============================================================================
// Lifecycle
// =============================================================================

void GaussianSplatSystem::init(VkDevice device, VkPhysicalDevice physDevice,
							   VkCommandPool cmdPool, VkQueue queue,
							   VkRenderPass mainRenderPass,
							   VkDescriptorSetLayout frameSetLayout,
							   uint32_t framesInFlight, pak::PackFile& pack)
{
	device_ = device;
	physDevice_ = physDevice;
	cmdPool_ = cmdPool;
	queue_ = queue;
	renderPass_ = mainRenderPass;
	frameSetLayout_ = frameSetLayout;
	framesInFlight_ = framesInFlight;

	create_compute_descriptor_layout();
	create_render_descriptor_layout();
	create_compute_pipelines(pack);
	create_render_pipeline(pack);
	create_descriptor_pool();
	create_descriptor_sets();
}

void GaussianSplatSystem::destroy_scene_buffers()
{
	auto destroy = [&](VkBuffer& b, VkDeviceMemory& m)
	{
		if (b)
		{
			vkDestroyBuffer(device_, b, nullptr);
			b = VK_NULL_HANDLE;
		}
		if (m)
		{
			vkFreeMemory(device_, m, nullptr);
			m = VK_NULL_HANDLE;
		}
	};
	destroy(splatBuffer_, splatMem_);
	destroy(preprocessBuf_, preprocessMem_);
	destroy(sortKeysA_, sortKeysAMem_);
	destroy(sortKeysB_, sortKeysBMem_);
	destroy(sortValsA_, sortValsAMem_);
	destroy(sortValsB_, sortValsBMem_);
	destroy(histogramBuf_, histogramMem_);
	destroy(prefixBuf_, prefixMem_);
	splatCount_ = 0;
	numSortWG_ = 0;
}

void GaussianSplatSystem::load(const GaussianSplatCloud& cloud)
{
	vkDeviceWaitIdle(device_);

	// Free old buffers if reloading
	destroy_scene_buffers();
	// Reset and re-allocate descriptor sets when buffers change
	if (descriptorPool_)
	{
		vkResetDescriptorPool(device_, descriptorPool_, 0);
		create_descriptor_sets();
	}

	if (cloud.splats.empty()) return;

	uint32_t N = static_cast<uint32_t>(cloud.splats.size());
	splatCount_ = N;
	numSortWG_ = (N + SORT_WG_SIZE - 1) / SORT_WG_SIZE;

	const VkBufferUsageFlags ssboFlags =
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	const VkMemoryPropertyFlags devLocal = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	// Splat buffer (240 bytes per splat)
	VkDeviceSize splatBytes = (VkDeviceSize)N * 240;
	create_buffer(splatBytes, ssboFlags, devLocal, splatBuffer_, splatMem_);
	upload_buffer(cloud.splats.data(), splatBytes, splatBuffer_);

	// Preprocess buffer (10 floats = 40 bytes per splat)
	create_buffer((VkDeviceSize)N * 40, ssboFlags, devLocal, preprocessBuf_,
				  preprocessMem_);

	// Sort key/value buffers (4 bytes each per splat, two ping-pong pairs)
	VkDeviceSize sortBytes = (VkDeviceSize)N * 4;
	create_buffer(sortBytes, ssboFlags, devLocal, sortKeysA_, sortKeysAMem_);
	create_buffer(sortBytes, ssboFlags, devLocal, sortKeysB_, sortKeysBMem_);
	create_buffer(sortBytes, ssboFlags, devLocal, sortValsA_, sortValsAMem_);
	create_buffer(sortBytes, ssboFlags, devLocal, sortValsB_, sortValsBMem_);

	// Histogram and prefix buffers (256 * numWG * 4 bytes each)
	VkDeviceSize histBytes = (VkDeviceSize)RADIX_BUCKETS * numSortWG_ * 4;
	create_buffer(histBytes, ssboFlags, devLocal, histogramBuf_, histogramMem_);
	create_buffer(histBytes, ssboFlags, devLocal, prefixBuf_, prefixMem_);

	update_descriptor_sets();
}

// =============================================================================
// Per-frame: compute dispatch
// =============================================================================

void GaussianSplatSystem::dispatch_compute(VkCommandBuffer cmd,
										   uint32_t /*frameIdx*/,
										   VkExtent2D extent,
										   VkDescriptorSet frameDS)
{
	if (!has_splats()) return;

	lastExtentW_ = static_cast<float>(extent.width);
	lastExtentH_ = static_cast<float>(extent.height);

	uint32_t N = splatCount_;
	uint32_t numWG = numSortWG_;

	VkDescriptorSet sets[] = {frameDS, computeDS_};

	auto bind_compute = [&](VkPipeline pipe)
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
								computePipeLayout_, 0, 2, sets, 0, nullptr);
	};

	auto ssbo_barrier = [&]()
	{
		VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
		mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
							 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0,
							 nullptr, 0, nullptr);
	};

	// ---- 1. Preprocess: project splats, eval SH, write depth sort keys ----
	{
		bind_compute(preprocessPipeline_);
		PreprocessPC pc{N};
		vkCmdPushConstants(cmd, computePipeLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
						   0, sizeof(pc), &pc);
		vkCmdDispatch(cmd, numWG, 1, 1);
	}
	ssbo_barrier();

	// ---- 2. Radix sort: 4 passes (ping-pong A<->B) ----------------------
	// After 4 passes starting ping=0 (read A, write B), the final
	// result ends in A (passes: A->B->A->B->A, 0->1->0->1->0).
	for (uint32_t pass = 0; pass < NUM_PASSES; ++pass)
	{
		uint32_t shift = pass * RADIX_BITS;
		uint32_t ping = pass & 1;  // 0=read A write B, 1=read B write A

		// 2a. Histogram
		{
			bind_compute(sortHistPipeline_);
			SortPC pc{N, shift, ping, numWG};
			vkCmdPushConstants(cmd, computePipeLayout_,
							   VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
			vkCmdDispatch(cmd, numWG, 1, 1);
		}
		ssbo_barrier();

		// 2b. Prefix sum (single workgroup over 256 digits)
		{
			bind_compute(sortPrefixPipeline_);
			SortPC pc{N, shift, ping, numWG};
			vkCmdPushConstants(cmd, computePipeLayout_,
							   VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
			vkCmdDispatch(cmd, 1, 1, 1);
		}
		ssbo_barrier();

		// 2c. Scatter
		{
			bind_compute(sortScatterPipeline_);
			SortPC pc{N, shift, ping, numWG};
			vkCmdPushConstants(cmd, computePipeLayout_,
							   VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
			vkCmdDispatch(cmd, numWG, 1, 1);
		}
		ssbo_barrier();
	}

	// ---- 3. Barrier: compute writes -> vertex reads ----------------------
	{
		VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
		mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
							 VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 1, &mb, 0,
							 nullptr, 0, nullptr);
	}
}

// =============================================================================
// Per-frame: render
// =============================================================================

void GaussianSplatSystem::record_draw(VkCommandBuffer cmd,
									  uint32_t /*frameIdx*/)
{
	if (!has_splats()) return;

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, splatPipeline_);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
							renderPipeLayout_, 0, 1, &renderDS_, 0, nullptr);

	// Push constants: N (vertex count = N * 6 triangles, N in PC)
	// Screen size is read from the viewport dynamic state via gl_FragCoord in
	// the fragment shader; the vertex shader uses push constants for
	// pixel->NDC. We embed screenW/H from the last-known extent in
	// dispatch_compute; for now pass through the render push constants.  The
	// vertex shader reads these. Note: we store the extent in dispatch_compute
	// but don't cache it here, so we use a simple approach: the vertex shader
	// re-derives pixel coords from NDC and reconstructs the required
	// transforms. Actual screen size is passed as push constant populated
	// below. (We forward the last extent stored in the system — see
	// dispatch_compute.)

	RenderPC pc{splatCount_, 0.f, 0.f};
	// screenW/H are 0 here; shaders use gl_FragCoord which is already in
	// pixels, and the vertex shader computes NDC from the stored pixel center +
	// viewport push constants. We cache them during dispatch_compute below. For
	// simplicity, the vertex shader gets screen size from the push constant.
	// Re-run dispatch_compute first sets them; they are cached in lastExtent_.
	pc.screenW = lastExtentW_;
	pc.screenH = lastExtentH_;

	vkCmdPushConstants(cmd, renderPipeLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0,
					   sizeof(pc), &pc);

	// 6 vertices per splat (triangle list, no index buffer)
	vkCmdDraw(cmd, splatCount_ * 6, 1, 0, 0);
}

// =============================================================================
// Cleanup
// =============================================================================

void GaussianSplatSystem::cleanup()
{
	vkDeviceWaitIdle(device_);

	destroy_scene_buffers();

	if (descriptorPool_)
	{
		vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
		descriptorPool_ = VK_NULL_HANDLE;
		computeDS_ = renderDS_ = VK_NULL_HANDLE;
	}

	auto destroyPipe = [&](VkPipeline& p)
	{
		if (p)
		{
			vkDestroyPipeline(device_, p, nullptr);
			p = VK_NULL_HANDLE;
		}
	};
	destroyPipe(preprocessPipeline_);
	destroyPipe(sortHistPipeline_);
	destroyPipe(sortPrefixPipeline_);
	destroyPipe(sortScatterPipeline_);
	destroyPipe(splatPipeline_);

	auto destroyLayout = [&](VkPipelineLayout& l)
	{
		if (l)
		{
			vkDestroyPipelineLayout(device_, l, nullptr);
			l = VK_NULL_HANDLE;
		}
	};
	auto destroyDSL = [&](VkDescriptorSetLayout& l)
	{
		if (l)
		{
			vkDestroyDescriptorSetLayout(device_, l, nullptr);
			l = VK_NULL_HANDLE;
		}
	};

	destroyLayout(computePipeLayout_);
	destroyLayout(renderPipeLayout_);
	destroyDSL(computeSetLayout_);
	destroyDSL(renderSetLayout_);
}
