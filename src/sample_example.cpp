/*
 * Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2021 NVIDIA CORPORATION
 * SPDX-License-Identifier: Apache-2.0
 */


/*
 * Main class to render the scene, holds sub-classes for various work
 */

#include <glm/glm.hpp>

#include <filesystem>
#include <thread>

#define VMA_IMPLEMENTATION

#include "shaders/host_device.h"
#include "rayquery.hpp"
#include "rtx_pipeline.hpp"
#include "sample_example.hpp"
#include "sample_gui.hpp"
#include "tools.hpp"

#include "nvml_monitor.hpp"


#if defined(NVP_SUPPORTS_NVML)
NvmlMonitor g_nvml(100, 100);
#endif

//--------------------------------------------------------------------------------------------------
// Keep the handle on the device
// Initialize the tool to do all our allocations: buffers, images
//
void SampleExample::setup(const VkInstance&               instance,
                          const VkDevice&                 device,
                          const VkPhysicalDevice&         physicalDevice,
                          const std::vector<nvvk::Queue>& queues)
{
  AppBaseVk::setup(instance, device, physicalDevice, queues[eGCT0].familyIndex);
  m_queues = std::vector<nvvk::Queue>{ queues.begin(), queues.end() };
  m_gui = std::make_shared<SampleGUI>(this);  // GUI of this class

  // Memory allocator for buffers and images
  m_alloc.init(instance, device, physicalDevice);

  m_debug.setup(m_device);

  // Compute queues can be use for acceleration structures
  m_picker.setup(m_device, physicalDevice, queues[eCompute].familyIndex, &m_alloc);
  m_accelStruct.setup(m_device, physicalDevice, queues[eCompute].familyIndex, &m_alloc);

  // Note: the GTC family queue is used because the nvvk::cmdGenerateMipmaps uses vkCmdBlitImage and this
  // command requires graphic queue and not only transfer.
  m_scene.setup(m_device, physicalDevice, queues[eGCT1], &m_alloc);

  // Transfer queues can be use for the creation of the following assets
  m_offscreen.setup(m_device, physicalDevice, queues[eTransfer].familyIndex, &m_alloc);
  m_skydome.setup(device, physicalDevice, queues[eTransfer].familyIndex, &m_alloc);

  m_surfel.setup(m_device, physicalDevice, queues, &m_alloc);
  m_gbufferPass.setup(m_device, physicalDevice, queues[eGCT0].familyIndex, &m_alloc);
  m_surfelPreparePass.setup(m_device, physicalDevice, queues[eGCT0].familyIndex, &m_alloc);
  m_surfelGenerationPass.setup(m_device, physicalDevice, queues[eGCT0].familyIndex, &m_alloc);
  m_surfelUpdatePass.setup(m_device, physicalDevice, queues[eGCT0].familyIndex, &m_alloc);
  m_cellInfoUpdatePass.setup(m_device, physicalDevice, queues[eGCT0].familyIndex, &m_alloc);
  m_cellToSurfelUpdatePass.setup(m_device, physicalDevice, queues[eGCT0].familyIndex, &m_alloc);
  m_surfelRaytracePass.setup(m_device, physicalDevice, queues[eGCT0].familyIndex, &m_alloc);
  m_surfelIntegratePass.setup(m_device, physicalDevice, queues[eGCT0].familyIndex, &m_alloc);
  m_indirectPostprocessPass.setup(m_device, physicalDevice, queues[eGCT0].familyIndex, &m_alloc);
  m_reflectionComputePass.setup(m_device, physicalDevice, queues[eGCT0].familyIndex, &m_alloc);
  m_temporalSpatialPass.setup(m_device, physicalDevice, queues[eGCT0].familyIndex, &m_alloc);
  m_bilateralCleanupPass.setup(m_device, physicalDevice, queues[eGCT0].familyIndex, &m_alloc);
  m_taaPass.setup(m_device, physicalDevice, queues[eGCT0].familyIndex, &m_alloc);
  m_taaSharpenPass.setup(m_device, physicalDevice, queues[eGCT0].familyIndex, &m_alloc);
  m_lightPass.setup(m_device, physicalDevice, queues[eGCT0].familyIndex, &m_alloc);

  // Create and setup all renderers
  m_pRender[eRtxPipeline] = new RtxPipeline;
  m_pRender[eRayQuery]    = new RayQuery;
  for(auto r : m_pRender)
  {
    r->setup(m_device, physicalDevice, queues[eTransfer].familyIndex, &m_alloc);
  }
  m_rtxState.totalFrames = 0;
}


//--------------------------------------------------------------------------------------------------
// Loading the scene file, setting up all scene buffers, create the acceleration structures
// for the loaded models.
//
void SampleExample::loadScene(const std::string& filename)
{
  m_scene.load(filename);
  m_accelStruct.create(m_scene.getScene(), m_scene.getBuffers(Scene::eVertex), m_scene.getBuffers(Scene::eIndex));

  // The picker is the helper to return information from a ray hit under the mouse cursor
  m_picker.setTlas(m_accelStruct.getTlas());
  resetFrame();
}

