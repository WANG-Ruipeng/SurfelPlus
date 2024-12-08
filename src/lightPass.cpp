#include "lightPass.h"

#include "nvh/fileoperations.hpp"
#include "nvvk/commands_vk.hpp"
#include "nvvk/images_vk.hpp"
#include "nvvk/pipeline_vk.hpp"
#include "nvvk/renderpasses_vk.hpp"

#include "scene.hpp"

#include "autogen/passthrough.vert.h"
#include "autogen/lightPass.frag.h"

void LightPass::setup(const VkDevice& device, const VkPhysicalDevice& physicalDevice, uint32_t familyIndex, nvvk::ResourceAllocator* allocator)
{
	m_device = device;
	m_pAlloc = allocator;
	m_queueIndex = familyIndex;
	m_debug.setup(device);

	m_pipelineLayout = VK_NULL_HANDLE;
	m_pipeline = VK_NULL_HANDLE;
}

void LightPass::destroy()
{
	m_pAlloc->destroy(m_offscreenColor);

	vkDestroyPipeline(m_device, m_pipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);

	m_pipelineLayout = VK_NULL_HANDLE;
	m_pipeline = VK_NULL_HANDLE;

	vkDestroyRenderPass(m_device, m_renderPass, nullptr);
	m_renderPass = VK_NULL_HANDLE;
}

