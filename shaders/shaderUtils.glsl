#include "shaderUtils_surfel_cell.glsl"

float hash13(vec3 p3)
{
    p3 = fract(p3 * .1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 hash3u1(uint n)
{
    n = (n << 13U) ^ n;
    n = n * (n * n * 15731U + 789221U) + 1376312589U;
    uvec3 k = n * uvec3(n, n * 16807U, n * 48271U);
    return vec3(k & uvec3(0x7fffffffU)) / float(0x7fffffff);
}

vec3 WorldPosFromDepth(in vec2 uv, in float depth)
{
    //float z = depth * 2.0 - 1.0;
    float z = depth;
    //uv.y = 1.f - uv.y;
    vec4 clipSpacePosition = vec4(uv * 2.0 - 1.0, z, 1.0);
    vec4 viewSpacePosition = sceneCamera.projInverse * clipSpacePosition;
	viewSpacePosition /= viewSpacePosition.w;
    vec4 worldSpacePosition = sceneCamera.viewInverse * viewSpacePosition;
	worldSpacePosition /= worldSpacePosition.w;
    return worldSpacePosition.xyz;
}


#ifdef LAYOUTS_GLSL
    State GetState(in uint primObjID, in vec3 normal, in float depth, vec2 texCoord)
    {
        uint nodeID = primObjID >> 23;
        uint instanceID = sceneNodes[nodeID].primMesh;
        mat4 worldMat = sceneNodes[nodeID].worldMatrix;
        uint primID = primObjID & 0x007FFFFF;
        InstanceData pinfo = geoInfo[instanceID];

        // Primitive buffer addresses
        Indices  indices = Indices(pinfo.indexAddress);
        Vertices vertices = Vertices(pinfo.vertexAddress);

        // Indices of this triangle primitive.
        uvec3 tri = indices.i[primID];

        // All vertex attributes of the triangle.
        VertexAttributes attr0 = vertices.v[tri.x];
        VertexAttributes attr1 = vertices.v[tri.y];
        VertexAttributes attr2 = vertices.v[tri.z];

        // reconstruct world position from depth
        vec3 worldPos = WorldPosFromDepth(texCoord, depth);

        // decompress normal
        //vec3 normal = decompress_unit_vec(texelFetch(gbufferNormal, ivec2(gl_FragCoord.xy), 0).r) * 2.0 - 1.0;

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

        // Tangent and Binormal
        float h0 = (floatBitsToInt(attr0.texcoord.y) & 1) == 1 ? 1.0f : -1.0f;  // Handiness stored in the less
        float h1 = (floatBitsToInt(attr1.texcoord.y) & 1) == 1 ? 1.0f : -1.0f;  // significative bit of the
        float h2 = (floatBitsToInt(attr2.texcoord.y) & 1) == 1 ? 1.0f : -1.0f;  // texture coord V

        const vec4 tng0 = vec4(decompress_unit_vec(attr0.tangent.x), h0);
        const vec4 tng1 = vec4(decompress_unit_vec(attr1.tangent.x), h1);
        const vec4 tng2 = vec4(decompress_unit_vec(attr2.tangent.x), h2);
        vec3       tangent = (tng0.xyz * w0 + tng1.xyz * w1 + tng2.xyz * w2);
        tangent.xyz = normalize(tangent.xyz);
        vec3 world_tangent = normalize(vec3(mat4(worldMat) * vec4(tangent.xyz, 0)));
        world_tangent = normalize(world_tangent - dot(world_tangent, normal) * normal);
        vec3 world_binormal = cross(normal, world_tangent) * tng0.w;

        // TexCoord
        const vec2 uv0 = decode_texture(attr0.texcoord);
        const vec2 uv1 = decode_texture(attr1.texcoord);
        const vec2 uv2 = decode_texture(attr2.texcoord);

        vec2 uv = w0 * uv0 + w1 * uv1 + w2 * uv2;

        // Getting the material index on this geometry
        const uint matIndex = max(0, pinfo.materialIndex);  // material of primitive mesh
        GltfShadeMaterial mat = materials[matIndex];

		// Camera ray
        vec3 camPos = (sceneCamera.viewInverse * vec4(0, 0, 0, 1)).xyz;
        Ray camRay = Ray(camPos, normalize(worldPos - camPos));

        // shading
        State state;
        state.position = worldPos;
        state.normal = normal;
        state.tangent = world_tangent;
        state.bitangent = world_binormal;
        state.texCoord = uv;
        state.matID = matIndex;
        state.isEmitter = false;
        state.specularBounce = false;
        state.isSubsurface = false;
        state.ffnormal = dot(state.normal, camRay.direction) <= 0.0 ? state.normal : -state.normal;

        // Filling material structures
        
        GetMaterialsAndTextures(state, camRay);

        return state;
    }

    // Select light uniformly from light buffer
    Light selectRandomLight(uint seed)
    {
        // uncomment this after implementing numlight uniform 
		//int index = floor(rand(seed) * float(numLights));

        int index = 0;
		return lights[index];
    }
#endif