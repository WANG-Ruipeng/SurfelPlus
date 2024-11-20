#pragma once

#include <string>
#include <glm/glm.hpp>

#include "nvh/gltfscene.hpp"
#include "nvvk/resourceallocator_vk.hpp"
#include "nvvk/debug_util_vk.hpp"
#include "nvvk/descriptorsets_vk.hpp"
#include "nvh/fileoperations.hpp"
#include "queue.hpp"

class GBufferResources
{
public:
	enum BufferType
	{
		ObjPrimID,
		Normal,
		Depth
	};
	std::vector<nvvk::Texture> m_images;
	VkDescriptorSetLayout	   m_samplerDescSetLayout{ VK_NULL_HANDLE };
	VkDescriptorSet			   m_samplerDescSet{ VK_NULL_HANDLE };
	VkDescriptorSetLayout	   m_imageDescSetLayout{ VK_NULL_HANDLE };
	VkDescriptorSet			   m_imageDescSet{ VK_NULL_HANDLE };
	std::vector<VkFramebuffer> m_frameBuffers;
};


class SurfelGI
{
public:
	enum Queues
	{
		eGraphics,
		eLoading,
		eCompute,
		eTransfer
	};

	void setup(const VkDevice& device, const VkPhysicalDevice& physicalDevice, const std::vector<nvvk::Queue>& queues, nvvk::ResourceAllocator* allocator);

	void createResources(const VkExtent2D& size);
	void createIndirectLightingMap(const VkExtent2D& size);
	void createGbuffers(const VkExtent2D& size, const size_t frameBufferCnt, VkRenderPass renderPass);
	void createIrradianceDepthMap();
	VkFramebuffer			getGbufferFramebuffer(uint32_t currFrame) { return m_gbufferResources.m_frameBuffers[currFrame]; }
	VkDescriptorSetLayout	getGbufferSamplerDescLayout() { return m_gbufferResources.m_samplerDescSetLayout; }
	VkDescriptorSet			getGbufferSamplerDescSet() { return m_gbufferResources.m_samplerDescSet; }
	VkDescriptorSetLayout	getGbufferImageDescLayout() { return m_gbufferResources.m_samplerDescSetLayout; }
	VkDescriptorSet			getGbufferImageDescSet() { return m_gbufferResources.m_samplerDescSet; }
	nvvk::Buffer			getSurfelCounterBuffer() { return m_surfelCounterBuffer; }

	VkDescriptorSetLayout            getSurfelBuffersDescLayout() { return m_surfelBuffersDescSetLayout; }
	VkDescriptorSet                  getSurfelBuffersDescSet() { return m_surfelBuffersDescSet; }
	VkDescriptorSetLayout            getIndirectLightDescLayout() { return m_indirectLightDescSetLayout; }
	VkDescriptorSet                  getIndirectLightDescSet() { return m_indirectLightDescSet; }
	VkDescriptorSetLayout			 getCellBufferDescLayout() { return m_cellBufferDescSetLayout;}
	VkDescriptorSet					 getCellBufferDescSet() { return m_cellBufferDescSet; }
	VkDescriptorSetLayout			 getSurfelDataMapsDescLayout() { return m_surfelDataMapsDescSetLayout; }
	VkDescriptorSet					 getSurfelDataMapsDescSet() { return m_surfelDataMapsDescSet; }

	void gbufferLayoutTransition(VkCommandBuffer cmdBuf);

	// Surfel Configuration
	const uint32_t maxSurfelCnt = 10000;
	const uint32_t maxRayBudget = maxSurfelCnt * 32;

private:

	// Setup
	nvvk::ResourceAllocator* m_pAlloc;  // Allocator for buffer, images, acceleration structures
	nvvk::DebugUtil          m_debug;   // Utility to name objects
	VkDevice                 m_device;
	std::vector<nvvk::Queue> m_queues;


	// Resources
	//std::array<nvvk::Buffer, 5>                            m_buffer;           // For single buffer
	//std::array<std::vector<nvvk::Buffer>, 2>               m_buffers;          // For array of buffers (vertex/index)
	//std::vector<nvvk::Texture>                             m_textures;         // vector of all textures of the scene
	//std::vector<std::pair<nvvk::Image, VkImageCreateInfo>> m_images;           // vector of all images of the scene
	//std::vector<size_t>                                    m_defaultTextures;  // for cleanup

	VkDescriptorPool      m_descPool{ VK_NULL_HANDLE };
	VkDescriptorSetLayout m_descSetLayout{ VK_NULL_HANDLE };
	VkDescriptorSet       m_descSet{ VK_NULL_HANDLE };

	// GBuufer Resources
	GBufferResources	  m_gbufferResources;
	VkFormat 			  m_gbufferDepthFormat{ VK_FORMAT_D32_SFLOAT };

	// Surfel Resources
	nvvk::Buffer				m_surfelCounterBuffer{ VK_NULL_HANDLE };
	nvvk::Buffer				m_surfelBuffer{ VK_NULL_HANDLE };
	nvvk::Buffer				m_surfelAliveBuffer{ VK_NULL_HANDLE };
	nvvk::Buffer				m_surfelDeadBuffer{ VK_NULL_HANDLE };
	nvvk::Buffer				m_surfelDirtyBuffer{ VK_NULL_HANDLE };
	nvvk::Buffer				m_surfelRecycleBuffer{ VK_NULL_HANDLE };
	nvvk::Buffer				m_surfelRayBuffer{ VK_NULL_HANDLE };
	
	// Cell Resources
	nvvk::Buffer 				m_cellInfoBuffer{ VK_NULL_HANDLE };
	nvvk::Buffer				m_cellCounterBuffer{ VK_NULL_HANDLE };
	nvvk::Buffer 			    m_cellToSurfelBuffer{ VK_NULL_HANDLE };

	nvvk::Texture				m_indirectLightingMap;
	nvvk::Texture				m_surfelIrradianceMap;
	nvvk::Texture				m_surfelDepthMap;

	VkDescriptorSetLayout       m_surfelBuffersDescSetLayout{ VK_NULL_HANDLE };
	VkDescriptorSet             m_surfelBuffersDescSet{ VK_NULL_HANDLE };
	VkDescriptorSetLayout       m_indirectLightDescSetLayout{ VK_NULL_HANDLE };
	VkDescriptorSet             m_indirectLightDescSet{ VK_NULL_HANDLE };
	VkDescriptorSetLayout		m_cellBufferDescSetLayout{ VK_NULL_HANDLE };
	VkDescriptorSet				m_cellBufferDescSet{ VK_NULL_HANDLE };
	VkDescriptorSetLayout		m_surfelDataMapsDescSetLayout{ VK_NULL_HANDLE };
	VkDescriptorSet				m_surfelDataMapsDescSet{ VK_NULL_HANDLE };

	// Basic Cell Info
	const uint32_t cellSize = 32;

};