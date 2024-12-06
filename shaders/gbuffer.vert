#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_debug_printf : enable
#extension GL_EXT_scalar_block_layout : enable

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_debug_printf : enable

#include "host_device.h"
#include "compress.glsl"

layout(set = 0, binding = 0,	scalar)		uniform _SceneCamera	{ SceneCamera sceneCamera; };

layout(push_constant) uniform Instance {
    mat4 model;
    mat4 modelInvTrp;
    uint id;
} instanceData;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in uint in_normal;

layout(location = 0) out flat uint instanceID;
layout(location = 1) out vec3 normal;

void main()
{
  instanceID = instanceData.id;
  vec3 normalL = decompress_unit_vec(in_normal);
  normal = mat3(instanceData.modelInvTrp) * normalL;

  vec4 wpos = instanceData.model * vec4(in_pos, 1);
  wpos /= wpos.w;

  gl_Position = sceneCamera.proj * sceneCamera.view * wpos;
}