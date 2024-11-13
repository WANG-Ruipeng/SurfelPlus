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

//-------------------------------------------------------------------------------------------------
// This is called by the post process shader to display the result of ray tracing.
// It applied a tonemapper and do dithering on the image to avoid banding.

#version 460
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_ray_query : enable
#extension GL_ARB_shader_clock : enable                 // Using clockARB
#extension GL_EXT_shader_image_load_formatted : enable  // The folowing extension allow to pass images as function parameters

#extension GL_NV_shader_sm_builtins : require     // Debug - gl_WarpIDNV, gl_SMIDNV
#extension GL_ARB_gpu_shader_int64 : enable       // Debug - heatmap value
#extension GL_EXT_shader_realtime_clock : enable  // Debug - heatmap timing

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_debug_printf : enable
#extension GL_KHR_vulkan_glsl : enable

#include "host_device.h"

layout(push_constant) uniform _RtxState
{
  RtxState rtxState;
};


#include "globals.glsl"

PtPayload        prd;
ShadowHitPayload shadow_payload;

#include "layouts.glsl"
#include "random.glsl"
#include "common.glsl"
#include "traceray_rq.glsl"

#include "pathtrace.glsl"

#define FIREFLIES 1

layout(location = 0) in vec2 uvCoords;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform usampler2D gbufferMaterial;
layout(set = 0, binding = 1) uniform usampler2D gbufferNormal;
layout(set = 0, binding = 2) uniform sampler2D gbufferDepth;


void main()
{
    fragColor.xyz = vec3(1.0, 0.0, 0.0);
    fragColor.a = 1.0;
}
