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

#define FIREFLIES 1

layout(location = 0) in vec2 uvCoords;
layout(location = 0) out vec4 fragColor;

layout(set = 4, binding = 0) uniform usampler2D gbufferPrim;
layout(set = 4, binding = 1) uniform usampler2D gbufferNormal;
layout(set = 4, binding = 2) uniform sampler2D gbufferDepth;

layout(set = 5, binding = eSampler)	uniform sampler2D indirectLightMap;


void main()
{
    uint primObjID = texelFetch(gbufferPrim, ivec2(gl_FragCoord.xy), 0).r;

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

//    vec3 col = textureLod(texturesMap[nonuniformEXT(mat.pbrBaseColorTexture)], state.texCoord, 0).rgb;

    bool hit = AnyHit(Ray(worldPos, directLight.lightDir), 1000.0);

    vec3 indirectLight = texelFetch(indirectLightMap, ivec2(gl_FragCoord.xy), 0).rgb;
    vec3 diffuseAlbedo = state.mat.albedo * (1.0 - state.mat.metallic);
    vec3 directLighting = hit ? vec3(0) : directLight.radiance;

    Light randLight = selectRandomLight(114514);
    float dist = distance(randLight.position, worldPos);
    fragColor.xyz = vec3(dist);

//    fragColor.xyz = IntegerToColor(matIndex);
//    fragColor.xyz = vec3(dot(state.normal, camRay.direction) <= 0.0 ? state.normal : -state.normal);
    //fragColor.xyz = hash3u1(nodeID);
    //fragColor.xyz = vec3(w0, w1, w2);
    //fragColor.xyz = worldPos - attr0_world;
//    fragColor.xyz = textureLod(environmentTexture, GetSphericalUv(normalize(worldPos - camPos)), 2).rgb;
    //fragColor.xyz = hit ? vec3(0) : directLight.radiance;
//    fragColor.xyz = directLighting + diffuseAlbedo * indirectLight;
    //fragColor.xyz = indirectLight;
    fragColor.a = 1.0;
}
