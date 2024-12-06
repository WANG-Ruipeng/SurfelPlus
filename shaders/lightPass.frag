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
#include "shaderUtils.glsl"
#include "shaderUtil_grid.glsl"

#define FIREFLIES 1

layout(location = 0) in vec2 uvCoords;
layout(location = 0) out vec4 fragColor;

layout(set = 4, binding = 0) uniform usampler2D gbufferPrim;
layout(set = 4, binding = 1) uniform usampler2D gbufferNormal;
layout(set = 4, binding = 2) uniform sampler2D gbufferDepth;

layout(set = 5, binding = eSampler)	uniform sampler2D indirectLightMap;

layout(set = 6, binding = 5) uniform sampler2D reflectionColor;
layout(set = 6, binding = 6) uniform sampler2D reflectionDirection;
layout(set = 6, binding = 7) uniform sampler2D reflectionPointBrdf;
layout(set = 6, binding = 8) uniform sampler2D filteredReflectionColor;
layout(set = 6, binding = 9) uniform sampler2D bilateralCleanupColor;

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}


void main()
{
    uint primObjID = texelFetch(gbufferPrim, ivec2(gl_FragCoord.xy), 0).r;
    vec2 uv = (gl_FragCoord.xy + vec2(0.5)) / vec2(textureSize(indirectLightMap,0));
    uv *= 0.5f;

    uint nodeID = primObjID >> 23;
    uint instanceID = sceneNodes[nodeID].primMesh;
    mat4 worldMat = sceneNodes[nodeID].worldMatrix;
    uint primID = primObjID & 0x007FFFFF;
    InstanceData pinfo = geoInfo[instanceID];

    // Primitive buffer addresses
    Indices  indices  = Indices(pinfo.indexAddress);
    Vertices vertices = Vertices(pinfo.vertexAddress);

    // Indices of this triangle primitive.
    uvec3 tri = indices.i[primID];

    // All vertex attributes of the triangle.
    VertexAttributes attr0 = vertices.v[tri.x];
    VertexAttributes attr1 = vertices.v[tri.y];
    VertexAttributes attr2 = vertices.v[tri.z];

    // reconstruct world position from depth
    float depth = texelFetch(gbufferDepth, ivec2(gl_FragCoord.xy), 0).r;
    vec3 worldPos = WorldPosFromDepth(uvCoords, depth);

    // camera ray
    vec3 camPos = (sceneCamera.viewInverse * vec4(0, 0, 0, 1)).xyz;
    Ray camRay = Ray(camPos, normalize(worldPos - camPos));

    // Hitting the environment
    if(distance(worldPos, camPos) > 400.0)
    {
      vec3 env;
      if(_sunAndSky.in_use == 1)
        env = sun_and_sky(_sunAndSky, camRay.direction);
      else
      {
        vec2 uv = GetSphericalUv(camRay.direction);  // See sampling.glsl
        env     = texture(environmentTexture, uv).rgb;
      }
      // Done sampling return
      fragColor = vec4(env * rtxState.hdrMultiplier, 1.0);
      return;
    }

    // decompress normal
    vec3 normal = decompress_unit_vec(texelFetch(gbufferNormal, ivec2(gl_FragCoord.xy), 0).r);

    State state = GetState(primObjID, normal, depth, uvCoords);

    // Direct lighting
    VisibilityContribution directLight = DirectLight(camRay, state);

    Ray ray = Ray(worldPos, directLight.lightDir);
    //ray.origin = OffsetRay(ray.origin, normal);
    ray.origin += 1e-4 * normal;
    bool hit = AnyHit(ray, 100.0);

    //vec3 indirectLight = texelFetch(indirectLightMap, ivec2(gl_FragCoord.xy) / 2, 0).rgb;
    vec3 indirectLight = texture(indirectLightMap, uv).rgb;
    vec3 diffuseAlbedo = state.mat.albedo * (1.0 - state.mat.metallic);
    vec3 directLighting = hit ? vec3(0) : directLight.radiance;

    Light randLight = selectRandomLight(114514);
    float dist = distance(randLight.position, worldPos);
    //fragColor.xyz = directLighting + diffuseAlbedo * 1 / (dist * dist) * randLight.color * randLight.intensity;

    fragColor.xyz = directLighting + diffuseAlbedo * indirectLight + state.mat.emission;

    if(rtxState.debugging_mode != eNoDebug)
    {
        if(rtxState.debugging_mode == esBaseColor)
            fragColor.xyz = state.mat.albedo;
        else if(rtxState.debugging_mode == esNormal)
            fragColor.xyz = state.normal;
        else if(rtxState.debugging_mode == esMetallic)
            fragColor.xyz = vec3(state.mat.metallic);
        else if (rtxState.debugging_mode == esRoughness)
            fragColor.xyz = vec3(state.mat.roughness);
        else if (rtxState.debugging_mode == esUniformGrid){
            vec3 cellPos = getCellPos(worldPos, camPos);
            fragColor.xyz = fract(sin(dot(cellPos, vec3(12.9898, 78.233, 45.164))) * vec3(43758.5453, 28001.8384, 50849.4141));
        }    
        else if (rtxState.debugging_mode == esNonUniformGrid) {
            ivec4 cellPos4 = getCellPosNonUniform(worldPos, camPos);
            uint index = getFlattenCellIndexNonUniform(cellPos4);
            if (cellPos4.w == 0){
                fragColor.a = 0.0f;
            }
            else if(index < n*n*n + 6*m*n*n || index > 0) {
                vec3 normalizedPos = vec3(cellPos4.xyz) / float(n); 
                fragColor.xyz = clamp(normalizedPos, 0.0, 1.0); 
            }else{
                fragColor.xyz = vec3(1.0, 0.0, 0.0);
            }
        }
        else if (rtxState.debugging_mode == esEmissive)
            fragColor.xyz = state.mat.emission;
        else if (rtxState.debugging_mode == esReflectionBrdf){
            uv = ( gl_FragCoord.xy) / vec2(textureSize(bilateralCleanupColor,0));
            fragColor.xyz = texture(bilateralCleanupColor, uv).rgb;
            fragColor.a = 1.0;
        }
        else
            fragColor.xyz = indirectLight;
    }

}
