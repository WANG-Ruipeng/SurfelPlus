#include "temporal_spatial_pass.h"

#include "nvvk/images_vk.hpp"
#include "nvh/alignment.hpp"
#include "nvh/fileoperations.hpp"
#include "nvvk/shaders_vk.hpp"
#include "scene.hpp"
#include "tools.hpp"
#include "nvvk/commands_vk.hpp"
#include "shaders/host_device.h"

#include "autogen/spatial_filter.comp.h"

void TemporalSpatialPass::setup(const VkDevice& device, const VkPhysicalDevice& physicalDevice, uint32_t familyIndex, nvvk::ResourceAllocator* allocator)
{
	m_device = device;
	m_pAlloc = allocator;
	m_queueIndex = familyIndex;
	m_debug.setup(device);
}

void TemporalSpatialPass::destroy()
{
	vkDestroyPipeline(m_device, m_pipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);

	m_pipelineLayout = VK_NULL_HANDLE;
	m_pipeline = VK_NULL_HANDLE;
}

void TemporalSpatialPass::run(const VkCommandBuffer& cmdBuf, const VkExtent2D& size, nvvk::ProfilerVK& profiler, const std::vector<VkDescriptorSet>& descSets)
{
	LABEL_SCOPE_VK(cmdBuf);
	const int GROUP_SIZE = 16;
	auto halfSize = VkExtent2D(size.width * 0.5, size.height * 0.5);
	// Preparing for the compute shader
	vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
	vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0,
		static_cast<uint32_t>(descSets.size()), descSets.data(), 0, nullptr);

	// Sending the push constant information
	vkCmdPushConstants(cmdBuf, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(RtxState), &m_state);

	// Dispatching the shader
	vkCmdDispatch(cmdBuf, (halfSize.width + (GROUP_SIZE - 1)) / GROUP_SIZE, (halfSize.height + (GROUP_SIZE - 1)) / GROUP_SIZE, 1);
}

void TemporalSpatialPass::create(const VkExtent2D& fullSize, const std::vector<VkDescriptorSetLayout>& extraDescSetsLayout, Scene* _scene)
{
	VkExtent2D size = { fullSize.width / 2, fullSize.height / 2 };

	std::vector<VkPushConstantRange> push_constants;
	push_constants.push_back({ VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(RtxState) });

	VkPipelineLayoutCreateInfo layout_info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	layout_info.pushConstantRangeCount = static_cast<uint32_t>(push_constants.size());
	layout_info.pPushConstantRanges = push_constants.data();
	layout_info.setLayoutCount = static_cast<uint32_t>(extraDescSetsLayout.size());
	layout_info.pSetLayouts = extraDescSetsLayout.data();
	vkCreatePipelineLayout(m_device, &layout_info, nullptr, &m_pipelineLayout);

	VkComputePipelineCreateInfo computePipelineCreateInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	computePipelineCreateInfo.layout = m_pipelineLayout;
	computePipelineCreateInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	computePipelineCreateInfo.stage.module = nvvk::createShaderModule(m_device, spatial_filter_comp, sizeof(spatial_filter_comp));
	computePipelineCreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	computePipelineCreateInfo.stage.pName = "main";

	vkCreateComputePipelines(m_device, {}, 1, &computePipelineCreateInfo, nullptr, &m_pipeline);

	m_debug.setObjectName(m_pipeline, "Temporal Spatial Denoise Compute Pass");
	vkDestroyShaderModule(m_device, computePipelineCreateInfo.stage.module, nullptr);
}

const std::string TemporalSpatialPass::name()
{
	return "Reflection Compute Pass";
}