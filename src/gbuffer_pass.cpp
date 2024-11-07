#include "gbuffer_pass.h"

#include "nvh/fileoperations.hpp"
#include "nvvk/commands_vk.hpp"
#include "nvvk/images_vk.hpp"
#include "nvvk/pipeline_vk.hpp"
#include "nvvk/renderpasses_vk.hpp"

#include "scene.hpp"

#include "autogen/gbuffer.vert.h"
#include "autogen/gbuffer.frag.h"

void GbufferPass::setup(const VkDevice& device, const VkPhysicalDevice& physicalDevice, uint32_t familyIndex, nvvk::ResourceAllocator* allocator)
{
	m_device = device;
	m_pAlloc = allocator;
	m_queueIndex = familyIndex;
	m_debug.setup(device);

	m_pipelineLayout = VK_NULL_HANDLE;
	m_pipeline = VK_NULL_HANDLE;
}

void GbufferPass::destroy()
{
	vkDestroyPipeline(m_device, m_pipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);

	m_pipelineLayout = VK_NULL_HANDLE;
	m_pipeline = VK_NULL_HANDLE;

	vkDestroyRenderPass(m_device, m_renderPass, nullptr);
	m_renderPass = VK_NULL_HANDLE;
}

void GbufferPass::create(const VkExtent2D& size, const std::vector<VkDescriptorSetLayout>& descSetLayouts, Scene* scene)
{
	m_size = size;
	m_scene = scene;

	createRenderPass();

	std::vector<VkPushConstantRange> push_constants;
	push_constants.push_back({ VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(InstanceData) });

	VkPipelineLayoutCreateInfo layout_info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	layout_info.pushConstantRangeCount = static_cast<uint32_t>(push_constants.size());
	layout_info.pPushConstantRanges = push_constants.data();
	layout_info.setLayoutCount = static_cast<uint32_t>(descSetLayouts.size());
	layout_info.pSetLayouts = descSetLayouts.data();
	vkCreatePipelineLayout(m_device, &layout_info, nullptr, &m_pipelineLayout);

	std::vector<uint32_t> vertexShader(std::begin(gbuffer_vert), std::end(gbuffer_vert));
	std::vector<uint32_t> fragShader(std::begin(gbuffer_frag), std::end(gbuffer_frag));

	std::array<VkFormat, 2> colorFormats = { VK_FORMAT_R32_UINT , VK_FORMAT_R32_UINT };

	VkPipelineRenderingCreateInfo prend_info{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
	prend_info.colorAttachmentCount = 2;
	prend_info.pColorAttachmentFormats = &colorFormats[0];
	prend_info.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

	nvvk::GraphicsPipelineGeneratorCombined pipelineGenerator(m_device, m_pipelineLayout, m_renderPass);
	pipelineGenerator.setPipelineRenderingCreateInfo(prend_info);
	pipelineGenerator.addShader(vertexShader, VK_SHADER_STAGE_VERTEX_BIT);
	pipelineGenerator.addShader(fragShader, VK_SHADER_STAGE_FRAGMENT_BIT);

	pipelineGenerator.setBlendAttachmentState(0, pipelineGenerator.makePipelineColorBlendAttachmentState(0xf, VK_FALSE));
	pipelineGenerator.addBlendAttachmentState(pipelineGenerator.makePipelineColorBlendAttachmentState(0xf, VK_FALSE));
	pipelineGenerator.rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;

	const std::vector<VkVertexInputBindingDescription> vertexInputBindingsInterleaved = {
		{ 0, sizeof(VertexAttributes), VK_VERTEX_INPUT_RATE_VERTEX },
	};
	const std::vector<VkVertexInputAttributeDescription> vertexInputAttributesInterleaved = {
		{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(VertexAttributes, position) },
		{ 1, 0, VK_FORMAT_R32_UINT, offsetof(VertexAttributes, normal) }
	};

	pipelineGenerator.addBindingDescriptions(vertexInputBindingsInterleaved);
	pipelineGenerator.addAttributeDescriptions(vertexInputAttributesInterleaved);

	CREATE_NAMED_VK(m_pipeline, pipelineGenerator.createPipeline());
}

void GbufferPass::run(const VkCommandBuffer& cmdBuf, const VkExtent2D& size, const VkRect2D& renderArea, nvvk::ProfilerVK& profiler, const std::vector<VkDescriptorSet>& descSets)
{

	LABEL_SCOPE_VK(cmdBuf);
	auto area = glm::vec2(renderArea.extent.width, renderArea.extent.height);

	VkViewport viewport{ static_cast<float>(renderArea.offset.x),
						static_cast<float>(renderArea.offset.y),
						static_cast<float>(renderArea.extent.width),
						static_cast<float>(renderArea.extent.height),
						0.0f,
						1.0f };
	VkRect2D   scissor{ renderArea.offset, {renderArea.extent.width, renderArea.extent.height} };
	//VkRect2D   scissor{ renderArea.offset, {size.width, size.height} };
	vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
	vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

	vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
	vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0,
		static_cast<uint32_t>(descSets.size()), descSets.data(), 0, nullptr);

	const std::vector<nvvk::Buffer>& vertexBuffers = m_scene->getBuffers(Scene::eVertex);
	const std::vector<nvvk::Buffer>& indexBuffers = m_scene->getBuffers(Scene::eIndex);
	const std::vector<uint32_t>& indicesCount = m_scene->getIndicesCount();
	VkDeviceSize offsets[] = { 0 };

	InstanceData instanceData;

	for (const auto& node : m_scene->getScene().m_nodes)
	{
		instanceData.id = node.primMesh;
		instanceData.model = node.worldMatrix;

		// Sending the push constant information
		vkCmdPushConstants(cmdBuf, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(InstanceData), &instanceData);

		vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vertexBuffers[instanceData.id].buffer, offsets);

		vkCmdBindIndexBuffer(cmdBuf, indexBuffers[instanceData.id].buffer, 0, VK_INDEX_TYPE_UINT32);

		// Drawing the object
		vkCmdDrawIndexed(cmdBuf, indicesCount[instanceData.id], 1, 0, 0, 0);
	}

}

