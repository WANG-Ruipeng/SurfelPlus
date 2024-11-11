#include "nvvk/resourceallocator_vk.hpp"
#include "nvvk/debug_util_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"

#include "autogen/surfel_prepare.comp.h"

#include "renderer.h"

class SurfelComputePass
{
public:
    void setup(const VkDevice& device, const VkPhysicalDevice& physicalDevice, uint32_t familyIndex, nvvk::ResourceAllocator* allocator);
    void dispatch();
    void submit(const VkDevice& device);

private:
    VkPipeline m_computePipeline;
    VkPipelineLayout m_pipelineLayout;
    VkCommandPool m_commandPool;
    VkCommandBuffer m_commandBuffer;
    VkQueue m_computeQueue;
    VkDescriptorPool m_descriptorPool;
    VkDescriptorSet m_descriptorSet;
    VkDescriptorSetLayout m_descSetLayout;
    VkShaderModule m_shaderModule;
    VkSampler m_depthSampler;
};
