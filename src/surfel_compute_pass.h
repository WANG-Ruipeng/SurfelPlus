#include "nvvk/resourceallocator_vk.hpp"
#include "nvvk/debug_util_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"

class SurfelComputePass {
public:
    void setup(const VkDevice& device, nvvk::ResourceAllocator* allocator, uint32_t queueFamilyIndex);
    void destroy();
    void createPipeline(const std::vector<VkDescriptorSetLayout>& descSetLayouts);
    void dispatch(const VkCommandBuffer& cmdBuf, VkDescriptorSet descSet);

private:
    VkDevice m_device{ VK_NULL_HANDLE };
    nvvk::ResourceAllocator* m_allocator{ nullptr };
    VkPipeline m_pipeline{ VK_NULL_HANDLE };
    VkPipelineLayout m_pipelineLayout{ VK_NULL_HANDLE };
    VkDescriptorSetLayout m_descSetLayout{ VK_NULL_HANDLE };
    nvvk::DebugUtil m_debug;
};
