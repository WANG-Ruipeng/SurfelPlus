#include "SurfelGI.h"

#include "nvvk/commands_vk.hpp"
#include "nvvk/images_vk.hpp"
#include "nvvk/pipeline_vk.hpp"
#include "nvvk/renderpasses_vk.hpp"

void SurfelGI::setup(const VkDevice& device, const VkPhysicalDevice& physicalDevice, const std::vector<nvvk::Queue>& queues, nvvk::ResourceAllocator* allocator)
{
	m_device = device;
	m_pAlloc = allocator;
	m_queues.assign(queues.begin(), queues.end());
	m_debug.setup(device);
}

void SurfelGI::createGbuffers(const VkExtent2D& size, const size_t frameBufferCnt, VkRenderPass renderPass)
{
	// creating objPrimID, normal, and depth images
	{
		auto objprimIDCreateInfo = nvvk::makeImage2DCreateInfo(
			size, VK_FORMAT_R32_UINT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false);

		auto normalCreateInfo = nvvk::makeImage2DCreateInfo(
			size, VK_FORMAT_R32_UINT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false);

		const VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
		VkImageCreateInfo        depthCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		depthCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		depthCreateInfo.extent = VkExtent3D{ size.width, size.height, 1 };
		depthCreateInfo.format = m_gbufferDepthFormat;
		depthCreateInfo.mipLevels = 1;
		depthCreateInfo.arrayLayers = 1;
		depthCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		depthCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		nvvk::Image objPrimID = m_pAlloc->createImage(objprimIDCreateInfo);
		nvvk::Image normal = m_pAlloc->createImage(normalCreateInfo);
		nvvk::Image depth = m_pAlloc->createImage(depthCreateInfo);

		VkImageViewCreateInfo objPrimIDvInfo = nvvk::makeImageViewCreateInfo(objPrimID.image, objprimIDCreateInfo);
		VkImageViewCreateInfo normalvInfo = nvvk::makeImageViewCreateInfo(normal.image, normalCreateInfo);

		VkImageSubresourceRange subresourceRange{};
		subresourceRange.aspectMask = aspect;
		subresourceRange.levelCount = 1;
		subresourceRange.layerCount = 1;
		VkImageViewCreateInfo depthvInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		depthvInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depthvInfo.format = m_gbufferDepthFormat;
		depthvInfo.subresourceRange = subresourceRange;
		depthvInfo.image = depth.image;

		VkSamplerCreateInfo objPrimIDsampler{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		VkSamplerCreateInfo normalsampler{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		VkSamplerCreateInfo depthsampler{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

		nvvk::Texture objPrimIdTex = m_pAlloc->createTexture(objPrimID, objPrimIDvInfo, objPrimIDsampler);
		objPrimIdTex.descriptor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		nvvk::Texture normalTex = m_pAlloc->createTexture(normal, normalvInfo, normalsampler);
		normalTex.descriptor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		nvvk::Texture depthTex = m_pAlloc->createTexture(depth, depthvInfo, depthsampler);
		depthTex.descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;


	// Setting the image layout for both color and depth

		nvvk::CommandPool cmdBufGet(m_device, m_queues[eLoading].familyIndex, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, m_queues[eLoading].queue);
		VkCommandBuffer   cmdBuf = cmdBufGet.createCommandBuffer();

		nvvk::cmdBarrierImageLayout(cmdBuf, objPrimID.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		nvvk::cmdBarrierImageLayout(cmdBuf, normal.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		nvvk::cmdBarrierImageLayout(cmdBuf, depth.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);

		cmdBufGet.submitAndWait(cmdBuf);

		m_gbufferResources.m_images.push_back(objPrimIdTex);
		m_gbufferResources.m_images.push_back(normalTex);
		m_gbufferResources.m_images.push_back(depthTex);
	}


	VkShaderStageFlags flags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	nvvk::DescriptorSetBindings bind;
	// objPrimID
	bind.addBinding({ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, flags });
	// normal
	bind.addBinding({ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, flags });
	// depth
	bind.addBinding({ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, flags });

	// allocate the descriptor set
	m_gbufferResources.m_descPool = bind.createPool(m_device);
	m_gbufferResources.m_descSetLayout = bind.createLayout(m_device);
	m_gbufferResources.m_descSet = nvvk::allocateDescriptorSet(m_device, m_gbufferResources.m_descPool, m_gbufferResources.m_descSetLayout);

	// update the descriptor set
	std::vector<VkWriteDescriptorSet> writes;
	VkDescriptorImageInfo descImg[3] = {
		m_gbufferResources.m_images[0].descriptor,
		m_gbufferResources.m_images[1].descriptor,
		m_gbufferResources.m_images[2].descriptor
	};
	descImg[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	descImg[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	descImg[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	writes.emplace_back(bind.makeWrite(m_gbufferResources.m_descSet, 0, &descImg[0]));
	writes.emplace_back(bind.makeWrite(m_gbufferResources.m_descSet, 1, &descImg[1]));
	writes.emplace_back(bind.makeWrite(m_gbufferResources.m_descSet, 2, &descImg[2]));
	vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

	// Recreate the frame buffers
	{
		for (auto framebuffer : m_gbufferResources.m_frameBuffers)
			vkDestroyFramebuffer(m_device, framebuffer, nullptr);

		// Array of attachment (primId, nor, depth)
		std::array<VkImageView, 3> attachments{};

		// Create frame buffers for every swap chain image
		VkFramebufferCreateInfo framebufferCreateInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
		framebufferCreateInfo.renderPass = renderPass;
		framebufferCreateInfo.attachmentCount = attachments.size();
		framebufferCreateInfo.width = size.width;
		framebufferCreateInfo.height = size.height;
		framebufferCreateInfo.layers = 1;
		framebufferCreateInfo.pAttachments = attachments.data();

		m_gbufferResources.m_frameBuffers.resize(frameBufferCnt);
		for (uint32_t i = 0; i < frameBufferCnt; i++)
		{
			attachments[0] = m_gbufferResources.m_images[0].descriptor.imageView;
			attachments[1] = m_gbufferResources.m_images[1].descriptor.imageView;
			attachments[2] = m_gbufferResources.m_images[2].descriptor.imageView;
			vkCreateFramebuffer(m_device, &framebufferCreateInfo, nullptr, &m_gbufferResources.m_frameBuffers[i]);
		}
	}
}

void SurfelGI::createComputeResources()
{
	// Create descriptor set layout
	nvvk::DescriptorSetBindings bind;
	bind.addBinding({ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT }); // Surfel counter buffer

	m_computeResources.descPool = bind.createPool(m_device);
	m_computeResources.descSetLayout = bind.createLayout(m_device);
	m_computeResources.descSet = nvvk::allocateDescriptorSet(m_device, m_computeResources.descPool,
		m_computeResources.descSetLayout);

	// Create surfel counter buffer
	VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.size = sizeof(uint32_t) * 6; // For all counter types
	bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	m_computeResources.surfelCounterBuffer = m_pAlloc->createBuffer(bufferInfo,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

	// Create pipeline layout
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &m_computeResources.descSetLayout;
	vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_computeResources.pipelineLayout);

	// Create compute shader module, read file
	std::string computeShaderCode = nvh::loadFile("src/surfel_prepare.comp", true);

	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = computeShaderCode.size();
	createInfo.pCode = reinterpret_cast<const uint32_t*>(computeShaderCode.data());

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
		throw std::runtime_error("failed to create compute shader module!");
	}

	// Create compute pipeline
	VkComputePipelineCreateInfo computePipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	computePipelineInfo.layout = m_computeResources.pipelineLayout;

	VkPipelineShaderStageCreateInfo shaderStageInfo{};
	shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	shaderStageInfo.module = shaderModule;
	shaderStageInfo.pName = "main";  // 入口函数名

	computePipelineInfo.stage = shaderStageInfo;

	if (vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr,
		&m_computeResources.pipeline) != VK_SUCCESS) {
		throw std::runtime_error("failed to create compute pipeline!");
	}

	// Update descriptor set
	VkDescriptorBufferInfo bufferDescriptorInfo{};
	bufferDescriptorInfo.buffer = m_computeResources.surfelCounterBuffer.buffer;
	bufferDescriptorInfo.offset = 0;
	bufferDescriptorInfo.range = VK_WHOLE_SIZE;

	VkWriteDescriptorSet descriptorWrite{};
	descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	descriptorWrite.dstSet = m_computeResources.descSet;
	descriptorWrite.dstBinding = 0;
	descriptorWrite.dstArrayElement = 0;
	descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	descriptorWrite.descriptorCount = 1;
	descriptorWrite.pBufferInfo = &bufferDescriptorInfo;

	vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);

	// Clean up shader module
	vkDestroyShaderModule(m_device, shaderModule, nullptr);
}


void SurfelGI::destroyComputeResources()
{
	vkDestroyPipeline(m_device, m_computeResources.pipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_computeResources.pipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_computeResources.descSetLayout, nullptr);
	vkDestroyDescriptorPool(m_device, m_computeResources.descPool, nullptr);
	m_pAlloc->destroy(m_computeResources.surfelCounterBuffer);
}

void SurfelGI::dispatchCompute(VkCommandBuffer cmdBuf)
{
	vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_computeResources.pipeline);
	vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
		m_computeResources.pipelineLayout, 0, 1,
		&m_computeResources.descSet, 0, nullptr);

	// Add memory barrier if needed
	VkMemoryBarrier memoryBarrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(cmdBuf,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0,
		1, &memoryBarrier,
		0, nullptr,
		0, nullptr);

	// Dispatch compute shader
	vkCmdDispatch(cmdBuf, 1, 1, 1);
}
