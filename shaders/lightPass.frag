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

#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_debug_printf : enable
#extension GL_ARB_gpu_shader_int64 : enable  // Shader reference
#extension GL_KHR_vulkan_glsl : enable

#define TONEMAP_UNCHARTED
#include "random.glsl"
#include "tonemapping.glsl"
#include "host_device.h"


layout(location = 0) in vec2 uvCoords;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform usampler2D gbufferMaterial;
layout(set = 0, binding = 1) uniform sampler2D gbufferNormal;
layout(set = 0, binding = 2) uniform sampler2D gbufferDepth;


void main()
{
  // Raw result of ray tracing
//  vec4 hdr = texture(inImage, uvCoords * tm.zoom).rgba;
//
//  if(((tm.autoExposure >> 0) & 1) == 1)
//  {
//    vec4  avg     = textureLod(inImage, vec2(0.5), 20);  // Get the average value of the image
//    float avgLum2 = luminance(avg.rgb);                  // Find the luminance
//    if(((tm.autoExposure >> 1) & 1) == 1)
//      hdr.rgb = toneLocalExposure(hdr.rgb, avgLum2);  // Adjust exposure
//    else
//      hdr.rgb = toneExposure(hdr.rgb, avgLum2);  // Adjust exposure
//  }
//
//  // Tonemap + Linear to sRgb
//  vec3 color = toneMap(hdr.rgb, tm.avgLum);
//
//  // Remove banding
//  if(tm.dither > 0)
//  {
//    // Generates a 3D random number using the PCG (Permuted Congruential Generator) algorithm
//    uvec3 r = pcg3d(uvec3(gl_FragCoord.xy, 0));
//
//    // The HEX value 0x3f800000 corresponds to the 32-bit floating-point representation of 1.0f.
//    // It is bitwise ORed into the lower 23 bits of the 32-bit floating-point format,
//    // following the IEEE 754 standard (1 sign bit, 8 exponent bits, 23 mantissa bits).
//    // This operation effectively sets the exponent to 0 (bias of 127), generating a float between 1.0 and 2.0.
//    // The uintBitsToFloat function then interprets this bit pattern as a floating-point number,
//    // and finally, 1.0 is subtracted to bring the range to (0.0, 1.0).
//    vec3 noise = uintBitsToFloat(0x3f800000 | (r >> 9)) - 1.0f;
//
//    // Apply dithering to hide banding artifacts.
//    color = dither(sRGBToLinear(color), noise, 1. / 255.);
//  }
//
//  // contrast
//  color = clamp(mix(vec3(0.5), color, tm.contrast), 0, 1);
//  // brighness
//  color = pow(color, vec3(1.0 / tm.brightness));
//  // saturation
//  vec3 i = vec3(dot(color, vec3(0.299, 0.587, 0.114)));
//  color  = mix(i, color, tm.saturation);
//  // vignette
//  vec2 uv = ((uvCoords * tm.renderingRatio) - 0.5) * 2.0;
//  color *= 1.0 - dot(uv, uv) * tm.vignette;
//
//  fragColor.xyz = color;
//  fragColor.a   = hdr.a;

    fragColor.xyz = vec3(1.0, 0.0, 0.0);
    fragColor.a = 1.0;
}