//--------------------------------------------------------------------------------------------------
// Loading an HDR image and creating the importance sampling acceleration structure
//
void SampleExample::loadEnvironmentHdr(const std::string& hdrFilename)
{
  MilliTimer timer;
  LOGI("Loading HDR and converting %s\n", hdrFilename.c_str());
  m_skydome.loadEnvironment(hdrFilename);
  timer.print();

  m_rtxState.fireflyClampThreshold = m_skydome.getIntegral() * 4.f;  // magic
}


//--------------------------------------------------------------------------------------------------
// Loading asset in a separate thread
// - Used by file drop and menu operation
// Marking the session as busy, to avoid calling rendering while loading assets
//
void SampleExample::loadAssets(const char* filename)
{
  std::string sfile = filename;

  // Need to stop current rendering
  m_busy = true;
  vkDeviceWaitIdle(m_device);

  std::thread([&, sfile]() {
    LOGI("Loading: %s\n", sfile.c_str());

    // Supporting only GLTF and HDR files
    namespace fs          = std::filesystem;
    std::string extension = fs::path(sfile).extension().string();
    if(extension == ".gltf" || extension == ".glb")
    {
      m_busyReasonText = "Loading scene ";

      // Loading scene and creating acceleration structure
      loadScene(sfile);

      // Loading the scene might have loaded new textures, which is changing the number of elements
      // in the DescriptorSetLayout. Therefore, the PipelineLayout will be out-of-date and need
      // to be re-created. If they are re-created, the pipeline also need to be re-created.
      for(auto& r : m_pRender)
        r->destroy();

      m_pRender[m_rndMethod]->create(
          m_size, {m_accelStruct.getDescLayout(), m_offscreen.getDescLayout(), m_scene.getDescLayout(), m_descSetLayout}, &m_scene);
    }

    if(extension == ".hdr")  //|| extension == ".exr")
    {
      m_busyReasonText = "Loading HDR ";
      loadEnvironmentHdr(sfile);
      updateHdrDescriptors();
    }


    // Re-starting the frame count to 0
    SampleExample::resetFrame();
    m_busy = false;
  }).detach();
}


//--------------------------------------------------------------------------------------------------
// Called at each frame to update the UBO: scene, camera, environment (sun&sky)
//
void SampleExample::updateUniformBuffer(const VkCommandBuffer& cmdBuf)
{
  if(m_busy)
    return;

  LABEL_SCOPE_VK(cmdBuf);
  const float aspectRatio = m_renderRegion.extent.width / static_cast<float>(m_renderRegion.extent.height);

  m_scene.updateCamera(cmdBuf, aspectRatio);
  if (m_scene.getDirty()) m_scene.updateLightBuffer(cmdBuf);
  vkCmdUpdateBuffer(cmdBuf, m_sunAndSkyBuffer.buffer, 0, sizeof(SunAndSky), &m_sunAndSky);
}

VkRect2D SampleExample::getRenderRegion()
{
    return m_renderRegion;
}

//--------------------------------------------------------------------------------------------------
// If the camera matrix has changed, resets the frame otherwise, increments frame.
//
void SampleExample::updateFrame()
{
  static glm::mat4 refCamMatrix;
  static float     fov = 0;

  auto& m = CameraManip.getMatrix();
  auto  f = CameraManip.getFov();
  if(refCamMatrix != m || f != fov)
  {
    resetFrame();
    refCamMatrix = m;
    fov          = f;
  }

  if (m_rtxState.frame < m_maxFrames)
  {
      m_rtxState.frame++;
	  m_rtxState.totalFrames++;
  }
  
  // update scene frame
  m_scene.setCurrFrame(m_rtxState.frame);
  m_scene.setSize(m_size);
}

//--------------------------------------------------------------------------------------------------
// Reset frame is re-starting the rendering
//
void SampleExample::resetFrame()
{
  m_rtxState.frame = -1;
}

