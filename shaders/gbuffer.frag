#version 450
#extension GL_GOOGLE_include_directive : enable
#extension GL_EXT_debug_printf : enable
#extension GL_ARB_gpu_shader_int64 : enable  // Shader reference

#include "compress.glsl"

layout(location = 0) in flat uint instanceID;
layout(location = 1) in vec3 normal;

layout(location = 0) out uint out_primObjID;
layout(location = 1) out uint out_normal;

void main()
{
	out_primObjID = ((instanceID << 23) & 0x7F800000) | (gl_PrimitiveID & 0x007FFFFF);
	out_normal = compress_unit_vec(normalize(normal));
}