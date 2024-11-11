#pragma once

#include "nvvk/resourceallocator_vk.hpp"
#include "nvvk/debug_util_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"

#include "nvvk/profiler_vk.hpp"
#include "renderer.h"
#include "shaders/host_device.h"

class GbufferPass : Renderer
{
public:
	struct InstanceData
	{
		glm::mat4 model;
		uint32_t id;
	};

	void setup(const VkDevice& device, const VkPhysicalDevice& physicalDevice, uint32_t familyIndex, nvvk::ResourceAllocator* allocator);
	void destroy();
	void create(const VkExtent2D& size, const std::vector<VkDescriptorSetLayout>& descSetLayouts, Scene* scene);
	void run(const VkCommandBuffer& cmdBuf, const VkExtent2D& size,
		nvvk::ProfilerVK& profiler, const std::vector<VkDescriptorSet>& descSets);
	const std::string name() { return std::string("GbufferPass"); }

	void createRenderPass();
	VkRenderPass getRenderPass() { return m_renderPass; };

	void beginRenderPass(const VkCommandBuffer& cmdBuf, VkFramebuffer framebuffer, const VkExtent2D& size);
	void endRenderPass(const VkCommandBuffer& cmdBuf);

private:
	// Setup
	nvvk::ResourceAllocator* m_pAlloc{ nullptr };  // Allocator for buffer, images, acceleration structures
	nvvk::DebugUtil          m_debug;            // Utility to name objects
	VkDevice                 m_device{ VK_NULL_HANDLE };
	uint32_t                 m_queueIndex{ 0 };
	VkExtent2D			     m_size;
	Scene*					 m_scene{ nullptr };


	VkPipelineLayout m_pipelineLayout{ VK_NULL_HANDLE };
	VkPipeline       m_pipeline{ VK_NULL_HANDLE };
	VkRenderPass	 m_renderPass{ VK_NULL_HANDLE };
};

