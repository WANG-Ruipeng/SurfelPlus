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
	VkDescriptorPool		   m_descPool{ VK_NULL_HANDLE };
	VkDescriptorSetLayout	   m_descSetLayout;
	VkDescriptorSet			   m_descSet;
	std::vector<VkFramebuffer> m_frameBuffers;
};

class ComputeResources
{
public:
	VkPipeline pipeline{ VK_NULL_HANDLE };
	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorPool descPool{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descSetLayout{ VK_NULL_HANDLE };
	VkDescriptorSet descSet{ VK_NULL_HANDLE };
	nvvk::Buffer surfelCounterBuffer;  // For storing surfel counters
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

	void createGbuffers(const VkExtent2D& size, const size_t frameBufferCnt, VkRenderPass renderPass);
	VkFramebuffer getGbufferFramebuffer(uint32_t currFrame) { return m_gbufferResources.m_frameBuffers[currFrame]; }
	VkDescriptorSet getGbufferDescSet() { return m_gbufferResources.m_descSet; }

	// better to have a descriptor name
	VkDescriptorSetLayout            getDescLayout() { return m_descSetLayout; }
	VkDescriptorSet                  getDescSet() { return m_descSet; }

	// Get GBuffer Resources
	VkImageView getGBufferPrimIDView() const {
		return m_gbufferResources.m_images[0].descriptor.imageView;
	}
	VkImageView getGBufferNormalView() const {
		return m_gbufferResources.m_images[1].descriptor.imageView;
	}

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

	// Compute Resources
	ComputeResources m_computeResources;
};

struct MSMEData {
	// TODO: Add MSME data for future use
};

struct Surfel {
	MSMEData msmeData;
	glm::vec3 position;   
	glm::vec3 normal;
	glm::vec3 radiance;
	float radius;           
	float sumLuminance;   
	uint32_t rayOffset;    
	uint32_t rayCount;    
	bool hasHole;
};