//--------------------------------------------------------------------------------------------------
// Descriptors for the Sun&Sky buffer
//
void SampleExample::createDescriptorSetLayout()
{
  VkShaderStageFlags flags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR
                             | VK_SHADER_STAGE_ANY_HIT_BIT_KHR | VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;


  m_bind.addBinding({EnvBindings::eSunSky, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_MISS_BIT_KHR | flags});
  m_bind.addBinding({EnvBindings::eHdr, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, flags});  // HDR image
  m_bind.addBinding({EnvBindings::eImpSamples, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, flags});   // importance sampling


  m_descPool = m_bind.createPool(m_device, 1);
  CREATE_NAMED_VK(m_descSetLayout, m_bind.createLayout(m_device));
  CREATE_NAMED_VK(m_descSet, nvvk::allocateDescriptorSet(m_device, m_descPool, m_descSetLayout));

  // Using the environment
  std::vector<VkWriteDescriptorSet> writes;
  VkDescriptorBufferInfo            sunskyDesc{m_sunAndSkyBuffer.buffer, 0, VK_WHOLE_SIZE};
  VkDescriptorBufferInfo            accelImpSmpl{m_skydome.m_accelImpSmpl.buffer, 0, VK_WHOLE_SIZE};
  writes.emplace_back(m_bind.makeWrite(m_descSet, EnvBindings::eSunSky, &sunskyDesc));
  writes.emplace_back(m_bind.makeWrite(m_descSet, EnvBindings::eHdr, &m_skydome.m_texHdr.descriptor));
  writes.emplace_back(m_bind.makeWrite(m_descSet, EnvBindings::eImpSamples, &accelImpSmpl));

  vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

//--------------------------------------------------------------------------------------------------
// Setting the descriptor for the HDR and its acceleration structure
//
void SampleExample::updateHdrDescriptors()
{
  std::vector<VkWriteDescriptorSet> writes;
  VkDescriptorBufferInfo            accelImpSmpl{m_skydome.m_accelImpSmpl.buffer, 0, VK_WHOLE_SIZE};

  writes.emplace_back(m_bind.makeWrite(m_descSet, EnvBindings::eHdr, &m_skydome.m_texHdr.descriptor));
  writes.emplace_back(m_bind.makeWrite(m_descSet, EnvBindings::eImpSamples, &accelImpSmpl));
  vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

//--------------------------------------------------------------------------------------------------
// Creating the uniform buffer holding the Sun&Sky structure
// - Buffer is host visible and will be set each frame
//
void SampleExample::createUniformBuffer()
{
  m_sunAndSkyBuffer = m_alloc.createBuffer(sizeof(SunAndSky), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  NAME_VK(m_sunAndSkyBuffer.buffer);
}

void SampleExample::createSurfelResources()
{
    m_surfel.createResources(m_size);

    createGbufferPass();
    m_surfel.createGbuffers(m_size, m_swapChain.getImageCount(), m_gbufferPass.getRenderPass());

	m_surfelPreparePass.create({ m_surfel.maxSurfelCnt, 0 },
        { m_surfel.getSurfelBuffersDescLayout(), m_surfel.getCellBufferDescLayout()}, & m_scene);

	m_surfelGenerationPass.create(m_size,
        {   m_surfel.getSurfelBuffersDescLayout(),
            m_surfel.getGbufferSamplerDescLayout(),
            m_scene.getDescLayout(),
            m_surfel.getIndirectLightDescLayout(),
            m_surfel.getCellBufferDescLayout()}, & m_scene);

	m_surfelUpdatePass.create({ m_surfel.maxSurfelCnt, 0 }, {
        m_surfel.getSurfelBuffersDescLayout(),
        m_surfel.getCellBufferDescLayout(),
        m_scene.getDescLayout(),
        m_surfel.getGbufferSamplerDescLayout(),
        }, &m_scene);

    m_cellInfoUpdatePass.create({ m_surfel.maxSurfelCnt, 0 }, { 
        m_surfel.getSurfelBuffersDescLayout(),
        m_surfel.getCellBufferDescLayout()
        }, &m_scene);

    m_cellToSurfelUpdatePass.create({ m_surfel.maxSurfelCnt, 0 }, {
        m_surfel.getSurfelBuffersDescLayout(),
        m_surfel.getCellBufferDescLayout(),
        m_scene.getDescLayout()
        }, &m_scene);

    m_surfelRaytracePass.create({ m_surfel.maxRayBudget, 0 }, {
        m_accelStruct.getDescLayout(), m_offscreen.getDescLayout(), m_scene.getDescLayout(), m_descSetLayout,
		m_surfel.getSurfelBuffersDescLayout(), m_surfel.getSurfelDataMapsDescLayout(), m_surfel.getCellBufferDescLayout()
        }, &m_scene);

	m_surfelIntegratePass.create({ m_surfel.maxSurfelCnt, 0 }, {
		m_surfel.getSurfelBuffersDescLayout(),
        m_surfel.getSurfelDataMapsDescLayout(),
        m_surfel.getCellBufferDescLayout(),
        m_scene.getDescLayout(),
		}, & m_scene);

    createReflectionPass();
	createLightPass();

    m_indirectPostprocessPass.create(m_size, {
        m_scene.getDescLayout(),
        m_surfel.getGbufferSamplerDescLayout(),
        m_surfel.getIndirectLightDescLayout(),
        m_reflectionComputePass.getSamplerDescSetLayout()
        }, &m_scene);
	
}

void SampleExample::createGbufferPass()
{
    m_gbufferPass.create(m_size, { m_scene.getDescLayout() }, &m_scene);
}

void SampleExample::createLightPass()
{
	m_lightPass.createFrameBuffer(m_size, m_offscreen.getOffscreenColorFormat(), m_queues[eGCT1]);
	m_lightPass.create(m_size, { 
        m_accelStruct.getDescLayout(), 
        m_offscreen.getDescLayout(),
        m_scene.getDescLayout(), 
        m_descSetLayout,
        m_surfel.getGbufferImageDescLayout(), 
        m_surfel.getIndirectLightDescLayout(), 
        m_reflectionComputePass.getSamplerDescSetLayout(),
        m_offscreen.getSamplerDescSetLayout()}, & m_scene);
    m_lightPass.createLightPassDescriptorSet(m_offscreen.getDescLayout());
}

void SampleExample::createReflectionPass()
{
    m_reflectionComputePass.createReflectionPassDescriptorSet(m_size, m_queues[eGCT1]);

    m_reflectionComputePass.create(m_size, { 
        m_accelStruct.getDescLayout(), 
        m_offscreen.getDescLayout(), 
        m_scene.getDescLayout(), 
        m_descSetLayout,
        m_surfel.getGbufferImageDescLayout(), 
        m_reflectionComputePass.getSamplerDescSetLayout(),
        m_surfel.getSurfelBuffersDescLayout(),
        m_surfel.getCellBufferDescLayout()
        }, & m_scene);

    m_temporalSpatialPass.create(m_size, { 
        m_reflectionComputePass.getSamplerDescSetLayout() }, &m_scene);

	m_bilateralCleanupPass.create(m_size, {
		m_reflectionComputePass.getSamplerDescSetLayout(),
        m_surfel.getGbufferImageDescLayout(), }, &m_scene);

	//m_taaPass.createTAADescriptorSet(m_size, m_queues[eGCT1]);
	m_taaPass.create(m_size, {
		m_reflectionComputePass.getSamplerDescSetLayout(), 
        m_surfel.getGbufferImageDescLayout(),
        m_scene.getDescLayout(),
		m_offscreen.getSamplerDescSetLayout(),
		m_offscreen.getDescLayout()
        }, &m_scene);

    m_taaSharpenPass.create(m_size, {
        m_scene.getDescLayout(),
        m_offscreen.getSamplerDescSetLayout(),
        }, &m_scene);

	//m_offscreen.createPostTAADescriptorSet(m_taaPass.getImages());

}

//--------------------------------------------------------------------------------------------------
// Destroying all allocations
//
void SampleExample::destroyResources()
{
  // Resources
  m_alloc.destroy(m_sunAndSkyBuffer);

  // Descriptors
  vkDestroyDescriptorPool(m_device, m_descPool, nullptr);
  vkDestroyDescriptorSetLayout(m_device, m_descSetLayout, nullptr);

  // Other
  m_picker.destroy();
  m_scene.destroy();
  m_accelStruct.destroy();
  m_offscreen.destroy();
  m_skydome.destroy();
  m_axis.deinit();

  // All renderers
  for(auto p : m_pRender)
  {
    p->destroy();
    p = nullptr;
  }

  // Memory
  m_alloc.deinit();
}

//--------------------------------------------------------------------------------------------------
// Handling resize of the window
//
void SampleExample::onResize(int /*w*/, int /*h*/)
{
  m_offscreen.update(m_size);
  /*m_surfel.createGbuffers(m_size, m_swapChain.getImageCount(), m_gbufferPass.getRenderPass());
  m_surfel.createIndirectLightingMap(m_size);
  m_lightPass.createFrameBuffer(m_size, m_offscreen.getOffscreenColorFormat(), m_queues[eGCT1]);*/
  resetFrame();
}

//--------------------------------------------------------------------------------------------------
// Call the rendering of all graphical user interface
//
void SampleExample::renderGui(nvvk::ProfilerVK& profiler)
{
  m_gui->titleBar();
  m_gui->menuBar();
  m_gui->render(profiler);

  auto& IO = ImGui::GetIO();
  if(ImGui::IsMouseDoubleClicked(ImGuiDir_Left) && !ImGui::GetIO().WantCaptureKeyboard)
  {
    screenPicking();
  }
}


//--------------------------------------------------------------------------------------------------
// Creating the render: RTX, Ray Query, ...
// - Destroy the previous one.
void SampleExample::createRender(RndMethod method)
{
  if(method == m_rndMethod)
    return;

  LOGI("Switching renderer, from %d to %d \n", m_rndMethod, method);
  if(m_rndMethod != eNone)
  {
    vkDeviceWaitIdle(m_device);  // cannot destroy while in use
    m_pRender[m_rndMethod]->destroy();
  }
  m_rndMethod = method;
  m_raytraceLayoutPack = { m_accelStruct.getDescLayout(), m_offscreen.getDescLayout(), m_scene.getDescLayout(), m_descSetLayout };
  m_pRender[m_rndMethod]->create(
      m_size, m_raytraceLayoutPack, &m_scene);
}

//--------------------------------------------------------------------------------------------------
// The GUI is taking space and size of the rendering area is smaller than the viewport
// This is the space left in the center view.
void SampleExample::setRenderRegion(const VkRect2D& size)
{
  if(memcmp(&m_renderRegion, &size, sizeof(VkRect2D)) != 0)
    resetFrame();
  m_renderRegion = size;
}

//////////////////////////////////////////////////////////////////////////
// Post ray tracing
//////////////////////////////////////////////////////////////////////////

void SampleExample::createOffscreenRender()
{
  m_offscreen.create(m_size, m_renderPass);
  m_axis.init(m_device, m_renderPass, 0, 50.0f);
}

//--------------------------------------------------------------------------------------------------
// This will draw the result of the rendering and apply the tonemapper.
// If enabled, draw orientation axis in the lower left corner.
void SampleExample::drawPost(VkCommandBuffer cmdBuf)
{
  LABEL_SCOPE_VK(cmdBuf);
  auto size = glm::vec2(m_size.width, m_size.height);
  auto area = glm::vec2(m_renderRegion.extent.width, m_renderRegion.extent.height);

  VkViewport viewport{static_cast<float>(m_renderRegion.offset.x),
                      static_cast<float>(m_renderRegion.offset.y),
                      static_cast<float>(m_size.width),
                      static_cast<float>(m_size.height),
                      0.0f,
                      1.0f};
  VkRect2D   scissor{m_renderRegion.offset, {m_renderRegion.extent.width, m_renderRegion.extent.height}};
  vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
  vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

  m_offscreen.m_tonemapper.zoom           = m_descaling ? 1.0f / m_descalingLevel : 1.0f;
  m_offscreen.m_tonemapper.renderingRatio = size / area;
  m_offscreen.m_tonemapper.frame          = m_rtxState.frame;

  if (m_busy)
    m_offscreen.run(cmdBuf);
  else
      m_offscreen.run(cmdBuf, {
      m_lightPass.getDescriptorSet(),
	  m_offscreen.getSamplerDescSet(),
        });
      //m_offscreen.run(cmdBuf, m_taaPass.getSamplerDescSet());

  if(m_showAxis)
    m_axis.display(cmdBuf, CameraManip.getMatrix(), m_size);
}

void SampleExample::execPost(const VkCommandBuffer& cmdBuf, const VkExtent2D& size)
{
    std::array<VkClearValue, 2> clearValues;
    clearValues[0].color = { {0.0f, 0.0f, 0.0f, 0.0f} };
    clearValues[1].depthStencil = { 1.0f, 0 };

    VkRenderPassBeginInfo postRenderPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    postRenderPassBeginInfo.clearValueCount = 2;
    postRenderPassBeginInfo.pClearValues = clearValues.data();
    postRenderPassBeginInfo.renderPass = getRenderPass();
    postRenderPassBeginInfo.framebuffer = getFramebuffers()[getCurFrame()];
    postRenderPassBeginInfo.renderArea = { {}, getSize() };

    vkCmdBeginRenderPass(cmdBuf, &postRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    // Draw the rendering result + tonemapper
    drawPost(cmdBuf);

}
//////////////////////////////////////////////////////////////////////////
// Ray tracing
//////////////////////////////////////////////////////////////////////////

void SampleExample::renderScene(const VkCommandBuffer& cmdBuf, nvvk::ProfilerVK& profiler)
{
#if defined(NVP_SUPPORTS_NVML)
  g_nvml.refresh();
#endif

  if(m_busy)
  {
    m_gui->showBusyWindow();  // Busy while loading scene
    return;
  }

  LABEL_SCOPE_VK(cmdBuf);

  auto sec = profiler.timeRecurring("Render", cmdBuf);

  // We are done rendering
  if(m_rtxState.frame >= m_maxFrames)
    return;

  // Handling de-scaling by reducing the size to render
  VkExtent2D render_size = m_renderRegion.extent;
  if(m_descaling)
    render_size = VkExtent2D{render_size.width / m_descalingLevel, render_size.height / m_descalingLevel};

  m_rtxState.size = {render_size.width, render_size.height};
  // State is the push constant structure
  m_pRender[m_rndMethod]->setPushContants(m_rtxState);
  // Running the renderer
  m_pRender[m_rndMethod]->run(cmdBuf, render_size, profiler,
                              {m_accelStruct.getDescSet(), m_offscreen.getDescSet(), m_scene.getDescSet(), m_descSet});


  // For automatic brightness tonemapping
  if(m_offscreen.m_tonemapper.autoExposure)
  {
    auto slot = profiler.timeRecurring("Mipmap", cmdBuf);
    m_offscreen.genMipmap(cmdBuf);
  }
}

void insertMemoryBarriers(const VkCommandBuffer& cmdBuf, std::vector<VkBuffer> buffDependencies)
{
    // Inserting barriers
    if (buffDependencies.size() > 0)
    {
        std::vector<VkBufferMemoryBarrier> outbuffDependencies = {};
        for (int i = 0; i < buffDependencies.size(); i++)
        {
            VkBufferMemoryBarrier outbuffDependency = {};
            outbuffDependency = {};
            outbuffDependency.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            outbuffDependency.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            outbuffDependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            outbuffDependency.buffer = buffDependencies[i];
            outbuffDependency.size = VK_WHOLE_SIZE;
			outbuffDependencies.push_back(outbuffDependency);
        }

        vkCmdPipelineBarrier(
            cmdBuf,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            (VkDependencyFlags)0,
            0, nullptr,
            outbuffDependencies.size(), outbuffDependencies.data(),
            0, nullptr
        );
    }
}


void SampleExample::calculateSurfels(const VkCommandBuffer& cmdBuf, nvvk::ProfilerVK& profiler)
{
	if (m_busy)
	{
		m_gui->showBusyWindow();  // Busy while loading scene
		return;
	}

	LABEL_SCOPE_VK(cmdBuf);

    auto sec = profiler.timeRecurring("Surfel Calculate", cmdBuf);

    VkExtent2D render_size = m_renderRegion.extent;
    if (m_descaling)
        render_size = VkExtent2D{ render_size.width / m_descalingLevel, render_size.height / m_descalingLevel };

    m_rtxState.size = { render_size.width, render_size.height };

	m_surfelPreparePass.setPushContants(m_rtxState);
	m_surfelGenerationPass.setPushContants(m_rtxState);
	m_surfelUpdatePass.setPushContants(m_rtxState);
	m_surfelRaytracePass.setPushContants(m_rtxState);
	m_surfelIntegratePass.setPushContants(m_rtxState);
	m_indirectPostprocessPass.setPushContants(m_rtxState);


    VkBufferMemoryBarrier outbuffDependency = {};
    std::vector<VkBufferMemoryBarrier> outbuffDependencies = {};


    m_surfelPreparePass.run(cmdBuf, { m_surfel.totalCellCount, 1 }, profiler,
        { m_surfel.getSurfelBuffersDescSet(),
        m_surfel.getCellBufferDescSet()
        });

	insertMemoryBarriers(cmdBuf, { m_surfel.getCellInfoBuffer().buffer, m_surfel.getCellCounterBuffer().buffer });

	m_surfelUpdatePass.run(cmdBuf, { m_surfel.maxSurfelCnt, 1 }, profiler, { 
        m_surfel.getSurfelBuffersDescSet(),
		m_surfel.getCellBufferDescSet(),
        m_scene.getDescSet(),
        m_surfel.getGbufferSamplerDescSet()
        });

    insertMemoryBarriers(cmdBuf, { m_surfel.getCellInfoBuffer().buffer, m_surfel.getSurfelCounterBuffer().buffer });

    m_cellInfoUpdatePass.run(cmdBuf, { m_surfel.totalCellCount, 1 }, profiler, {
        m_surfel.getSurfelBuffersDescSet(),
        m_surfel.getCellBufferDescSet()
        });

	insertMemoryBarriers(cmdBuf, { m_surfel.getCellInfoBuffer().buffer, m_surfel.getCellCounterBuffer().buffer});

    m_cellToSurfelUpdatePass.run(cmdBuf, { m_surfel.maxSurfelCnt, 1 }, profiler, {
        m_surfel.getSurfelBuffersDescSet(),
        m_surfel.getCellBufferDescSet(),
        m_scene.getDescSet(),
        });

    insertMemoryBarriers(cmdBuf, { m_surfel.getCellInfoBuffer().buffer, m_surfel.getCellToSurfelBuffer().buffer });

	m_surfelRaytracePass.run(cmdBuf, { m_surfel.maxRayBudget, 1 }, profiler, {
		m_accelStruct.getDescSet(), m_offscreen.getDescSet(), m_scene.getDescSet(), m_descSet,
		m_surfel.getSurfelBuffersDescSet(), m_surfel.getSurfelDataMapsDescSet(), m_surfel.getCellBufferDescSet()
		});

    insertMemoryBarriers(cmdBuf, { m_surfel.getSurfelCounterBuffer().buffer, m_surfel.getSurfelRayBuffer().buffer, m_surfel.getSurfelBuffer().buffer });

	m_surfelIntegratePass.run(cmdBuf, { m_surfel.maxSurfelCnt, 1 }, profiler, {
		m_surfel.getSurfelBuffersDescSet(), m_surfel.getSurfelDataMapsDescSet(), m_surfel.getCellBufferDescSet(), m_scene.getDescSet()
		});

    insertMemoryBarriers(cmdBuf, { m_surfel.getCellInfoBuffer().buffer, m_surfel.getCellToSurfelBuffer().buffer,
        m_surfel.getSurfelBuffer().buffer, m_surfel.getSurfelCounterBuffer().buffer });

    m_surfelGenerationPass.run(cmdBuf, render_size, profiler, {
        m_surfel.getSurfelBuffersDescSet(),
        m_surfel.getGbufferSamplerDescSet(),
        m_scene.getDescSet(),
        m_surfel.getIndirectLightDescSet(),
        m_surfel.getCellBufferDescSet() });

	VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	VkImageMemoryBarrier imageMemoryBarrier = {};
	imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	imageMemoryBarrier.image = m_surfel.getIndirectLightingMap().image;
	imageMemoryBarrier.subresourceRange = subresourceRange;

    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);

	m_indirectPostprocessPass.run(cmdBuf, render_size, profiler, {
		m_scene.getDescSet(),
		m_surfel.getGbufferSamplerDescSet(),
		m_surfel.getIndirectLightDescSet(),
        m_reflectionComputePass.getSamplerDescSet()
		});


    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
    
}


//////////////////////////////////////////////////////////////////////////
// Keyboard / Drag and Drop
//////////////////////////////////////////////////////////////////////////

//--------------------------------------------------------------------------------------------------
// Overload keyboard hit
// - Home key: fit all, the camera will move to see the entire scene bounding box
// - Space: Trigger ray picking and set the interest point at the intersection
//          also return all information under the cursor
//
void SampleExample::onKeyboard(int key, int scancode, int action, int mods)
{
  nvvkhl::AppBaseVk::onKeyboard(key, scancode, action, mods);

  if(m_busy || action == GLFW_RELEASE)
    return;

  switch(key)
  {
    case GLFW_KEY_HOME:
    case GLFW_KEY_F:  // Set the camera as to see the model
      fitCamera(m_scene.getScene().m_dimensions.min, m_scene.getScene().m_dimensions.max, false);
      break;
    case GLFW_KEY_SPACE:
      screenPicking();
      break;
    case GLFW_KEY_R:
      resetFrame();
      break;
    default:
      break;
  }
}

//--------------------------------------------------------------------------------------------------
//
//
void SampleExample::screenPicking()
{
  double x, y;
  glfwGetCursorPos(m_window, &x, &y);

  // Set the camera as to see the model
  nvvk::CommandPool sc(m_device, m_graphicsQueueIndex);
  VkCommandBuffer   cmdBuf = sc.createCommandBuffer();

  const float aspectRatio = m_renderRegion.extent.width / static_cast<float>(m_renderRegion.extent.height);
  const auto& view        = CameraManip.getMatrix();
  auto        proj        = glm::perspectiveRH_ZO(glm::radians(CameraManip.getFov()), aspectRatio, 0.1f, 1000.0f);
  proj[1][1] *= -1;

  nvvk::RayPickerKHR::PickInfo pickInfo;
  pickInfo.pickX          = float(x - m_renderRegion.offset.x) / float(m_renderRegion.extent.width);
  pickInfo.pickY          = float(y - m_renderRegion.offset.y) / float(m_renderRegion.extent.height);
  pickInfo.modelViewInv   = glm::inverse(view);
  pickInfo.perspectiveInv = glm::inverse(proj);


  m_picker.run(cmdBuf, pickInfo);
  sc.submitAndWait(cmdBuf);

  nvvk::RayPickerKHR::PickResult pr = m_picker.getResult();

  if(pr.instanceID == ~0)
  {
    LOGI("Nothing Hit\n");
    return;
  }

  glm::vec3 worldPos = glm::vec3(pr.worldRayOrigin + pr.worldRayDirection * pr.hitT);
  // Set the interest position
  glm::vec3 eye, center, up;
  CameraManip.getLookat(eye, center, up);
  CameraManip.setLookat(eye, worldPos, up, false);


  auto& prim = m_scene.getScene().m_primMeshes[pr.instanceCustomIndex];
  LOGI("Hit(%d): %s\n", pr.instanceCustomIndex, prim.name.c_str());
  LOGI(" - PrimId(%d)\n", pr.primitiveID);
}

//--------------------------------------------------------------------------------------------------
//
//
void SampleExample::onFileDrop(const char* filename)
{
  if(m_busy)
    return;

  loadAssets(filename);
}

//--------------------------------------------------------------------------------------------------
// Window callback when the mouse move
// - Handling ImGui and a default camera
//
void SampleExample::onMouseMotion(int x, int y)
{
  AppBaseVk::onMouseMotion(x, y);
  if(m_busy)
    return;

  if(ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard)
    return;

  if(m_inputs.lmb || m_inputs.rmb || m_inputs.mmb)
  {
    m_descaling = true;
  }
}

//--------------------------------------------------------------------------------------------------
//
//
void SampleExample::onMouseButton(int button, int action, int mods)
{
  AppBaseVk::onMouseButton(button, action, mods);
  if(m_busy)
    return;

  if((m_inputs.lmb || m_inputs.rmb || m_inputs.mmb) == false && action == GLFW_RELEASE && m_descaling == true)
  {
    m_descaling = false;
    resetFrame();
  }
}

void SampleExample::computeReflection(const VkCommandBuffer& cmdBuf, nvvk::ProfilerVK& profiler)
{
    if (m_busy)
    {
        m_gui->showBusyWindow();  // Busy while loading scene
        return;
    }

    LABEL_SCOPE_VK(cmdBuf);

    auto sec = profiler.timeRecurring("Compute Reflection", cmdBuf);

    VkExtent2D render_size = m_renderRegion.extent;
    if (m_descaling)
        render_size = VkExtent2D{ render_size.width / m_descalingLevel, render_size.height / m_descalingLevel };

    std::vector<nvvk::Texture> textures = m_reflectionComputePass.getColorDirectionTextures();
    std::vector< VkImageMemoryBarrier> barriers = {};
    VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    for (int i = 0; i < textures.size(); i++)
    {
        VkImageMemoryBarrier imageMemoryBarrier = {};
        imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL; // change here?
        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL; // change here?
        imageMemoryBarrier.srcQueueFamilyIndex = m_queues[eGCT0].familyIndex;
        imageMemoryBarrier.dstQueueFamilyIndex = m_queues[eGCT0].familyIndex;
        imageMemoryBarrier.image = textures[i].image;
        imageMemoryBarrier.subresourceRange = subresourceRange;
        barriers.push_back(imageMemoryBarrier);
    }

    m_rtxState.size = { render_size.width, render_size.height };

	m_reflectionComputePass.setPushContants(m_rtxState);
	m_temporalSpatialPass.setPushContants(m_rtxState);
	m_bilateralCleanupPass.setPushContants(m_rtxState);
	m_taaPass.setPushContants(m_rtxState);

    m_reflectionComputePass.run(cmdBuf, render_size, profiler, { 
        m_accelStruct.getDescSet(), 
        m_offscreen.getDescSet(), 
        m_scene.getDescSet(), 
        m_descSet,
        m_surfel.getGbufferImageDescSet(),
        m_reflectionComputePass.getSamplerDescSet(),
        m_surfel.getSurfelBuffersDescSet(),
        m_surfel.getCellBufferDescSet()
        });

    vkCmdPipelineBarrier(cmdBuf,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr,
        static_cast<uint32_t>(barriers.size()),
        barriers.data());

    m_temporalSpatialPass.run(cmdBuf, render_size, profiler, {
        m_reflectionComputePass.getSamplerDescSet()
        });
	
    vkCmdPipelineBarrier(cmdBuf,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr,
        static_cast<uint32_t>(barriers.size()),
        barriers.data());

    m_bilateralCleanupPass.run(cmdBuf, render_size, profiler, {
        m_reflectionComputePass.getSamplerDescSet(),
        m_surfel.getGbufferImageDescSet(),
        });

  //  m_taaPass.run(cmdBuf, render_size, profiler, {
  //      m_reflectionComputePass.getSamplerDescSet(),
  //      m_surfel.getGbufferImageDescSet(),
  //      m_scene.getDescSet(),
		//m_taaPass.getSamplerDescSet()
  //      });

    //vkCmdPipelineBarrier(cmdBuf,
    //    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    //    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
    //    0, 0, nullptr, 0, nullptr,
    //    static_cast<uint32_t>(barriers.size()),
    //    barriers.data());
}

vec2 Hammersley(float i, float numSamples)
{
    uint b = uint(i);

    b = (b << 16u) | (b >> 16u);
    b = ((b & 0x55555555u) << 1u) | ((b & 0xAAAAAAAAu) >> 1u);
    b = ((b & 0x33333333u) << 2u) | ((b & 0xCCCCCCCCu) >> 2u);
    b = ((b & 0x0F0F0F0Fu) << 4u) | ((b & 0xF0F0F0F0u) >> 4u);
    b = ((b & 0x00FF00FFu) << 8u) | ((b & 0xFF00FF00u) >> 8u);

    float radicalInverseVDC = float(b) * 2.3283064365386963e-10;

    return vec2((i / numSamples), radicalInverseVDC);
}

#include <random>
std::vector<vec2> SampleExample::hammersleySequence(int maxNumberPoints)
{
	std::vector<vec2> points;
	for (int i = 0; i < maxNumberPoints; i++)
	{
		points.push_back(Hammersley(i, maxNumberPoints));
	}

    std::random_device rd;
    // 4257880815, 734147533
	auto seed = rd();
    //std::cout << seed << std::endl;
    std::mt19937 g(4257880815);
    std::shuffle(points.begin(), points.end(), g);

    /*std::vector<vec2> points{
    {0.500000f, 0.333333f},
    {0.250000f, 0.666667f},
    {0.750000f, 0.111111f},
    {0.125000f, 0.444444f},
    {0.625000f, 0.777778f},
    {0.375000f, 0.222222f},
    {0.875000f, 0.555556f},
    {0.062500f, 0.888889f},
    {0.562500f, 0.037037f},
    {0.312500f, 0.370370f},
    {0.812500f, 0.703704f},
    {0.187500f, 0.148148f},
    {0.687500f, 0.481481f},
    {0.437500f, 0.814815f},
    {0.937500f, 0.259259f},
    {0.031250f, 0.592593f}
    };*/

	return points;
}

void SampleExample::initHammerleySequence(int maxNumberPoints)
{
	m_scene.setHammersleySequence(hammersleySequence(maxNumberPoints));
}

void SampleExample::runTAA(const VkCommandBuffer& cmdBuf, nvvk::ProfilerVK& profiler)
{
    if (m_busy)
    {
        m_gui->showBusyWindow();  // Busy while loading scene
        return;
    }

    LABEL_SCOPE_VK(cmdBuf);

    auto sec = profiler.timeRecurring("Compute Reflection", cmdBuf);

    VkExtent2D render_size = m_renderRegion.extent;
    if (m_descaling)
        render_size = VkExtent2D{ render_size.width / m_descalingLevel, render_size.height / m_descalingLevel };

    VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkImageMemoryBarrier offscreenMemoryBarrier = {};
    offscreenMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    offscreenMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    offscreenMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    offscreenMemoryBarrier.image = m_lightPass.getTexture().image;
    offscreenMemoryBarrier.subresourceRange = subresourceRange;

    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &offscreenMemoryBarrier);
    m_rtxState.size = { render_size.width, render_size.height };
    m_taaPass.setPushContants(m_rtxState);

    m_taaPass.run(cmdBuf, render_size, profiler, {
        m_reflectionComputePass.getSamplerDescSet(),
        m_surfel.getGbufferImageDescSet(),
        m_scene.getDescSet(),
        m_offscreen.getSamplerDescSet(),
        m_lightPass.getDescriptorSet(),
        });

    std::vector<VkImageMemoryBarrier> imageMemoryBarriers;
    for (auto image : m_offscreen.getImages())
    {
        VkImageMemoryBarrier imageMemoryBarrier = {};
        imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        imageMemoryBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageMemoryBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        imageMemoryBarrier.image = image.image;
        imageMemoryBarrier.subresourceRange = subresourceRange;
        imageMemoryBarriers.push_back(imageMemoryBarrier);
    }

    vkCmdPipelineBarrier(cmdBuf,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr,
        static_cast<uint32_t>(imageMemoryBarriers.size()),
        imageMemoryBarriers.data());

    m_taaSharpenPass.setPushContants(m_rtxState);
    m_taaSharpenPass.run(cmdBuf, render_size, profiler, {
        m_scene.getDescSet(),
        m_offscreen.getSamplerDescSet(),
        });


    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, imageMemoryBarriers.size(), imageMemoryBarriers.data());

    
}