void LightPass::create(const VkExtent2D& size, const std::vector<VkDescriptorSetLayout>& descSetLayouts, Scene* scene)
{
	m_size = size;
	m_scene = scene;

	//createRenderPass();

	std::vector<VkPushConstantRange> push_constants;
	push_constants.push_back({ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(RtxState) }); 

	VkPipelineLayoutCreateInfo layout_info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	layout_info.pushConstantRangeCount = static_cast<uint32_t>(push_constants.size());
	layout_info.pPushConstantRanges = push_constants.data();
	layout_info.setLayoutCount = static_cast<uint32_t>(descSetLayouts.size());
	layout_info.pSetLayouts = descSetLayouts.data();
	vkCreatePipelineLayout(m_device, &layout_info, nullptr, &m_pipelineLayout);

	std::vector<uint32_t> vertexShader(std::begin(passthrough_vert), std::end(passthrough_vert));
	std::vector<uint32_t> fragShader(std::begin(lightPass_frag), std::end(lightPass_frag));

	std::array<VkFormat, 1> colorFormats = { m_colorFormat}; 

	VkPipelineRenderingCreateInfo prend_info{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
	prend_info.colorAttachmentCount = 1;
	prend_info.pColorAttachmentFormats = &colorFormats[0];

	nvvk::GraphicsPipelineGeneratorCombined pipelineGenerator(m_device, m_pipelineLayout, m_renderPass);
	pipelineGenerator.setPipelineRenderingCreateInfo(prend_info);
	pipelineGenerator.addShader(vertexShader, VK_SHADER_STAGE_VERTEX_BIT);
	pipelineGenerator.addShader(fragShader, VK_SHADER_STAGE_FRAGMENT_BIT);

	pipelineGenerator.setBlendAttachmentState(0, pipelineGenerator.makePipelineColorBlendAttachmentState(0xf, VK_FALSE));
	pipelineGenerator.rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;


	CREATE_NAMED_VK(m_pipeline, pipelineGenerator.createPipeline());


}

void LightPass::createFrameBuffer(const VkExtent2D& size, const VkFormat& colorFormat, nvvk::Queue queue)
{

	m_colorFormat = colorFormat;

	createRenderPass();

	if (m_offscreenColor.image != VK_NULL_HANDLE)
	{
		m_pAlloc->destroy(m_offscreenColor);
	}

	// Creating the color image
	{
		auto colorCreateInfo = nvvk::makeImage2DCreateInfo(
			size, m_colorFormat,
			VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, true);
		colorCreateInfo.mipLevels = 1;

		nvvk::Image image = m_pAlloc->createImage(colorCreateInfo);
		NAME_VK(image.image);
		VkImageViewCreateInfo ivInfo = nvvk::makeImageViewCreateInfo(image.image, colorCreateInfo);
		ivInfo.subresourceRange.levelCount = 1;

		VkSamplerCreateInfo sampler{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
		sampler.maxLod = FLT_MAX;
		m_offscreenColor = m_pAlloc->createTexture(image, ivInfo, sampler);
		m_offscreenColor.descriptor.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		nvvk::CommandPool cmdBufGet(m_device, queue.familyIndex, VK_COMMAND_POOL_CREATE_TRANSIENT_BIT, queue.queue);
		auto              cmdBuf = cmdBufGet.createCommandBuffer();
		nvvk::cmdBarrierImageLayout(cmdBuf, m_offscreenColor.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		cmdBufGet.submitAndWait(cmdBuf);
		//// Transition the image to general layout
		//nvvk::CommandPool genCmdBuf(m_device, m_queueIndex);
		//auto cmdBuf = genCmdBuf.createCommandBuffer();

		//nvvk::cmdBarrierImageLayout(
		//	cmdBuf,
		//	m_offscreenColor.image,
		//	VK_IMAGE_LAYOUT_UNDEFINED,
		//	VK_IMAGE_LAYOUT_GENERAL
		//);


		//genCmdBuf.submitAndWait(cmdBuf);
	}

	if (m_framebuffer != VK_NULL_HANDLE)
	{
		vkDestroyFramebuffer(m_device, m_framebuffer, nullptr);
		m_framebuffer = VK_NULL_HANDLE; 
	}

	std::array<VkImageView, 1> attachments = {
		m_offscreenColor.descriptor.imageView
	};

	// create framebuffer
	VkFramebufferCreateInfo framebufferCreateInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
	framebufferCreateInfo.renderPass = m_renderPass;  
	framebufferCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	framebufferCreateInfo.pAttachments = attachments.data();
	framebufferCreateInfo.width = size.width;   
	framebufferCreateInfo.height = size.height;
	framebufferCreateInfo.layers = 1;  

	VkResult result = vkCreateFramebuffer(m_device, &framebufferCreateInfo, nullptr, &m_framebuffer);
	if (result != VK_SUCCESS)
	{
		throw std::runtime_error("Failed to create framebuffer");
	}
}

void LightPass::createLightPassDescriptorSet(const VkDescriptorSetLayout& descSetLayout)
{
	nvvk::DescriptorSetBindings bind;

	vkDestroyDescriptorPool(m_device, m_descPool, nullptr);

	bind.addBinding({ OutputBindings::eSampler, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT });
	bind.addBinding({ OutputBindings::eStore, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1,
					 VK_SHADER_STAGE_COMPUTE_BIT});
	m_descPool = bind.createPool(m_device);
	m_descSet = nvvk::allocateDescriptorSet(m_device, m_descPool, descSetLayout);

	auto colorCreateInfo = nvvk::makeImage2DCreateInfo(
		m_size, m_colorFormat,
		VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, true);
	colorCreateInfo.mipLevels = 1;
	VkImageViewCreateInfo ivInfo = nvvk::makeImageViewCreateInfo(m_offscreenColor.image, colorCreateInfo);

	// update the descriptor set
	VkDescriptorImageInfo descImg[2] = {
		m_offscreenColor.descriptor,
		m_offscreenColor.descriptor
	};
	descImg[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	descImg[1].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	std::vector<VkWriteDescriptorSet> writes;
	writes.emplace_back(bind.makeWrite(m_descSet, OutputBindings::eSampler, &descImg[0]));
	writes.emplace_back(bind.makeWrite(m_descSet, OutputBindings::eStore, &descImg[1]));
	vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

void LightPass::layoutTransition(VkCommandBuffer cmdBuf)
{
	nvvk::cmdBarrierImageLayout(cmdBuf, m_offscreenColor.image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

void LightPass::run(const VkCommandBuffer& cmdBuf,
	const VkExtent2D& size,
	nvvk::ProfilerVK& profiler,
	const std::vector<VkDescriptorSet>& descSets)
{

	LABEL_SCOPE_VK(cmdBuf);

	auto area = glm::vec2(size.width, size.height);

	VkViewport viewport{ static_cast<float>(0),
						static_cast<float>(0),
						static_cast<float>(size.width),
						static_cast<float>(size.height),
						0.0f,
						1.0f };
	VkRect2D   scissor{ {0,0}, {size.width, size.height} };
	//VkRect2D   scissor{ renderArea.offset, {size.width, size.height} };
	vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
	vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

	// Sending the push constant information
	vkCmdPushConstants(cmdBuf, m_pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(RtxState), &m_state);

	vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
	vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, static_cast<uint32_t>(descSets.size()), descSets.data(), 0, nullptr);
	vkCmdDraw(cmdBuf, 3, 1, 0, 0);
}

void LightPass::createRenderPass()
{
	if (m_renderPass)
		vkDestroyRenderPass(m_device, m_renderPass, nullptr);

	std::array<VkAttachmentDescription, 1> attachments{};

	// Color attachment
	attachments[0].format = m_colorFormat;  
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentReference colorReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

	VkSubpassDescription subpassDescription{};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;

	// Subpass dependencies
	std::array<VkSubpassDependency, 1> subpassDependencies{};
	subpassDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	subpassDependencies[0].dstSubpass = 0;
	subpassDependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	subpassDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	subpassDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(subpassDependencies.size());
	renderPassInfo.pDependencies = subpassDependencies.data();

	vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass);
}

void LightPass::beginRenderPass(const VkCommandBuffer& cmdBuf, const VkExtent2D& size)
{

	std::array<VkClearValue, 1> clearValues;
	clearValues[0].color = { {0, 0, 0, 0} };

	VkRenderPassBeginInfo postRenderPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	postRenderPassBeginInfo.clearValueCount = clearValues.size();
	postRenderPassBeginInfo.pClearValues = clearValues.data();
	postRenderPassBeginInfo.renderPass = m_renderPass;
	postRenderPassBeginInfo.framebuffer = m_framebuffer;
	postRenderPassBeginInfo.renderArea = { {}, size };

	vkCmdBeginRenderPass(cmdBuf, &postRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

//void LightPass::beginRenderPass(const VkCommandBuffer& cmdBuf, const VkExtent2D& size, const VkFramebuffer& framebuffer)
//{
//
//	std::array<VkClearValue, 1> clearValues;
//	clearValues[0].color = { {0, 0, 0, 0} };
//
//	VkRenderPassBeginInfo postRenderPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
//	postRenderPassBeginInfo.clearValueCount = clearValues.size();
//	postRenderPassBeginInfo.pClearValues = clearValues.data();
//	postRenderPassBeginInfo.renderPass = m_renderPass;
//	postRenderPassBeginInfo.framebuffer = framebuffer;
//	postRenderPassBeginInfo.renderArea = { {}, size };
//
//	vkCmdBeginRenderPass(cmdBuf, &postRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
//}

void LightPass::endRenderPass(const VkCommandBuffer& cmdBuf)
{
	vkCmdEndRenderPass(cmdBuf);

}
