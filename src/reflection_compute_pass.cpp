#include "reflection_compute.h"

#include "nvvk/images_vk.hpp"
#include "nvh/alignment.hpp"
#include "nvh/fileoperations.hpp"
#include "nvvk/shaders_vk.hpp"
#include "scene.hpp"
#include "tools.hpp"
#include "nvvk/commands_vk.hpp"


#include "autogen/surfel_generation_pass.comp.h"

void ReflectionComputePass::setup(const VkDevice& device, const VkPhysicalDevice& physicalDevice, uint32_t familyIndex, nvvk::ResourceAllocator* allocator)
{
	m_device = device;
	m_pAlloc = allocator;
	m_queueIndex = familyIndex;
	m_debug.setup(device);
}

void ReflectionComputePass::destroy()
{
	vkDestroyPipeline(m_device, m_pipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);

	m_pipelineLayout = VK_NULL_HANDLE;
	m_pipeline = VK_NULL_HANDLE;
}

void ReflectionComputePass::run(const VkCommandBuffer& cmdBuf, const VkExtent2D& size, nvvk::ProfilerVK& profiler, const std::vector<VkDescriptorSet>& descSets)
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

void ReflectionComputePass::create(const VkExtent2D& size, const std::vector<VkDescriptorSetLayout>& extraDescSetsLayout, Scene* _scene)
{
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
	computePipelineCreateInfo.stage.module = nvvk::createShaderModule(m_device, surfel_generation_pass_comp, sizeof(surfel_generation_pass_comp));
	computePipelineCreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	computePipelineCreateInfo.stage.pName = "main";

	vkCreateComputePipelines(m_device, {}, 1, &computePipelineCreateInfo, nullptr, &m_pipeline);

	m_debug.setObjectName(m_pipeline, "Reflection Compute Pass");
	vkDestroyShaderModule(m_device, computePipelineCreateInfo.stage.module, nullptr);
}

const std::string ReflectionComputePass::name()
{
	return "Reflection Compute Pass";
}

void ReflectionComputePass::createReflectionPassDescriptorSet(const VkExtent2D& size, const size_t frameBufferCnt, VkRenderPass renderPass)
{
	// destroy previous descriptor set
	{
		for (int i = 0; i < m_images.size(); i++)
		{
			if (m_images[i].image != VK_NULL_HANDLE)
			{
				m_pAlloc->destroy(m_images[i]);
			}
		}

		if (m_samplerDescSetLayout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(m_device, m_samplerDescSetLayout, nullptr);
	}
	
	// create reflection map and direction map
	{
		auto reflectColorCreateInfo = nvvk::makeImage2DCreateInfo(
			size, VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
			, false);

		auto reflectDirectionCreateInfo = nvvk::makeImage2DCreateInfo(
			size, VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
			, false);

		const VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;

		nvvk::Image reflectColor = m_pAlloc->createImage(reflectColorCreateInfo);
		nvvk::Image reflectDirection = m_pAlloc->createImage(reflectDirectionCreateInfo);

		VkImageViewCreateInfo reflectColorvInfo = nvvk::makeImageViewCreateInfo(reflectColor.image, reflectColorCreateInfo);
		VkImageViewCreateInfo reflectDirectionvInfo = nvvk::makeImageViewCreateInfo(reflectDirection.image, reflectDirectionCreateInfo);

		VkSamplerCreateInfo reflectColorSampler{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		VkSamplerCreateInfo reflectDirectionSampler{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

		nvvk::Texture reflectColorTex = m_pAlloc->createTexture(reflectColor, reflectColorvInfo, reflectColorSampler);
		reflectColorTex.descriptor.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		nvvk::Texture reflectDirectionTex = m_pAlloc->createTexture(reflectDirection, reflectDirectionvInfo, reflectDirectionSampler);
		reflectColorTex.descriptor.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

		nvvk::CommandPool cmdBufGet(m_device, m_queues[eLoading].familyIndex, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, m_queues[eLoading].queue);
		VkCommandBuffer   cmdBuf = cmdBufGet.createCommandBuffer();

		nvvk::cmdBarrierImageLayout(cmdBuf, reflectColor.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
		nvvk::cmdBarrierImageLayout(cmdBuf, reflectDirection.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);

		cmdBufGet.submitAndWait(cmdBuf);

		m_images.push_back(reflectColorTex);
		m_images.push_back(reflectDirectionTex);
	}
}