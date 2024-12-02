# SurfelPlus Project Page

![DemoImage.png](/img/DemoImage.png)
![DemoImage.png](/img/logo.png)

# Introduction

**SurfelPlus** is a real-time dynamic global illumination renderer built on top of NVIDIA's vk_raytrace framework. Using **Vulkan ray tracing** and **surfel-based** techniques, SurfelPlus aims to deliver high-quality lighting effects with dynamic scene updates, providing an efficient solution for realistic and immersive visual rendering in real time.

# Technical Overview

**SurfelPlus** utilizes surfel-based techniques to achieve high-quality and accurate lighting effects. This approach builds on the surfel GI method presented by EA SEED in their [SIGGRAPH 2021 talk](https://www.ea.com/seed/news/siggraph21-global-illumination-surfels) and [SIGGRAPH 2024 talk](https://advances.realtimerendering.com/s2024/content/EA-GIBS2/Apers_Advances-s2024_Shipping-Dynamic-GI.pdf). Originally designed for the Frostbite engine, these techniques have been carefully adapted and optimized for our Vulkan-based renderer, taking advantage of Vulkan's flexibility and ray tracing capabilities to deliver real-time global illumination in dynamic scenes.

### What are Surfels?

Surfels, short for "surface elements," are point-like primitives that represent the surface of a 3D object. Each surfel stores key information, including position, normal, color, and other material properties, enabling efficient computation of light interactions in a scene. 

![image.png](/img/surfel.png)

In global illumination, surfels serve as intermediaries for light propagation and reflection, balancing accuracy and computational cost.

### Key Features and Techniques

1. **Surfel-Based Indirect Lighting**
    
    Indirect lighting is computed by storing and sharing light energy across surfels. This approach captures diffuse inter-reflections and supports dynamic scenes without requiring precomputed data, making it well-suited for interactive applications.
    
    ![image.png](/img/surfelIndirect.png)
    
2. **Lighting Integration**
    
    SurfelPlus leverages Vulkan's capabilities for direct lighting, while surfels handle the more performance-intensive indirect light calculations. This hybrid approach optimizes rendering performance without sacrificing visual quality. 
    
3. **Global Illumination Pipeline**
    - **Surfel Preparation**: Surfels are generated or updated based on the surfel coverage of the screen. Each surfel's properties (e.g., normal, color, and reflectance) are computed to match the underlying surface.
    - **Surfel Visibility and Culling**: Surfels outside the camera's view frustum or occluded by other geometry are efficiently culled to reduce computational overhead.
    - **Lighting Calculation**: Light bounces are simulated using surfel-to-surfel interactions, enabling indirect light accumulation in the scene.
4. **Grid-based surfel acceleration structure**
A grid-based surfel acceleration structure organizes surfels into spatial cells for fast neighbor queries and updates. This method enables efficient surfel-to-surfel interactions and scalable indirect lighting computations.
    
    ![image.png](/img/Grid-Based.png)
    

### Advantages of Surfel-Based GI

- **Real-Time Performance**: Unlike traditional methods that rely on precomputed lightmaps or heavy ray tracing, surfel GI enables dynamic updates at real-time frame rates.
- **Dynamic Scene Support**: Lighting changes are immediately reflected, making SurfelPlus ideal for interactive environments and games.
- **Physically-Based Lighting**: Surfels inherently store surface and material properties, allow our rendering pipeline seamlessly to integrate into original PBR methods.

# Milestones Development Log

## Milestone 1

[Click here for the original PDF.](https://medias.wangruipeng.com/SurfelPlusMilestone1.pdf)

For our first milestone, we made significant progress in setting up the foundation of our renderer:

### Key Achievements

- Conducted multiple meetings and discussions to:
    - Define all necessary render passes.
    - Explore possible implementation details.
- Familiarized ourselves with the codebase and Vulkan framework, including features like ray queries and resource management.
- Created initial implementations of basic render passes, some of which are naive or empty placeholders.
- Added a functional G-buffer pass, capturing essential attributes like visibility, normals, and depth.
- Began working on a naive surfelization approach to integrate surfels into the pipeline.

This milestone established a solid groundwork for our surfel-based GI system.

### Render Passes Implemented

- **G-buffer Pass**: Captures visibility, normal, and depth information.
- **Surfel Prepare Compute Pass**: Prepares data for surfel-based lighting calculations.
- **Surfel Generation Compute Pass**: Generates surfels from the scene geometry.
- **Surfel Update Compute Pass**: Updates surfel data dynamically.
- **Direct Lighting Pass**: Computes direct lighting effects.
- **Post-Processing Pass**: Applies post-processing effects to the final image.

![image.png](/img/M1Passes.png)

### Demo

| Surfel Generation | Visibility Buffer | Normal Buffer |
|-----------------|----------------|----------------|
| ![image.png](/img/M1Demo1.png) | ![image.png](/img/M1Demo2.png) | ![image.png](/img/M1Demo3.png) |

### Milestone 2 Goals

For the second milestone, our focus is on improving the surfel system and integrating advanced features:

- **Enhanced Surfelization**: Refine the process of generating and managing surfels for better accuracy and performance.
- **Surfel Recycling**: Implement a recycling mechanism to reuse surfels efficiently, reducing memory overhead.
- **Surfel Acceleration Structure**: Develop a grid-based acceleration structure to enable fast surfel queries and interactions.
- **Surfel Ray Generation & Ray Tracing (If Possible)**: Explore the integration of surfel-based ray tracing for more accurate light propagation and visibility calculations.

These improvements aim to advance the overall efficiency and realism of our renderer.

## Milestone 2

[Click here for the original PDF.](https://medias.wangruipeng.com/SurfelPlusMilestone2.pdf)

For Milestone 2, we achieved significant advancements in our surfel-based GI system:

### Completed Goals

- **Better Surfelization**: Improved surfel generation for greater accuracy and efficiency.
- **Surfel Recycling**: Implemented a mechanism to reuse surfels, reducing memory usage and maintaining dynamic scene updates.
- **Surfel Acceleration Structure**: Developed a naive uniform grid-based acceleration structure to facilitate fast surfel queries and interactions.

### Partially Completed Goal

- **Surfel Ray Generation & Ray Tracing**: Initiated work on integrating ray generation and tracing using surfels, with partial implementation completed.

### New Passes Implemented:

- **Surfel Compute Passes**:
    - Surfel Generation
    - Surfel Trace
    - Surfel Update/Recycle
- **Integration pass for collecting all surfel information**

![image.png](/img/M2Passes.png)

### Demo

| Diffuse | Surfel indirect |
|-----------------|----------------|
| ![image.png](/img/M2Demo1.png) | ![image.png](/img/M2Demo2.png) |

### Milestone 3 & Final Goals

Looking ahead to Milestone 3 and the final stage of the project, our goals include:

- **Glossy Indirect Lighting**: Add support for glossy reflections in the indirect lighting pipeline.
- **Spatial-Temporal Filtering**: Improve temporal stability and spatial consistency for better rendering quality.
- **Better Surfelization & Tracing**: Refine surfel generation and tracing algorithms for enhanced performance and accuracy.
- **Non-Uniform Acceleration Structure**: Transition from a uniform grid to a non-uniform structure for improved scalability and efficiency.
- **Demo Scenes**: Identify and prepare demonstration scenes to showcase the renderer's capabilities.

## Milestone 3

[Click here for the original PDF.](https://medias.wangruipeng.com/SurfelPlusMilestone3.pdf)

For Milestone 3, we addressed key issues identified in Milestone 2 and implemented significant improvements:

### Issues from Milestone 2:

- **Low Resolution & Poor Performance**: The previous implementation suffered from low resolution and suboptimal frame rates.
- **Low-Frequency Noise**: Visible artifacts in the lighting pipeline reduced visual quality.

### Milestone 3 Improvements:

- **Better Surfelization & Tracing**: Enhanced surfel generation and tracing algorithms to improve accuracy and reduce artifacts.
- **Higher Resolution & Improved Performance**: Increased rendering resolution from **1080x720** to **2560x1440** while keeping over 120 FPS for smoother performance.
- **Non-Uniform Acceleration Structure**: Replaced the uniform grid with a non-uniform structure for more efficient surfel queries and better scalability.
- **Improved Stability**:
    - Render pipeline synchronization to prevent flickering and inconsistencies.
    - G-buffer depth bias and ray offset techniques to mitigate precision issues.
    - More uniform surfel distribution to ensure consistent lighting quality.

### New Features:

- Added **new demo scenes** to better showcase the renderer's capabilities in diverse lighting and scene configurations.

### Demo

| Before Milestone 3 | After Milestone 3 |
|-----------------|----------------|
| ![image.png](/img/M3Demo1.png) | ![image.png](/img/M3Demo2.png) |


### Goals for Milestone 3 & Final:

- **Glossy Indirect Lighting**: Adding support for glossy reflections (currently in progress).
- **Spatial-Temporal Filtering**: Implementing filtering techniques to improve temporal stability and spatial quality (currently in progress).
- **Better Surfelization & Tracing**: Completed with significant enhancements.
- **Non-Uniform Acceleration Structure**: Successfully implemented.

These improvements position **SurfelPlus** as a highly efficient and visually robust renderer capable of dynamic global illumination with high performance and quality.