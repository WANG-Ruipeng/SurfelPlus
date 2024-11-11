#include "surfel_compute_pass.h"

//Reference: https://vulkan-tutorial.com/Compute_shader

void SurfelComputePass::setup(const VkDevice& device, const VkPhysicalDevice& physicalDevice,
    uint32_t familyIndex, nvvk::ResourceAllocator* allocator) {
    // Create shader module
    std::vector<uint32_t> perpareComputeShader(std::begin(surfel_prepare_comp),
        std::end(surfel_prepare_comp));

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = perpareComputeShader.size() * sizeof(uint32_t);
    createInfo.pCode = perpareComputeShader.data();

    if (vkCreateShaderModule(device, &createInfo, nullptr, &m_shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }

	// Create Descriptor Set Layout
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{}; 

    // GBuffer primObjID binding
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[0].pImmutableSamplers = nullptr;

    // GBuffer normal binding
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].pImmutableSamplers = nullptr;

    // Depth buffer binding
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }

    // Create Descriptor Pool
    std::array<VkDescriptorPoolSize, 1> descPoolSizes{}; 
    descPoolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descPoolSizes[0].descriptorCount = 2; 

    VkDescriptorPoolCreateInfo descPoolInfo{};
    descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descPoolInfo.poolSizeCount = static_cast<uint32_t>(descPoolSizes.size());
    descPoolInfo.pPoolSizes = descPoolSizes.data();
    descPoolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(device, &descPoolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }

    // Allocate Descriptor Set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor set!");
    }

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }

    // Create compute pipeline
    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = m_shaderModule;
    shaderStageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = m_pipelineLayout;

    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_computePipeline) != VK_SUCCESS) {
        throw std::runtime_error("failed to create compute pipeline!");
    }

    // Get compute queue
    vkGetDeviceQueue(device, familyIndex, 0, &m_computeQueue);

    // Create Command Pool
    VkCommandPoolCreateInfo cmdPoolInfo{};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;  // Allow reset
	cmdPoolInfo.queueFamilyIndex = familyIndex;  // TODO: The compute queue family index is not set yet

    if (vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }

    // Create Command Buffer
    VkCommandBufferAllocateInfo cmdBufferAllocInfo{};
    cmdBufferAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufferAllocInfo.commandPool = m_commandPool;
    cmdBufferAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;  // Primary buffer set
    cmdBufferAllocInfo.commandBufferCount = 1;  // Only one buffer

    if (vkAllocateCommandBuffers(device, &cmdBufferAllocInfo, &m_commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffer!");
    }
}

void SurfelComputePass::dispatch() {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);

    // Bind pipeline
    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);

    // Bind descriptor set
    vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

	//TODO: Get image size from surfel, assume 1024*1024 for now
    VkExtent2D imageSize; 
	imageSize.width = 1024;
	imageSize.height = 1024;

    uint32_t groupsX = (imageSize.width + 16 - 1) / 16;   
    uint32_t groupsY = (imageSize.height + 16 - 1) / 16;

    // Dispatch compute work
    vkCmdDispatch(m_commandBuffer, groupsX, groupsY, 1);

    vkEndCommandBuffer(m_commandBuffer);
}

void SurfelComputePass::submit(const VkDevice& device) {
    // Create fence for synchronization
    VkFence fence;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(device, &fenceInfo, nullptr, &fence);

    // Submit command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;

    vkQueueSubmit(m_computeQueue, 1, &submitInfo, fence);

    // Wait for completion
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);

    // Cleanup Fence
    vkDestroyFence(device, fence, nullptr);
}