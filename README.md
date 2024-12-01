# SurfelPlus Readme

*University of Pennsylvania, CIS 565: GPU Programming and Architecture, Final Project*

This project is developed base on Nvidia's [vk_raytrace renderer](https://github.com/nvpro-samples/vk_raytrace/tree/master).

**IMPORTANT**: This readme file will only include the basic setup and usage for this project. For a complete development log and demo, please visit this site: [SurfelPlus Project Page](https://wang-ruipeng.github.io/SurfelPlus/)

## Usage

**Controls**

| Action | Description |
| --- | --- |
| `LMB` | Rotate around the target |
| `RMB` | Dolly in/out |
| `MMB` | Pan along view plane |
| `LMB + Shift` | Dolly in/out |
| `LMB + Ctrl` | Pan |
| `LMB + Alt` | Look around |
| `Mouse wheel` | Dolly in/out |
| `Mouse wheel + Shift` | Zoom in/out (FOV) |
| `Space` | Set interest point on the surface under the mouse cursor. |
| `F10` | Toggle UI pane. |

**Change glTF model**

- Drag and drop glTF files (`.gltf` or `.glb`) into viewer

**Change HDR lighting**

- Drag and drop HDR files (`.hdr`) into viewer

## Setup

You can use cmake to build this project.

```
git clone <https://github.com/WANG-Ruipeng/SurfelPlus.git>
cd ./SurfelPlus
mkdir build
cd ./build
cmake-gui ..
```

We recommend build this project based on Visual Studio 2022 as it is used by everyone in the team.