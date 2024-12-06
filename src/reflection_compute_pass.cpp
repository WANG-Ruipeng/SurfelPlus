#include "reflection_compute.h"

#include "nvvk/images_vk.hpp"
#include "nvh/alignment.hpp"
#include "nvh/fileoperations.hpp"
#include "nvvk/shaders_vk.hpp"
#include "scene.hpp"
#include "tools.hpp"
#include "nvvk/commands_vk.hpp"
#include "shaders/host_device.h"

#include "autogen/reflection_generation.comp.h"

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

void ReflectionComputePass::create(const VkExtent2D& fullSize, const std::vector<VkDescriptorSetLayout>& extraDescSetsLayout, Scene* _scene)
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
	computePipelineCreateInfo.stage.module = nvvk::createShaderModule(m_device, reflection_generation_comp, sizeof(reflection_generation_comp));
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

void ReflectionComputePass::createReflectionPassDescriptorSet(const VkExtent2D& fullSize, nvvk::Queue queue)
{
	VkExtent2D size = { fullSize.width / 2, fullSize.height / 2 };
	std::vector<VkDescriptorPoolSize> descriptorPoolSizes = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 5 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5 }
	};
	m_descPool = nvvk::createDescriptorPool(m_device, descriptorPoolSizes, 1);
 
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
		// create info
		auto reflectColorCreateInfo = nvvk::makeImage2DCreateInfo(
			size, VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, true);

		auto reflectDirectionCreateInfo = nvvk::makeImage2DCreateInfo(
			size, VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, true);

		auto reflectBrdfCreateInfo = nvvk::makeImage2DCreateInfo(
			size, VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, true);

		auto filteredReflectionCreateInfo = nvvk::makeImage2DCreateInfo(
			fullSize, VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
			true);
		auto bilateralCleanupCreateInfo = nvvk::makeImage2DCreateInfo(
			fullSize, VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT,
			true);

		// create image
		nvvk::Image reflectColor = m_pAlloc->createImage(reflectColorCreateInfo);
		NAME_VK(reflectColor.image);
		nvvk::Image reflectDirection = m_pAlloc->createImage(reflectDirectionCreateInfo);
		NAME_VK(reflectDirection.image);
		nvvk::Image reflectBrdf = m_pAlloc->createImage(reflectBrdfCreateInfo);
		NAME_VK(reflectBrdf.image);
		nvvk::Image filteredReflection = m_pAlloc->createImage(filteredReflectionCreateInfo);
		NAME_VK(filteredReflection.image);
		nvvk::Image bilateralCleanup = m_pAlloc->createImage(bilateralCleanupCreateInfo);
		NAME_VK(bilateralCleanup.image);

		// create image view
		VkImageViewCreateInfo reflectColorvInfo = nvvk::makeImageViewCreateInfo(reflectColor.image, reflectColorCreateInfo);
		VkImageViewCreateInfo reflectDirectionvInfo = nvvk::makeImageViewCreateInfo(reflectDirection.image, reflectDirectionCreateInfo);
		VkImageViewCreateInfo reflectBrdfvInfo = nvvk::makeImageViewCreateInfo(reflectBrdf.image, reflectBrdfCreateInfo);
		VkImageViewCreateInfo filteredReflectionvInfo = nvvk::makeImageViewCreateInfo(filteredReflection.image, filteredReflectionCreateInfo);
		VkImageViewCreateInfo bilateralCleanupvInfo = nvvk::makeImageViewCreateInfo(bilateralCleanup.image, bilateralCleanupCreateInfo);

		// create sampler
		VkSamplerCreateInfo reflectColorSampler{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		reflectColorSampler.magFilter = VK_FILTER_LINEAR;
		reflectColorSampler.minFilter = VK_FILTER_LINEAR;

		VkSamplerCreateInfo reflectDirectionSampler{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		reflectDirectionSampler.magFilter = VK_FILTER_LINEAR;
		reflectDirectionSampler.minFilter = VK_FILTER_LINEAR;

		VkSamplerCreateInfo reflectBrdfSampler{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		reflectBrdfSampler.magFilter = VK_FILTER_LINEAR;
		reflectBrdfSampler.minFilter = VK_FILTER_LINEAR;

		VkSamplerCreateInfo filteredReflectionSampler{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		filteredReflectionSampler.magFilter = VK_FILTER_LINEAR;
		filteredReflectionSampler.minFilter = VK_FILTER_LINEAR;

		VkSamplerCreateInfo bilateralCleanupSampler{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		bilateralCleanupSampler.magFilter = VK_FILTER_LINEAR;
		bilateralCleanupSampler.minFilter = VK_FILTER_LINEAR;

		// create texture
		nvvk::Texture reflectColorTex = m_pAlloc->createTexture(reflectColor, reflectColorvInfo, reflectColorSampler);
		reflectColorTex.descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		nvvk::Texture reflectDirectionTex = m_pAlloc->createTexture(reflectDirection, reflectDirectionvInfo, reflectDirectionSampler);
		reflectDirectionTex.descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		nvvk::Texture reflectBrdfTex = m_pAlloc->createTexture(reflectBrdf, reflectBrdfvInfo, reflectBrdfSampler);
		reflectBrdfTex.descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		nvvk::Texture filteredReflectionTex = m_pAlloc->createTexture(filteredReflection, filteredReflectionvInfo, filteredReflectionSampler);
		filteredReflectionTex.descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
		nvvk::Texture bilateralCleanupTex = m_pAlloc->createTexture(bilateralCleanup, bilateralCleanupvInfo, bilateralCleanupSampler);
		bilateralCleanupTex.descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;


		nvvk::CommandPool cmdBufGet(m_device, queue.familyIndex, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, queue.queue);
		VkCommandBuffer   cmdBuf = cmdBufGet.createCommandBuffer();

		nvvk::cmdBarrierImageLayout(cmdBuf, reflectColor.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		nvvk::cmdBarrierImageLayout(cmdBuf, reflectDirection.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		nvvk::cmdBarrierImageLayout(cmdBuf, reflectBrdf.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		nvvk::cmdBarrierImageLayout(cmdBuf, filteredReflection.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		nvvk::cmdBarrierImageLayout(cmdBuf, bilateralCleanup.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

		cmdBufGet.submitAndWait(cmdBuf);

		m_images.push_back(reflectColorTex);
		m_images.push_back(reflectDirectionTex);
		m_images.push_back(reflectBrdfTex);
		m_images.push_back(filteredReflectionTex);
		m_images.push_back(bilateralCleanupTex);
	}

	// reflection description set
	{

		nvvk::DescriptorSetBindings bind;
		bind.addBinding({ 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT });
		bind.addBinding({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT });
		bind.addBinding({ 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT });
		bind.addBinding({ 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT });
		bind.addBinding({ 4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT });

		bind.addBinding({ 5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT });
		bind.addBinding({ 6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT });
		bind.addBinding({ 7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT });
		bind.addBinding({ 8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT });
		bind.addBinding({ 9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT });

		// allocate the descriptor set
		m_samplerDescSetLayout = bind.createLayout(m_device);
		m_samplerDescSet = nvvk::allocateDescriptorSet(m_device,
			m_descPool, m_samplerDescSetLayout);

		// update the descriptor set
		std::vector<VkWriteDescriptorSet> writes;

		VkDescriptorImageInfo descImg[5] = {
			m_images[0].descriptor,
			m_images[1].descriptor,
			m_images[2].descriptor,
			m_images[3].descriptor,
			m_images[4].descriptor
		};

		writes.emplace_back(bind.makeWrite(m_samplerDescSet, 0, &descImg[0]));
		writes.emplace_back(bind.makeWrite(m_samplerDescSet, 1, &descImg[1]));
		writes.emplace_back(bind.makeWrite(m_samplerDescSet, 2, &descImg[2]));
		writes.emplace_back(bind.makeWrite(m_samplerDescSet, 3, &descImg[3]));
		writes.emplace_back(bind.makeWrite(m_samplerDescSet, 4, &descImg[4]));
		writes.emplace_back(bind.makeWrite(m_samplerDescSet, 5, &descImg[0]));
		writes.emplace_back(bind.makeWrite(m_samplerDescSet, 6, &descImg[1]));
		writes.emplace_back(bind.makeWrite(m_samplerDescSet, 7, &descImg[2]));
		writes.emplace_back(bind.makeWrite(m_samplerDescSet, 8, &descImg[3]));
		writes.emplace_back(bind.makeWrite(m_samplerDescSet, 9, &descImg[4]));
		vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	}

}