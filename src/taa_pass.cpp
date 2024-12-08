#include "taa_pass.h"

#include "nvvk/images_vk.hpp"
#include "nvh/alignment.hpp"
#include "nvh/fileoperations.hpp"
#include "nvvk/shaders_vk.hpp"
#include "scene.hpp"
#include "tools.hpp"
#include "nvvk/commands_vk.hpp"
#include "shaders/host_device.h"

#include "autogen/taa_pass.comp.h"

void TAAPass::setup(const VkDevice& device, const VkPhysicalDevice& physicalDevice, uint32_t familyIndex, nvvk::ResourceAllocator* allocator)
{
	m_device = device;
	m_pAlloc = allocator;
	m_queueIndex = familyIndex;
	m_debug.setup(device);
}

void TAAPass::destroy()
{
	vkDestroyPipeline(m_device, m_pipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);

	m_pipelineLayout = VK_NULL_HANDLE;
	m_pipeline = VK_NULL_HANDLE;
}

void TAAPass::run(const VkCommandBuffer& cmdBuf, const VkExtent2D& size, nvvk::ProfilerVK& profiler, const std::vector<VkDescriptorSet>& descSets)
{
	LABEL_SCOPE_VK(cmdBuf);
	const int GROUP_SIZE = 8;
	// Preparing for the compute shader
	vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
	vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0,
		static_cast<uint32_t>(descSets.size()), descSets.data(), 0, nullptr);

	// Sending the push constant information
	vkCmdPushConstants(cmdBuf, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(RtxState), &m_state);

	// Dispatching the shader
	vkCmdDispatch(cmdBuf, (size.width + (GROUP_SIZE - 1)) / GROUP_SIZE, (size.height + (GROUP_SIZE - 1)) / GROUP_SIZE, 1);
}

void TAAPass::create(const VkExtent2D& fullSize, const std::vector<VkDescriptorSetLayout>& extraDescSetsLayout, Scene* _scene)
{
	VkExtent2D size = { fullSize.width, fullSize.height};

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
	computePipelineCreateInfo.stage.module = nvvk::createShaderModule(m_device, taa_pass_comp, sizeof(taa_pass_comp));
	computePipelineCreateInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	computePipelineCreateInfo.stage.pName = "main";

	vkCreateComputePipelines(m_device, {}, 1, &computePipelineCreateInfo, nullptr, &m_pipeline);

	m_debug.setObjectName(m_pipeline, "TAA Pass");
	vkDestroyShaderModule(m_device, computePipelineCreateInfo.stage.module, nullptr);
}

const std::string TAAPass::name()
{
	return "TAA Pass";
}

void TAAPass::createTAADescriptorSet(const VkExtent2D& fullSize, nvvk::Queue queue)
{
	VkExtent2D size = { fullSize.width, fullSize.height };
	std::vector<VkDescriptorPoolSize> descriptorPoolSizes = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 }
	};
	m_descPool = nvvk::createDescriptorPool(m_device, descriptorPoolSizes, 2);

	// destroy previous descriptor set
	{
		for (int i = 0; i < m_images.size(); i++)
		{
			if (m_images[i].image != VK_NULL_HANDLE)
			{
				m_pAlloc->destroy(m_images[i]);
			}
		}

		if (m_sampleDescSetLayout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(m_device, m_sampleDescSetLayout, nullptr);
	}

	// create reflection map and direction map
	{
		// create info
		auto bilateralCleanupCreateInfo1 = nvvk::makeImage2DCreateInfo(
			fullSize, VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
			true);
		auto bilateralCleanupCreateInfo2 = nvvk::makeImage2DCreateInfo(
			fullSize, VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
			true);

		// create image
		nvvk::Image bilateralCleanup1 = m_pAlloc->createImage(bilateralCleanupCreateInfo1);
		NAME_VK(bilateralCleanup1.image);
		nvvk::Image bilateralCleanup2 = m_pAlloc->createImage(bilateralCleanupCreateInfo2);
		NAME_VK(bilateralCleanup2.image);

		// create image view
		VkImageViewCreateInfo bilateralCleanupvInfo1 = nvvk::makeImageViewCreateInfo(bilateralCleanup1.image, bilateralCleanupCreateInfo1);
		VkImageViewCreateInfo bilateralCleanupvInfo2 = nvvk::makeImageViewCreateInfo(bilateralCleanup2.image, bilateralCleanupCreateInfo2);

		// create sampler
		VkSamplerCreateInfo bilateralCleanupSampler1{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		bilateralCleanupSampler1.magFilter = VK_FILTER_LINEAR;
		bilateralCleanupSampler1.minFilter = VK_FILTER_LINEAR;
		VkSamplerCreateInfo bilateralCleanupSampler2{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		bilateralCleanupSampler2.magFilter = VK_FILTER_LINEAR;
		bilateralCleanupSampler2.minFilter = VK_FILTER_LINEAR;

		// create texture
		nvvk::Texture bilateralCleanupTex1 = m_pAlloc->createTexture(bilateralCleanup1, bilateralCleanupvInfo1, bilateralCleanupSampler1);
		bilateralCleanupTex1.descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		nvvk::Texture bilateralCleanupTex2 = m_pAlloc->createTexture(bilateralCleanup2, bilateralCleanupvInfo2, bilateralCleanupSampler2);
		bilateralCleanupTex2.descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		nvvk::CommandPool cmdBufGet(m_device, queue.familyIndex, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, queue.queue);
		VkCommandBuffer   cmdBuf = cmdBufGet.createCommandBuffer();

		nvvk::cmdBarrierImageLayout(cmdBuf, bilateralCleanup1.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		nvvk::cmdBarrierImageLayout(cmdBuf, bilateralCleanup2.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

		cmdBufGet.submitAndWait(cmdBuf);

		m_images.push_back(bilateralCleanupTex1);
		m_images.push_back(bilateralCleanupTex2);

	}

	// reflection description set
	{
		nvvk::DescriptorSetBindings m_bind;
		m_bind.addBinding({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT });
		m_bind.addBinding({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT });
		m_bind.addBinding({ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT });
		m_bind.addBinding({ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT });

		// allocate the descriptor set
		m_sampleDescSetLayout = m_bind.createLayout(m_device);
		m_sampleDescSet = nvvk::allocateDescriptorSet(m_device,
			m_descPool, m_sampleDescSetLayout);

		// update the descriptor set
		std::vector<VkWriteDescriptorSet> writes;

		VkDescriptorImageInfo descImg[2] = {
			m_images[0].descriptor,
			m_images[1].descriptor,
		};

		writes.emplace_back(m_bind.makeWrite(m_sampleDescSet, 0, &descImg[0]));
		writes.emplace_back(m_bind.makeWrite(m_sampleDescSet, 1, &descImg[1]));
		writes.emplace_back(m_bind.makeWrite(m_sampleDescSet, 2, &descImg[0]));
		writes.emplace_back(m_bind.makeWrite(m_sampleDescSet, 3, &descImg[1]));

		vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

	}
}

