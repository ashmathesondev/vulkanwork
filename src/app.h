#pragma once

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

#include <vector>
#include <string>
#include <array>

#include "camera.h"

struct App {
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    static constexpr int INITIAL_WIDTH  = 1280;
    static constexpr int INITIAL_HEIGHT = 720;

    // Window
    GLFWwindow* window = nullptr;

    // Core Vulkan
    VkInstance               instance       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR             surface        = VK_NULL_HANDLE;
    VkPhysicalDevice         physicalDevice = VK_NULL_HANDLE;
    VkDevice                 device         = VK_NULL_HANDLE;
    VkQueue                  graphicsQueue  = VK_NULL_HANDLE;
    VkQueue                  presentQueue   = VK_NULL_HANDLE;
    uint32_t                 graphicsFamily = 0;
    uint32_t                 presentFamily  = 0;

    // Swapchain
    VkSwapchainKHR             swapchain = VK_NULL_HANDLE;
    VkFormat                   swapchainFormat{};
    VkExtent2D                 swapchainExtent{};
    std::vector<VkImage>       swapchainImages;
    std::vector<VkImageView>   swapchainImageViews;

    // Depth
    VkImage        depthImage  = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView    depthView   = VK_NULL_HANDLE;

    // Pipeline
    VkRenderPass          renderPass          = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout      pipelineLayout      = VK_NULL_HANDLE;
    VkPipeline            graphicsPipeline    = VK_NULL_HANDLE;

    // Framebuffers
    std::vector<VkFramebuffer> framebuffers;

    // Commands
    VkCommandPool                commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;

    // Texture
    VkImage        textureImage       = VK_NULL_HANDLE;
    VkDeviceMemory textureImageMemory = VK_NULL_HANDLE;
    VkImageView    textureImageView   = VK_NULL_HANDLE;
    VkSampler      textureSampler     = VK_NULL_HANDLE;

    // Geometry buffers
    VkBuffer       vertexBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
    VkBuffer       indexBuffer        = VK_NULL_HANDLE;
    VkDeviceMemory indexBufferMemory  = VK_NULL_HANDLE;
    uint32_t       indexCount         = 0;

    // Uniform buffers (per frame-in-flight)
    std::vector<VkBuffer>       uniformBuffers;
    std::vector<VkDeviceMemory> uniformBuffersMemory;
    std::vector<void*>          uniformBuffersMapped;

    // Descriptors
    VkDescriptorPool             descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    // ImGui
    VkDescriptorPool imguiPool = VK_NULL_HANDLE;

    // Sync
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence>     inFlightFences;

    // State
    uint32_t currentFrame      = 0;
    bool     framebufferResized = false;
    bool     mouseCaptured     = false;
    Camera   camera;
    float    deltaTime     = 0.0f;
    float    lastFrameTime = 0.0f;
    char     gpuName[256]  = {};

    // --- Public API ----------------------------------------------------------
    void run();

private:
    void init_window();
    void init_vulkan();
    void main_loop();
    void cleanup();

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
    void init_imgui();

    // Rendering
    void draw_frame();
    void update_uniform_buffer(uint32_t frameIndex);
    void record_command_buffer(VkCommandBuffer cmd, uint32_t imageIndex);

    // Swapchain management
    void recreate_swapchain();
    void cleanup_swapchain();

    // Helpers
    uint32_t        find_memory_type(uint32_t filter, VkMemoryPropertyFlags props);
    void            create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags props,
                                  VkBuffer& buffer, VkDeviceMemory& memory);
    void            copy_buffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    void            transition_image_layout(VkImage image, VkImageLayout oldLayout,
                                            VkImageLayout newLayout);
    void            copy_buffer_to_image(VkBuffer buffer, VkImage image,
                                         uint32_t width, uint32_t height);
    VkCommandBuffer begin_single_time_commands();
    void            end_single_time_commands(VkCommandBuffer cmd);
    VkImageView     create_image_view(VkImage image, VkFormat format,
                                      VkImageAspectFlags aspect);
    VkFormat        find_supported_format(const std::vector<VkFormat>& candidates,
                                          VkImageTiling tiling,
                                          VkFormatFeatureFlags features);
    VkFormat        find_depth_format();
    VkShaderModule  create_shader_module(const std::vector<char>& code);
};
