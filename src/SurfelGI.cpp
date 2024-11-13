#include "SurfelGI.h"

#include "nvvk/commands_vk.hpp"
#include "nvvk/images_vk.hpp"
#include "nvvk/pipeline_vk.hpp"
#include "nvvk/renderpasses_vk.hpp"
#include "shaders/host_device.h"

void SurfelGI::setup(const VkDevice& device, const VkPhysicalDevice& physicalDevice, const std::vector<nvvk::Queue>& queues, nvvk::ResourceAllocator* allocator)
{
	m_device = device;
	m_pAlloc = allocator;
	m_queues.assign(queues.begin(), queues.end());
	m_debug.setup(device);
}

void SurfelGI::createResources(const VkExtent2D& size)
{
	nvvk::CommandPool cmdBufGet(m_device, m_queues[eGraphics].familyIndex, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, m_queues[eGraphics].queue);
	VkCommandBuffer   cmdBuf = cmdBufGet.createCommandBuffer();

	std::vector<VkDescriptorPoolSize> descriptorPoolSizes = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10 },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 10 },
	};
	m_descPool = nvvk::createDescriptorPool(m_device, descriptorPoolSizes, 20);

	std::vector<SurfelCounter> counters = { {0, maxSurfelCnt, 0} };
	m_surfelCounterBuffer = m_pAlloc->createBuffer(cmdBuf, counters, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	std::vector<Surfel> surfels(maxSurfelCnt);
	m_surfelBuffer = m_pAlloc->createBuffer(cmdBuf, surfels, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	std::vector<uint32_t> surfelAliveBuffer(maxSurfelCnt, 0);
	m_surfelAliveBuffer = m_pAlloc->createBuffer(cmdBuf, surfelAliveBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	std::vector<uint32_t> surfelDeadBuffer(maxSurfelCnt, 0);
	for (int i = 0; i < maxSurfelCnt; i++)
		surfelDeadBuffer[i] = i;
	
	m_surfelDeadBuffer = m_pAlloc->createBuffer(cmdBuf, surfelDeadBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	
	std::vector<uint32_t> surfelDirtyBuffer(maxSurfelCnt, 0);
	m_surfelDirtyBuffer = m_pAlloc->createBuffer(cmdBuf, surfelDirtyBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	const uint32_t cellCountX = (size.width + cellSize - 1) / cellSize;
	const uint32_t cellCountY = (size.height + cellSize - 1) / cellSize;
	const uint32_t totalCellCount = cellCountX * cellCountY;
	std::vector<CellInfo> cells(totalCellCount);
	m_cellInfoBuffer = m_pAlloc->createBuffer(cmdBuf, cells, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	CellCounter cellCounter;
	cellCounter.totalCellCount = totalCellCount;
	std::vector<CellCounter> cellCounters = { cellCounter };
	m_cellCounterBuffer = m_pAlloc->createBuffer(cmdBuf, cellCounters, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	//TODO: Size should be different! Currently is the same as maxSurfelCnt, should have something different
	std::vector<uint32_t> cellToSurfelBuffer(maxSurfelCnt, 0);
	m_cellToSurfelBuffer = m_pAlloc->createBuffer(cmdBuf, cellToSurfelBuffer, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	cmdBufGet.submitAndWait(cmdBuf);

	// create indirect lighting map
	createIndirectLightingMap(size);

	// Create the surfel descriptor set layout
	{
		nvvk::DescriptorSetBindings bind;
		bind.addBinding({ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT });
		bind.addBinding({ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT });
		bind.addBinding({ 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT });
		bind.addBinding({ 3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT });
		bind.addBinding({ 4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT });

		m_surfelBuffersDescSetLayout = bind.createLayout(m_device);

		// Create the edscriptor set
		m_surfelBuffersDescSet = nvvk::allocateDescriptorSet(m_device, m_descPool, m_surfelBuffersDescSetLayout);

		std::array<VkDescriptorBufferInfo, 5> dbi;
		dbi[0] = VkDescriptorBufferInfo{ m_surfelCounterBuffer.buffer, 0, VK_WHOLE_SIZE };
		dbi[1] = VkDescriptorBufferInfo{ m_surfelBuffer.buffer, 0, VK_WHOLE_SIZE };
		dbi[2] = VkDescriptorBufferInfo{ m_surfelAliveBuffer.buffer, 0, VK_WHOLE_SIZE };
		dbi[3] = VkDescriptorBufferInfo{ m_surfelDeadBuffer.buffer, 0, VK_WHOLE_SIZE };
		dbi[4] = VkDescriptorBufferInfo{ m_surfelDirtyBuffer.buffer, 0, VK_WHOLE_SIZE };

		std::vector<VkWriteDescriptorSet> writes;
		writes.emplace_back(bind.makeWrite(m_surfelBuffersDescSet, 0, &dbi[0]));
		writes.emplace_back(bind.makeWrite(m_surfelBuffersDescSet, 1, &dbi[1]));
		writes.emplace_back(bind.makeWrite(m_surfelBuffersDescSet, 2, &dbi[2]));
		writes.emplace_back(bind.makeWrite(m_surfelBuffersDescSet, 3, &dbi[3]));
		writes.emplace_back(bind.makeWrite(m_surfelBuffersDescSet, 4, &dbi[4]));

		// Writing the information
		vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	}

	// Create cell buffer descriptor set layout
	{
		nvvk::DescriptorSetBindings bind;
		bind.addBinding({ 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT });
		bind.addBinding({ 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT });
		bind.addBinding({ 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT });

		m_cellBufferDescSetLayout = bind.createLayout(m_device);

		// Create descriptor set
		m_cellBufferDescSet = nvvk::allocateDescriptorSet(m_device, m_descPool, m_cellBufferDescSetLayout);

		// Write descriptor set
		std::array<VkDescriptorBufferInfo, 3> dbi;
		dbi[0] = VkDescriptorBufferInfo{ m_cellInfoBuffer.buffer, 0, VK_WHOLE_SIZE };
		dbi[1] = VkDescriptorBufferInfo{ m_cellCounterBuffer.buffer, 0, VK_WHOLE_SIZE };
		dbi[2] = VkDescriptorBufferInfo{ m_cellToSurfelBuffer.buffer, 0, VK_WHOLE_SIZE };
		
		std::vector<VkWriteDescriptorSet> writes;
		writes.emplace_back(bind.makeWrite(m_cellBufferDescSet, 0, &dbi[0]));
		writes.emplace_back(bind.makeWrite(m_cellBufferDescSet, 1, &dbi[1]));
		writes.emplace_back(bind.makeWrite(m_cellBufferDescSet, 2, &dbi[2]));

		vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	}

}

void SurfelGI::createIndirectLightingMap(const VkExtent2D& size)
{
	if (m_indirectLightingMap.image != VK_NULL_HANDLE)
	{
		m_pAlloc->destroy(m_indirectLightingMap);
	}

	// Creating indirect lighting image
	{
		auto colorCreateInfo = nvvk::makeImage2DCreateInfo(
			size, VK_FORMAT_R8G8B8A8_UNORM,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, true);

		nvvk::Image image = m_pAlloc->createImage(colorCreateInfo);
		NAME_VK(image.image);
		VkImageViewCreateInfo ivInfo = nvvk::makeImageViewCreateInfo(image.image, colorCreateInfo);

		VkSamplerCreateInfo sampler{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		sampler.maxLod = FLT_MAX;
		m_indirectLightingMap = m_pAlloc->createTexture(image, ivInfo, sampler);
		m_indirectLightingMap.descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	}

	// Setting the image layout
	{
		nvvk::CommandPool cmdBufGet(m_device, m_queues[eLoading].familyIndex, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, m_queues[eLoading].queue);
		auto              cmdBuf = cmdBufGet.createCommandBuffer();
		nvvk::cmdBarrierImageLayout(cmdBuf, m_indirectLightingMap.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

		cmdBufGet.submitAndWait(cmdBuf);
	}

	nvvk::DescriptorSetBindings bind;

	vkDestroyDescriptorSetLayout(m_device, m_indirectLightDescSetLayout, nullptr);

	bind.addBinding({ OutputBindings::eSampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT });
	bind.addBinding({ OutputBindings::eStore, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
					 VK_SHADER_STAGE_COMPUTE_BIT});
	m_indirectLightDescSetLayout = bind.createLayout(m_device);
	m_indirectLightDescSet = nvvk::allocateDescriptorSet(m_device, m_descPool, m_indirectLightDescSetLayout);

	std::vector<VkWriteDescriptorSet> writes;
	writes.emplace_back(bind.makeWrite(m_indirectLightDescSet, OutputBindings::eSampler, &m_indirectLightingMap.descriptor));
	writes.emplace_back(bind.makeWrite(m_indirectLightDescSet, OutputBindings::eStore, &m_indirectLightingMap.descriptor));
	vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void SurfelGI::createGbuffers(const VkExtent2D& size, const size_t frameBufferCnt, VkRenderPass renderPass)
{
	// creating objPrimID, normal, and depth images
	{
		auto objprimIDCreateInfo = nvvk::makeImage2DCreateInfo(
			size, VK_FORMAT_R32_UINT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
			, false);

		auto normalCreateInfo = nvvk::makeImage2DCreateInfo(
			size, VK_FORMAT_R32_UINT,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
			, false);

		const VkImageAspectFlags aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
		VkImageCreateInfo        depthCreateInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		depthCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		depthCreateInfo.extent = VkExtent3D{ size.width, size.height, 1 };
		depthCreateInfo.format = m_gbufferDepthFormat;
		depthCreateInfo.mipLevels = 1;
		depthCreateInfo.arrayLayers = 1;
		depthCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		depthCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

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
		objPrimIdTex.descriptor.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

		nvvk::Texture normalTex = m_pAlloc->createTexture(normal, normalvInfo, normalsampler);
		normalTex.descriptor.imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;

		nvvk::Texture depthTex = m_pAlloc->createTexture(depth, depthvInfo, depthsampler);
		depthTex.descriptor.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;


	// Setting the image layout for both color and depth

		nvvk::CommandPool cmdBufGet(m_device, m_queues[eLoading].familyIndex, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, m_queues[eLoading].queue);
		VkCommandBuffer   cmdBuf = cmdBufGet.createCommandBuffer();

		nvvk::cmdBarrierImageLayout(cmdBuf, objPrimID.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
		nvvk::cmdBarrierImageLayout(cmdBuf, normal.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
		nvvk::cmdBarrierImageLayout(cmdBuf, depth.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);

		cmdBufGet.submitAndWait(cmdBuf);

		m_gbufferResources.m_images.push_back(objPrimIdTex);
		m_gbufferResources.m_images.push_back(normalTex);
		m_gbufferResources.m_images.push_back(depthTex);
	}


	VkShaderStageFlags flags = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	// gbuffer sampler descriptor set
	{
		nvvk::DescriptorSetBindings bind;
		// objPrimID
		bind.addBinding({ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, flags });
		// normal
		bind.addBinding({ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, flags });
		// depth
		bind.addBinding({ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, flags });

		// allocate the descriptor set
		m_gbufferResources.m_samplerDescSetLayout = bind.createLayout(m_device);
		m_gbufferResources.m_samplerDescSet = nvvk::allocateDescriptorSet(m_device,
			m_descPool, m_gbufferResources.m_samplerDescSetLayout);

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
		writes.emplace_back(bind.makeWrite(m_gbufferResources.m_samplerDescSet, 0, &descImg[0]));
		writes.emplace_back(bind.makeWrite(m_gbufferResources.m_samplerDescSet, 1, &descImg[1]));
		writes.emplace_back(bind.makeWrite(m_gbufferResources.m_samplerDescSet, 2, &descImg[2]));
		vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	}

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

void SurfelGI::gbufferLayoutTransition(VkCommandBuffer cmdBuf)
{
	nvvk::cmdBarrierImageLayout(cmdBuf, m_gbufferResources.m_images[0].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
	nvvk::cmdBarrierImageLayout(cmdBuf, m_gbufferResources.m_images[1].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL);
	nvvk::cmdBarrierImageLayout(cmdBuf, m_gbufferResources.m_images[2].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);
}


