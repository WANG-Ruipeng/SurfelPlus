#pragma once
#pragma once

#include "nvvk/resourceallocator_vk.hpp"
#include "nvvk/debug_util_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"
#include "nvh/gltfscene.hpp"
#include "nvh/fileoperations.hpp"
#include "queue.hpp"
#include "nvvk/profiler_vk.hpp"
#include "renderer.h"
#include "shaders/host_device.h"

class TAASharpenPass : Renderer
{
    enum Queues
    {
        eGraphics,
        eLoading,
        eCompute,
        eTransfer
    };
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
	void createTAADescriptorSet(const VkExtent2D& fullSize, nvvk::Queue queue);

    // getters
    VkDescriptorSetLayout getSamplerDescSetLayout() { return m_sampleDescSetLayout; }
    VkDescriptorSet getSamplerDescSet() { return m_sampleDescSet; }
    std::vector<nvvk::Texture>& getImages() { return m_images; }

private:
    // Setup
    nvvk::ResourceAllocator* m_pAlloc{ nullptr };  // Allocator for buffer, images, acceleration structures
    nvvk::DebugUtil          m_debug;            // Utility to name objects
    VkDevice                 m_device{ VK_NULL_HANDLE };
    uint32_t                 m_queueIndex{ 0 };

    VkPipelineLayout m_pipelineLayout{ VK_NULL_HANDLE };
    VkPipeline       m_pipeline{ VK_NULL_HANDLE };
    VkDescriptorPool m_descPool{ VK_NULL_HANDLE };
    VkDescriptorSet  m_descSet{ VK_NULL_HANDLE };
    std::vector<nvvk::Texture> m_images;
    VkDescriptorSetLayout	   m_sampleDescSetLayout{ VK_NULL_HANDLE };
    VkDescriptorSet			   m_sampleDescSet{ VK_NULL_HANDLE };

};

