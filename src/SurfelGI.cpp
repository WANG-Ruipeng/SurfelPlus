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

void SurfelGI::createGbuffers(const VkExtent2D& size, VkRenderPass renderPass)
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

		m_gbufferResources.m_frameBuffers.resize(2);
		for (uint32_t i = 0; i < 2; i++)
		{
			attachments[0] = m_gbufferResources.m_images[0].descriptor.imageView;
			attachments[1] = m_gbufferResources.m_images[1].descriptor.imageView;
			attachments[2] = m_gbufferResources.m_images[2].descriptor.imageView;
			vkCreateFramebuffer(m_device, &framebufferCreateInfo, nullptr, &m_gbufferResources.m_frameBuffers[i]);
		}
	}
}
