# SurfelPlus Project Page

![DemoImage.png](/img/DemoImage.png)
![DemoImage.png](/img/logo.png)

# Introduction

**SurfelPlus** is a real-time dynamic global illumination renderer built on top of NVIDIA's vk_raytrace framework. Using **Vulkan ray tracing** and **surfel-based** techniques, SurfelPlus aims to deliver high-quality lighting effects with dynamic scene updates, providing an efficient solution for realistic and immersive visual rendering in real time.
[Click here for the project pitch PDF.](https://github.com/WANG-Ruipeng/SurfelPlus/blob/master/docs/files/ProjectPitch.pdf)

# Technical Overview

**SurfelPlus** utilizes surfel-based techniques to achieve high-quality and accurate lighting effects. This approach builds on the surfel GI method presented by EA SEED in their [SIGGRAPH 2021 talk](https://www.ea.com/seed/news/siggraph21-global-illumination-surfels) and [SIGGRAPH 2024 talk](https://advances.realtimerendering.com/s2024/content/EA-GIBS2/Apers_Advances-s2024_Shipping-Dynamic-GI.pdf). Originally designed for the Frostbite engine, these techniques have been carefully adapted and optimized for our Vulkan-based renderer, taking advantage of Vulkan's flexibility and ray tracing capabilities to deliver real-time global illumination in dynamic scenes.

### What are Surfels?

Surfels, short for "surface elements," are point-like primitives that represent the surface of a 3D object. Each surfel stores key information, including position, normal, color, and other material properties, enabling efficient computation of light interactions in a scene.

![image.png](/img/surfel.png)

In global illumination, surfels serve as intermediaries for light propagation and reflection, balancing accuracy and computational cost.

### Key Features and Techniques

1.  **Surfel-Based Indirect Lighting**

    Indirect lighting is computed by storing and sharing light energy across surfels. This approach captures diffuse inter-reflections and supports dynamic scenes without requiring precomputed data, making it well-suited for interactive applications.

    ![image.png](/img/surfelIndirect.png)

2.  **Lighting Integration**

    SurfelPlus leverages Vulkan's capabilities for direct lighting, while surfels handle the more performance-intensive indirect light calculations. This hybrid approach optimizes rendering performance without sacrificing visual quality.

3.  **Global Illumination Pipeline**
    -   **Surfel Preparation**: Surfels are generated or updated based on the surfel coverage of the screen. Each surfel's properties (e.g., normal, color, and reflectance) are computed to match the underlying surface.
    -   **Surfel Visibility and Culling**: Surfels outside the camera's view frustum or occluded by other geometry are efficiently culled to reduce computational overhead.
    -   **Lighting Calculation**: Light bounces are simulated using surfel-to-surfel interactions, enabling indirect light accumulation in the scene.
4.  **Grid-based surfel acceleration structure**
    A grid-based surfel acceleration structure organizes surfels into spatial cells for fast neighbor queries and updates. This method enables efficient surfel-to-surfel interactions and scalable indirect lighting computations.
    ![image.png](/img/Grid-Based.png)

### Advantages of Surfel-Based GI

-   **Real-Time Performance**: Unlike traditional methods that rely on precomputed lightmaps or heavy ray tracing, surfel GI enables dynamic updates at real-time frame rates.
-   **Dynamic Scene Support**: Lighting changes are immediately reflected, making SurfelPlus ideal for interactive environments and games.
-   **Physically-Based Lighting**: Surfels inherently store surface and material properties, allow our rendering pipeline seamlessly to integrate into original PBR methods.

## Surfel GI Render Passes Overview

### Overview

-   Prepare Stage
    -   Gbuffer Pass
-   Surfel Calculation Stage
    -   Surfel Prepare Pass
    -   Surfel Update Pass
    -   Cell Info Update Pass
    -   Cell to Surfel Update Pass
    -   Surfel Ray Trace Pass
    -   Surfel Generation & Evaluation Pass
-   Reflection Calculation Stage
    -   Reflection Trace Pass
    -   Spatial Temporal Filtering Pass
    -   Bilateral Filtering Pass
-   Integrate Stage
    -   SSAO Pass
    -   Light Integrate Pass
    -   TAA Pass
    -   Tone Mapping Pass

![image.png](/img/HeaderImage.png)

### G-Buffer Pass

The **G-Buffer Pass** is responsible for capturing per-pixel information about the scene's geometry and surface properties, which are later used in lighting and shading computations. This pass encodes data such as primitive object ID, compressed world-space normals, and other attributes necessary for the rendering pipeline.

| Visibility Buffer                 | Depth Buffer                       |
| --------------------------------- | ---------------------------------- |
| ![image.png](/img/visibility.jpg) | ![image.png](/img/DepthBuffer.png) |

### Surfel Prepare Pass

A pass that reset some counters and prepares all the buffer for later accumulation.

### Surfel Update Pass

The Surfel Update Pass is responsible for maintaining and updating the dynamic surfel data in real time. It processes active surfels to:

-   Recycle expired or invalid surfels based on specific criteria, such as lifespan, distance from the camera, total surfel count, or visibility status.

-   Adjust surfel radius dynamically based on camera distance and scene conditions to balance performance and visual quality. The cell radius is bounded by cell size to keep surfels right in their cells.

-   Distribute surfels into appropriate grid cells for efficient spatial queries and interactions. Surfels would check the surrounding 3x3 cells so that surfels crossing multiple cells can be correctly recorded in each cell.

-   Allocate ray resources for surfel-based global illumination calculations, ensuring adequate sampling for indirect lighting. The ray allocation is influenced by surfel variance, surfel life and surfel visibility.

This pass ensures the surfel system remains efficient and responsive to scene changes, supporting real-time dynamic global illumination with consistent performance. However, surfel recycle may dispose some surfels in unseen area, causing the GI to re-converge when camera looks at them.

### Cell Info Update Pass

Pre-allocate the offset and range of cell to surfel buffer.

### Cell to Surfel Update Pass

Populate data in cell to surfel buffer.

### Surfel Ray Trace Pass

The Surfel Ray Tracing Pass is responsible for casting rays from surfels to compute their radiance, which is essential for indirect lighting in the scene. This pass integrates ray-guiding sampling and cosine-weighted hemisphere sampling to ensure accurate and efficient light transport calculations.

-   Ray Generation: Uses either ray-guided sampling (based on irradiance maps) or cosine-weighted sampling (based on the surfel’s orientation) to determine the direction of rays. The ray-guided sampling prioritizes high-irradiance directions for better accuracy, while cosine-weighted sampling provides fallback sampling when irradiance data is insufficient. The surfel radiance in each direction is written to a irradiance map which is a 4K texture atlas. Each surfel has a 6x6 quad to store their radiance in each direction.

![](/img/irradianceMap.png)

-   Radiance Calculation: Casts rays from surfel positions into the scene using our custom path tracing method. When rays hit an emissive surface or sky, it would return the corresponding radiance. If not, we would try to finalize the path using existing surfels.

When a ray reach its maximum depth but receiving not lights, we can use the surrounding surfels to provide diffuse indirect lighting for that ray. The helps the scene to converge faster. However, if there are many surfels nearby, sampling all of them would cause a huge performance deration. Therefore, a stochastic sampling strategy would be used to limit the number of surfel samples.

-   Surfel Ray Updates: Updates each surfel ray with its computed radiance, direction, and probability density function for use in subsequent lighting computations. Here, we also clamps high luminance values to improve stability and avoid firefly effects in the scene.

-   Dynamic Adjustments: Adapts ray tracing depth based on the surfel’s activeness to optimize performance without sacrificing realism.

-   This pass is critical for accurately simulating light propagation in the scene, contributing to the high-quality global illumination achieved by SurfelPlus.

### Surfel Integrate Pass

The Surfel Integration Pass is responsible for aggregating the radiance contributions collected by surfel rays, updating surfel properties, and sharing irradiance data among nearby surfels. This pass plays a crucial role in achieving consistent and smooth global illumination.

-   Radiance Aggregation: Gathers and accumulates radiance data from surfel rays stored in the memory. Each surfel would have maximum 64 rays in each frame. These rays are divided to 4 packs and each pack would contribute to the surfel irradiance using MSME. MSME algorithm would also calculate the surfel variance and accumulation weight so that surfels can be more responsive to environment change and converge in a smoother way.

-   Irradiance Map Update: Updates the irradiance map for each surfel, storing directional irradiance information in a 6x6 grid for efficient reuse in guided sampling. We also writes depth information into a corresponding depth map to assist with visibility checks.
    ![](/img/surfelIrradiance.png)
    _Radiance in each surfel_
    
-   Shared Radiance Contribution: Enables nearby surfels to share irradiance data within a local spatial cell. This step accelerate surfel convergence a lot. Uses factors like normal alignment, distance, and surfel lifespan to weight contributions, ensuring consistent and plausible lighting. Stochastic sampling strategy is also applied here.

-   Adaptive Integration: Differentiates behavior for newly created surfels and established ones to avoid sudden changes in irradiance values. Then, we normalizes contributions across samples, ensuring accurate energy conservation.

This pass ensures that surfels maintain smooth and stable lighting across frames while leveraging shared data to enhance global illumination accuracy and performance.

### Surfel Generation & Evaluation Pass

The Surfel Generation Pass computes indirect lighting contributions for each pixel based on nearby surfels, updates shading information, and dynamically generates new surfels to ensure adequate coverage in the scene. This pass evaluates the lighting of the rendered image by incorporating smooth and detailed global illumination.

-   Indirect Lighting Calculation: Aggregates irradiance contributions from nearby surfels within the same spatial cell. Factors like distance, normal alignment, and surfel radius are used to weight contributions, ensuring realistic lighting effects.

![](/img/indirectLight.png)

-   Coverage and Contribution Analysis: Evaluates coverage and contribution metrics of each pixel determine lighting consistency and detect potential gaps in surfel representation.

-   Dynamic Surfel Generation: Generates new surfels dynamically in underrepresented regions to maintain proper lighting coverage. The position, radius, and radiance of new surfels are initialized based on the scene’s current lighting conditions.
    ![](/img/surfelization1.png)

-   Debugging and Visualization: Here, we provide supports multiple debugging modes, including visualizing radiance, surfel IDs, variance, and surfel radius. This provides insights into surfel contributions and the overall quality of indirect lighting.

-   Adaptive Surfel Removal: Removes surfels in regions with excessive coverage to optimize surfel layout memory usage and computational performance.

This pass evaluates the indirect diffuse lighting using nearby surfels, while dynamically adapting the surfel distribution to maintain high-quality and uniform global illumination in real-time scenarios.

![image.png](/img/surfelVisualization.png)

_Each color cell represents one surfel_

### Reflection Trace Pass

The Reflection Compute Pass computes specular reflections using RIS. It calculates accurate reflection contributions while efficiently handling complex material properties and varying surface roughness.

RIS: Implements a weighted reservoir sampling approach to select the best reflection candidate based on target contribution weights.

Reflection Computation: Traces reflection rays to gather radiance from the scene using both raw trace and surfel-based indirect lighting. The idea is similar to surfel ray trace. This approach accelerate reflection convergence surprisingly.

| Without surfel indirect, max bounce = 6 | With surfel indirect, max bounce = 1 |
| :-------------------------------------: | :----------------------------------: |
|    ![](/img/reflectionWOsurfel.png)     |   ![](/img/reflectionWsurfel.png)    |

From the above image, you can see the huge difference achieved by surfel indirect lighting even with only 1 bounce.

Firefly Suppression: Includes a threshold-based luminance clamp to prevent outliers (fireflies) in the reflection output, ensuring stable and realistic visuals.

This pass provides high-quality specular reflections, essential for realistic rendering of glossy and metallic surfaces, while maintaining efficiency through RIS and surfel indirect lighting techniques.

### Spatial Temporal Filtering Pass

The Spatial Temporal Filtering Pass refines the specular reflection data by applying spatial reconstruction and temporal accumulation techniques. This pass ensures smooth and visually accurate reflections while mitigating noise and artifacts.

![image.png](/img/spatial_temporal.png)

_Reflection lighting in the scene_

-   Spatial Reconstruction Filtering: Uses a neighborhood sampling pattern to collect and average reflection data from nearby pixels. Incorporates sample weighting based on BRDF contributions, material consistency, and PDF values to prioritize relevant samples.

-   Material Consistency Check: Ensures that only samples with matching material IDs contribute to the reflection data, preserving material-specific characteristics.

-   Variance Calculation: Computes the variance of sampled contributions to measure the reliability and stability of the filtered reflections.

-   Temporal Accumulation: Combines current filtered results with the previous frame’s reflections to enhance temporal stability and reduce flickering.

![](/img/stfilter.png)

-   Adaptive Sampling Patterns: Leverages stochastic sampling with predefined patterns (Blue noise disk in our code) to balance performance and quality across different pixel regions. Since the raw tracing result is in a half resolution texture, each half res pixel corresponds to a 2x2 quad in spatial filtering. Therefore, we apply different sampling pattern by threshold a blue noise texture into 4 parts.

![](/img/samplePattern.png)

### Bilateral Filtering Pass

The Bilateral Cleanup Pass refines the filtered reflection data by applying a bilateral filter that considers spatial, color, and normal similarity. This pass enhances the smoothness of reflections while preserving sharp edges and important details.

![image.png](/img/bilateral.png)

_Filtered reflection image for denoising_

-   Variance-Based Filtering: Handles pixels differently based on their variance:
-   Culled Pixels: Variance below a threshold results in the pixel being discarded.
-   Low-Variance Pixels: Retains the original reflection data.
-   High-Variance Pixels: Applies bilateral filtering to smooth the data.
-   Bilateral Filtering:Combines three weights to refine the reflection data:
-   Spatial Weight: Penalizes contributions from farther neighbors.
-   Range Weight: Reduces the impact of neighbors with significantly different colors.
-   Normal Weight: Incorporates geometric similarity by considering surface normal alignment.
-   Dynamic Kernel Radius: Adapts the filter kernel radius based on pixel variance, allowing for more aggressive smoothing in high-variance areas while maintaining sharpness elsewhere.
-   Edge Preservation: Ensures that boundaries and fine details are preserved by factoring in normal similarity and color differences.

This pass ensures high-quality reflection visuals by reducing noise and artifacts while maintaining important surface details and transitions, providing a polished final image.

![](/img/bilateral1.png)

### SSAO Pass

A ssao pass to add more realism to the scene. Use temporal accumulation to do denoising.

![](/img/SSAO.png)

### Light Integrate Pass

This pass calculates **direct lighting** and integrats it with **indirect and reflection** information that gathered from previous passes.  
Information needed (material, world position, etc.) for Direct lighting was obtained and uncompressed/reconstructed from G-Buffer, then ray-query features was used to compute shading accordingly.

|            Direct Lighting             |            Indirect Lighting             |             Reflection             |
| :------------------------------------: | :--------------------------------------: | :--------------------------------: |
| ![](/img/lightPass/directlighting.png) | ![](/img/lightPass/indirectLighting.png) | ![](/img/lightPass/reflection.png) |

### TAA Pass

The TAA pass **jitters** the view frustum and strategically **averges** the color between multiple frames.

Position Reconstruction: Reconstruct world position using depth buffer and screen uv.

Previous Frame Reprojection: Using the view-projection matrix of last frame to calculate uv of world position of current pixel in last frame.

|          Reprojection          |
| :----------------------------: |
| ![](/img/TAA/reprojection.png) |

Neighbor Color Vector AABB: Sample the 3x3 neighbor color and adjacent neighbor color (surrounding pixels in "+" pattern), calculate aabb of color vector

|           AABB           |
| :----------------------: |
| ![](/img/TAA/33plus.png) |

Neighbor Color Clipping: clip the current color towards history color instead of just clamping it. In this way color from previous frame is trivially accepted to reduce ghost and smearing effect.

|    Clamping and Clipping    |
| :-------------------------: |
| ![](/img/TAA/clampclip.png) |

Blend and weigh history frames: Lerp between colors of past frame and this frame. Higher feedback factor will have a faster converge but will introduce artifacts.

|          Blend          |
| :---------------------: |
| ![](/img/TAA/blend.png) |

# Milestones Development Log

## Milestone 1

[Click here for the original PDF.](https://github.com/WANG-Ruipeng/SurfelPlus/blob/master/docs/files/SurfelPlusMilestone1.pdf)

For our first milestone, we made significant progress in setting up the foundation of our renderer:

### Key Achievements

-   Conducted multiple meetings and discussions to:
    -   Define all necessary render passes.
    -   Explore possible implementation details.
-   Familiarized ourselves with the codebase and Vulkan framework, including features like ray queries and resource management.
-   Created initial implementations of basic render passes, some of which are naive or empty placeholders.
-   Added a functional G-buffer pass, capturing essential attributes like visibility, normals, and depth.
-   Began working on a naive surfelization approach to integrate surfels into the pipeline.

This milestone established a solid groundwork for our surfel-based GI system.

### Render Passes Implemented

-   **G-buffer Pass**: Captures visibility, normal, and depth information.
-   **Surfel Prepare Compute Pass**: Prepares data for surfel-based lighting calculations.
-   **Surfel Generation Compute Pass**: Generates surfels from the scene geometry.
-   **Surfel Update Compute Pass**: Updates surfel data dynamically.
-   **Direct Lighting Pass**: Computes direct lighting effects.
-   **Post-Processing Pass**: Applies post-processing effects to the final image.

![image.png](/img/M1Passes.png)

### Demo

| Surfel Generation              | Visibility Buffer              | Normal Buffer                  |
| ------------------------------ | ------------------------------ | ------------------------------ |
| ![image.png](/img/M1Demo1.png) | ![image.png](/img/M1Demo2.png) | ![image.png](/img/M1Demo3.png) |

### Milestone 2 Goals

For the second milestone, our focus is on improving the surfel system and integrating advanced features:

-   **Enhanced Surfelization**: Refine the process of generating and managing surfels for better accuracy and performance.
-   **Surfel Recycling**: Implement a recycling mechanism to reuse surfels efficiently, reducing memory overhead.
-   **Surfel Acceleration Structure**: Develop a grid-based acceleration structure to enable fast surfel queries and interactions.
-   **Surfel Ray Generation & Ray Tracing (If Possible)**: Explore the integration of surfel-based ray tracing for more accurate light propagation and visibility calculations.

These improvements aim to advance the overall efficiency and realism of our renderer.

## Milestone 2

[Click here for the original PDF.](https://github.com/WANG-Ruipeng/SurfelPlus/blob/master/docs/files/SurfelPlusMilestone2.pdf)

For Milestone 2, we achieved significant advancements in our surfel-based GI system:

### Completed Goals

-   **Better Surfelization**: Improved surfel generation for greater accuracy and efficiency.
-   **Surfel Recycling**: Implemented a mechanism to reuse surfels, reducing memory usage and maintaining dynamic scene updates.
-   **Surfel Acceleration Structure**: Developed a naive uniform grid-based acceleration structure to facilitate fast surfel queries and interactions.

### Partially Completed Goal

-   **Surfel Ray Generation & Ray Tracing**: Initiated work on integrating ray generation and tracing using surfels, with partial implementation completed.

### New Passes Implemented:

-   **Surfel Compute Passes**:
    -   Surfel Generation
    -   Surfel Trace
    -   Surfel Update/Recycle
-   **Integration pass for collecting all surfel information**

![image.png](/img/M2Passes.png)

### Demo

| Diffuse                        | Surfel indirect                |
| ------------------------------ | ------------------------------ |
| ![image.png](/img/M2Demo1.png) | ![image.png](/img/M2Demo2.png) |

### Milestone 3 & Final Goals

Looking ahead to Milestone 3 and the final stage of the project, our goals include:

-   **Glossy Indirect Lighting**: Add support for glossy reflections in the indirect lighting pipeline.
-   **Spatial-Temporal Filtering**: Improve temporal stability and spatial consistency for better rendering quality.
-   **Better Surfelization & Tracing**: Refine surfel generation and tracing algorithms for enhanced performance and accuracy.
-   **Non-Uniform Acceleration Structure**: Transition from a uniform grid to a non-uniform structure for improved scalability and efficiency.
-   **Demo Scenes**: Identify and prepare demonstration scenes to showcase the renderer's capabilities.

## Milestone 3

[Click here for the original PDF.](https://github.com/WANG-Ruipeng/SurfelPlus/blob/master/docs/files/SurfelPlusMilestone3.pdf)

For Milestone 3, we addressed key issues identified in Milestone 2 and implemented significant improvements:

### Issues from Milestone 2:

-   **Low Resolution & Poor Performance**: The previous implementation suffered from low resolution and suboptimal frame rates.
-   **Low-Frequency Noise**: Visible artifacts in the lighting pipeline reduced visual quality.

### Milestone 3 Improvements:

-   **Better Surfelization & Tracing**: Enhanced surfel generation and tracing algorithms to improve accuracy and reduce artifacts.
-   **Higher Resolution & Improved Performance**: Increased rendering resolution from **1080x720** to **2560x1440** while keeping over 120 FPS for smoother performance.
-   **Non-Uniform Acceleration Structure**: Replaced the uniform grid with a non-uniform structure for more efficient surfel queries and better scalability. We have observe a 15% fps increase in out [test stadium scene](https://sketchfab.com/3d-models/al-wakrah-stadium-worldcup-2022-d7452e247d55493e8e6a9c086a4eafe6).
-   **Improved Stability**:
    -   Render pipeline synchronization to prevent flickering and inconsistencies.
    -   G-buffer depth bias and ray offset techniques to mitigate precision issues.
    -   More uniform surfel distribution to ensure consistent lighting quality.

### New Features:

-   Added **new demo scenes** to better showcase the renderer's capabilities in diverse lighting and scene configurations.

### Demo

| Before Milestone 3             | After Milestone 3              |
| ------------------------------ | ------------------------------ |
| ![image.png](/img/M3Demo1.png) | ![image.png](/img/M3Demo2.png) |

### Goals for Milestone 3 & Final:

-   **Glossy Indirect Lighting**: Adding support for glossy reflections (currently in progress).
-   **Spatial-Temporal Filtering**: Implementing filtering techniques to improve temporal stability and spatial quality (currently in progress).
-   **Better Surfelization & Tracing**: Completed with significant enhancements.
-   **Non-Uniform Acceleration Structure**: Successfully implemented.

These improvements position **SurfelPlus** as a highly efficient and visually robust renderer capable of dynamic global illumination with high performance and quality.

## Final

### Completed Goals:

1. **Glossy Indirect Lighting**:
    - Fully implemented support for glossy reflections, enhancing visual realism by simulating accurate light bounces on glossy surfaces. This feature significantly improves the fidelity of materials like polished metals and glass.
2. **Spatial-Temporal Filtering for Glossy Reflections**:
    - Successfully integrated stochastically spatial-temporal filtering techniques for glossy reflections. This approach improves both temporal stability and spatial consistency, reducing flickering and noise in scenes.

### Additional Features Implemented:

1. **Bilateral Filtering**:
    - Added a bilateral filtering stage after spatial-temporal filtering to further smooth indirect lighting. This step preserves edge details while eliminating noise, resulting in cleaner and more visually appealing outputs.
2. **Temporal Anti-Aliasing (TAA)**:
    - Implemented TAA to address aliasing artifacts, particularly in high-contrast areas of the scene.
    - The current implementation focuses on **motion vectors derived from camera movement**, which provide smoother visuals during dynamic camera interactions.
    - Due to time constraints, we did not include **ray hit re-projection**, but the groundwork has been laid for future extensions.
3. **Screen-Space Ambient Occlusion (SSAO)**:
    - Integrated SSAO into the rendering pipeline to enhance the perception of depth and contact shadows. This feature improves the overall realism by adding subtle occlusion effects in areas where light is naturally obstructed.
      ![image.png](/img/SSAO.png)

These enhancements collectively elevate the visual quality and performance of **SurfelPlus**, making it a robust and versatile renderer for real-time global illumination in dynamic environments.

### TAA Pass

The TAA pass **jitters** the view frustum and strategically **averges** the color between multiple frames.

Position Reconstruction: Reconstruct world position using depth buffer and screen uv.

Previous Frame Reprojection: Using the view-projection matrix of last frame to calculate uv of world position of current pixel in last frame.

|            Reprojection             |
| :---------------------------------: |
| ![](/docs/img/TAA/reprojection.png) |

Neighbor Color Vector AABB: Sample the 3x3 neighbor color and adjacent neighbor color (surrounding pixels in "+" pattern), calculate aabb of color vector

|             AABB              |
| :---------------------------: |
| ![](/docs/img/TAA/33plus.png) |

Neighbor Color Clipping: clip the current color towards history color instead of just clamping it. In this way color from previous frame is trivially accepted to reduce ghost and smearing effect.

|      Clamping and Clipping       |
| :------------------------------: |
| ![](/docs/img/TAA/clampclip.png) |

Blend and weigh history frames: Lerp between colors of past frame and this frame. Higher feedback factor will have a faster converge but will introduce artifacts.

|            Blend             |
| :--------------------------: |
| ![](/img/TAA/blend.png) |

Sharpen: Filter the final image with Laplace operator

|            No TAA            |           TAA (Unsharpened)           |           TAA (Sharpened)           |
| :--------------------------: | :-----------------------------------: | :---------------------------------: |
| ![](/img/TAA/NOTAA.png) | ![](/img/TAA/TAAUnsharpened.png) | ![](/img/TAA/TAASharpened.png) |

### Demo

| Large open scene                  | Closed scene                  |
| --------------------------------- | ----------------------------- |
| ![image.png](/img/FLargeOpen.png) | ![image.png](/img/FClose.png) |

| With Glossy Indirect Lighting   | Without Glossy Indirect Lighting  |
| ------------------------------- | --------------------------------- |
| ![image.png](/img/FDiffuse.png) | ![image.png](/img/FNoDiffuse.png) |

# References

[GIBS: SIGGRAPH 2021 talk](https://www.ea.com/seed/news/siggraph21-global-illumination-surfels)  
[GIBS: SIGGRAPH 2024 talk](https://advances.realtimerendering.com/s2024/content/EA-GIBS2/Apers_Advances-s2024_Shipping-Dynamic-GI.pdf)  
[Stochastic All the Thing](https://media.contentapi.ea.com/content/dam/ea/seed/presentations/dd18-seed-raytracing-in-hybrid-real-time-rendering.pdf)  
[Unreal Engine 5.5](https://github.com/EpicGames/UnrealEngine/tree/5.5)  
[SurfelGI-W298](https://github.com/W298/SurfelGI)  
[Stochasitc Screen-Space Reflections](https://www.ea.com/frostbite/news/stochastic-screen-space-reflections)  
[Ground Truth Ambient Occlusion](https://old.reddit.com/r/opengl/comments/96api8/has_anyone_successfully_implemented_groundtruth_ao/e40d2ie/)
