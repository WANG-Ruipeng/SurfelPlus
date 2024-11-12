#pragma once

#include "nvvk/resourceallocator_vk.hpp"
#include "nvvk/debug_util_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"

#include "nvvk/profiler_vk.hpp"
#include "renderer.h"
#include "shaders/host_device.h"

class SurfelGenerationPass : Renderer
{
public:
    void setup(const VkDevice& device,
        const VkPhysicalDevice& physicalDevice,
        uint32_t                 familyIndex,
        nvvk::ResourceAllocator* allocator);
    void destroy();
    void run(const VkCommandBuffer& cmdBuf,
        const VkExtent2D& size,
        nvvk::ProfilerVK& profiler,
        const std::vector<VkDescriptorSet>& descSets);
    void create(const VkExtent2D& size, const std::vector<VkDescriptorSetLayout>& descSetsLayout, Scene* _scene = nullptr);
    const std::string name();
    void          setPushContants(const RtxState& state) { m_state = state; }

private:
    // Setup
    nvvk::ResourceAllocator* m_pAlloc{ nullptr };  // Allocator for buffer, images, acceleration structures
    nvvk::DebugUtil          m_debug;            // Utility to name objects
    VkDevice                 m_device{ VK_NULL_HANDLE };
    uint32_t                 m_queueIndex{ 0 };


    VkPipelineLayout m_pipelineLayout{ VK_NULL_HANDLE };
    VkPipeline       m_pipeline{ VK_NULL_HANDLE };
    VkRenderPass     m_renderPass{ VK_NULL_HANDLE };
};

