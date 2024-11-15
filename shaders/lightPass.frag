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
//    vec3 clipPos = vec3(uvCoords.x * 2.0 - 1.0, 1.0 - uvCoords.y * 2.0, texture(gbufferDepth, uvCoords).r);
//    vec3 worldPos = (sceneCamera.viewInverse * sceneCamera.projInverse * vec4(clipPos, 1.0)).xyz;
    float depth = texelFetch(gbufferDepth, ivec2(gl_FragCoord.xy), 0).r;
    vec3 worldPos = WorldPosFromDepth(uvCoords, depth);


    
    // Compute barycentric coordinates
    vec3 attr0_world = vec3(worldMat * vec4(attr0.position, 1.0));
    vec3 attr1_world = vec3(worldMat * vec4(attr1.position, 1.0));
    vec3 attr2_world = vec3(worldMat * vec4(attr2.position, 1.0));

    vec3 v0 = attr1_world - attr0_world;
    vec3 v1 = attr2_world - attr0_world;
    vec3 v2 = worldPos - attr0_world;

    float d00 = dot(v0, v0);
    float d01 = dot(v0, v1);
    float d11 = dot(v1, v1);
    float d20 = dot(v2, v0);
    float d21 = dot(v2, v1);
    float denom = d00 * d11 - d01 * d01;

    float w1 = (d11 * d20 - d01 * d21) / denom;
    float w2 = (d00 * d21 - d01 * d20) / denom;
    float w0 = 1.0 - w1 - w2;

//    // Tangent and Binormal
//    float h0 = (floatBitsToInt(attr0.texcoord.y) & 1) == 1 ? 1.0f : -1.0f;  // Handiness stored in the less
//    float h1 = (floatBitsToInt(attr1.texcoord.y) & 1) == 1 ? 1.0f : -1.0f;  // significative bit of the
//    float h2 = (floatBitsToInt(attr2.texcoord.y) & 1) == 1 ? 1.0f : -1.0f;  // texture coord V
//
//    const vec4 tng0     = vec4(decompress_unit_vec(attr0.tangent.x), h0);
//    const vec4 tng1     = vec4(decompress_unit_vec(attr1.tangent.x), h1);
//    const vec4 tng2     = vec4(decompress_unit_vec(attr2.tangent.x), h2);
//    vec3       tangent  = (tng0.xyz * w0 + tng1.xyz * w1 + tng2.xyz * w2);
//    tangent.xyz         = normalize(tangent.xyz);
//    vec3 world_tangent  = normalize(vec3(mat4(hstate.objectToWorld) * vec4(tangent.xyz, 0)));
//    world_tangent       = normalize(world_tangent - dot(world_tangent, world_normal) * world_normal);
//    vec3 world_binormal = cross(world_normal, world_tangent) * tng0.w;


    // TexCoord
    const vec2 uv0       = decode_texture(attr0.texcoord);
    const vec2 uv1       = decode_texture(attr1.texcoord);
    const vec2 uv2       = decode_texture(attr2.texcoord);

    vec2 uv = w0 * uv0 + w1 * uv1 + w2 * uv2;

    // Getting the material index on this geometry
    const uint matIndex = max(0, pinfo.materialIndex);  // material of primitive mesh
    GltfShadeMaterial mat = materials[matIndex];
    //uv = (vec4(uv.xy, 1, 1) * mat.uvTransform).xy;

    // decompress normal
    vec3 normal = decompress_unit_vec(texelFetch(gbufferNormal, ivec2(gl_FragCoord.xy), 0).r) * 2.0 - 1.0;

    // shading
    State state;
    state.depth = 0;
    state.eta = 1.0;
    state.position = worldPos;
    state.normal = normal;
    state.ffnormal = normal;
    state.texCoord = uv;
    state.isEmitter = false;
    state.specularBounce = false;
    state.isSubsurface = false;



    vec3 col = textureLod(texturesMap[nonuniformEXT(mat.pbrBaseColorTexture)], state.texCoord, 0).rgb;

    bool hit = AnyHit(Ray(worldPos, normal), 1000.0);

    
    fragColor.xyz = vec3(col);
    //fragColor.xyz = hash3u1(nodeID);
    //fragColor.xyz = vec3(w0, w1, w2);
    //fragColor.xyz = worldPos - attr0_world;
    fragColor.a = 1.0;
}