void GbufferPass::createRenderPass()
{
	if (m_renderPass)
		vkDestroyRenderPass(m_device, m_renderPass, nullptr);

	std::array<VkAttachmentDescription, 3> attachments{};
	// objPrimID attachment
	attachments[0].format = VK_FORMAT_R32_UINT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;

	// normal attachment
	attachments[1].format = VK_FORMAT_R32_UINT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;

	// depth attachment
	attachments[2].format = VK_FORMAT_D32_SFLOAT;
	attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[2].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;


	const VkAttachmentReference primReference{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	const VkAttachmentReference normalReference{ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
	const VkAttachmentReference depthReference{ 2, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL };

	std::array< VkAttachmentReference, 2> colorReferences = { primReference, normalReference };

	std::array<VkSubpassDependency, 1> subpassDependencies{};
	// Transition from final to initial (VK_SUBPASS_EXTERNAL refers to all commands executed outside of the actual renderpass)
	subpassDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	subpassDependencies[0].dstSubpass = 0;
	subpassDependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	subpassDependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpassDependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	subpassDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	subpassDependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkSubpassDescription subpassDescription{};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 2;
	subpassDescription.pColorAttachments = &colorReferences[0];
	subpassDescription.pDepthStencilAttachment = &depthReference;

	VkRenderPassCreateInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(subpassDependencies.size());
	renderPassInfo.pDependencies = subpassDependencies.data();

	vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass);
}

void GbufferPass::beginRenderPass(const VkCommandBuffer& cmdBuf, VkFramebuffer framebuffer, const VkExtent2D& size)
{
	std::array<VkClearValue, 3> clearValues;
	clearValues[0].color = { {0, 0, 0, 0} };
	clearValues[1].color = { {0, 0, 0, 0} };
	clearValues[2].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo postRenderPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	postRenderPassBeginInfo.clearValueCount = clearValues.size();
	postRenderPassBeginInfo.pClearValues = clearValues.data();
	postRenderPassBeginInfo.renderPass = m_renderPass;
	postRenderPassBeginInfo.framebuffer = framebuffer;
	postRenderPassBeginInfo.renderArea = { {}, size};

	vkCmdBeginRenderPass(cmdBuf, &postRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void GbufferPass::endRenderPass(const VkCommandBuffer& cmdBuf)
{
	vkCmdEndRenderPass(cmdBuf);
}
