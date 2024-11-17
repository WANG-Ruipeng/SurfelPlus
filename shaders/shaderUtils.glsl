
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

// RNG
// ref: https://www.shadertoy.com/view/wltcRS
//internal RNG state 
uvec4 s0, s1;

void rng_initialize(uvec2 p, uint frame)
{

    //white noise seed
    s0 = uvec4(p, frame, p.x + p.y);

    //blue noise seed
    s1 = uvec4(frame, frame * 15843, frame * 31 + 4566, frame * 2345 + 58585);
}

void pcg4d(inout uvec4 v)
{
    v = v * 1664525u + 1013904223u;
    v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
    v = v ^ (v >> 16u);
    v.x += v.y * v.w; v.y += v.z * v.x; v.z += v.x * v.y; v.w += v.y * v.z;
}

const float denom = 1.f / float(0xffffffffu);
float rand()
{
    pcg4d(s0); return float(s0.x) * denom;
}

vec2 rand2()
{
    pcg4d(s0); return vec2(s0.xy) * denom;
}

vec3 rand3()
{
    pcg4d(s0); return vec3(s0.xyz) * denom;
}

vec4 rand4()
{
    pcg4d(s0); return vec4(s0) * denom;
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

uvec3 getCellPos(vec3 posW, vec3 cameraPosW, float cellUnit)
{
    vec3 posC = posW - cameraPosW;
    posC /= cellUnit;
    return uvec3(round(posC));
}

vec3 getCameraPosition(SceneCamera camera)
{
    return vec3(camera.viewInverse[3]); 
}

const float surfelSize = 1.0f;
const float cellSize = 2.0f;
const uint kCellDimension = 64;
const uint kMaxLife = 240u;
const uint kSleepingMaxLife = 60u;

float calcRadiusApprox(float area, float distance, float fovy, vec2 resolution) {
    float angle = sqrt(area / 3.14159265359) * fovy / max(resolution.x, resolution.y);
    return distance * tan(angle);
}

float calcSurfelRadius(float distance, float fovy, vec2 resolution) {
    return min(calcRadiusApprox(surfelSize, distance, fovy, resolution), cellSize * 0.5);
}

bool isCellValid(vec3 cellPos)
{
    if (abs(cellPos.x) >= cellSize / 2)
        return false;
    if (abs(cellPos.y) >= cellSize / 2)
        return false;
    if (abs(cellPos.z) >= cellSize / 2)
        return false;

    return true;
}

bool isSurfelIntersectCell(Surfel surfel, vec3 cellPos, vec3 cameraPosW, float cellUnit)
{
	if (!isCellValid(cellPos))
		return false;

    vec3 minPosW = cellPos * cellUnit - cellUnit / 2.0f + cameraPosW;
    vec3 maxPosW = cellPos * cellUnit + cellUnit / 2.0f + cameraPosW;
    vec3 closePoint = min(max(surfel.position, minPosW), maxPosW);

    float dist = distance(closePoint, surfel.position);
    return dist < surfel.radius;
}

uint getFlattenCellIndex(vec3 cellPos)
{
    uvec3 unsignedPos = uvec3(cellPos + ivec3(kCellDimension / 2));
    return (unsignedPos.z * kCellDimension * kCellDimension) +
        (unsignedPos.y * kCellDimension) +
        unsignedPos.x;
}

// 3x3x3 neighborhood
const vec3 neighborOffset[125] = vec3[125](
    vec3(-2, -2, -2),
    vec3(-2, -2, -1),
    vec3(-2, -2, 0),
    vec3(-2, -2, 1),
    vec3(-2, -2, 2),
    vec3(-2, -1, -2),
    vec3(-2, -1, -1),
    vec3(-2, -1, 0),
    vec3(-2, -1, 1),
    vec3(-2, -1, 2),
    vec3(-2, 0, -2),
    vec3(-2, 0, -1),
    vec3(-2, 0, 0),
    vec3(-2, 0, 1),
    vec3(-2, 0, 2),
    vec3(-2, 1, -2),
    vec3(-2, 1, -1),
    vec3(-2, 1, 0),
    vec3(-2, 1, 1),
    vec3(-2, 1, 2),
    vec3(-2, 2, -2),
    vec3(-2, 2, -1),
    vec3(-2, 2, 0),
    vec3(-2, 2, 1),
    vec3(-2, 2, 2),
    vec3(-1, -2, -2),
    vec3(-1, -2, -1),
    vec3(-1, -2, 0),
    vec3(-1, -2, 1),
    vec3(-1, -2, 2),
    vec3(-1, -1, -2),
    vec3(-1, -1, -1),
    vec3(-1, -1, 0),
    vec3(-1, -1, 1),
    vec3(-1, -1, 2),
    vec3(-1, 0, -2),
    vec3(-1, 0, -1),
    vec3(-1, 0, 0),
    vec3(-1, 0, 1),
    vec3(-1, 0, 2),
    vec3(-1, 1, -2),
    vec3(-1, 1, -1),
    vec3(-1, 1, 0),
    vec3(-1, 1, 1),
    vec3(-1, 1, 2),
    vec3(-1, 2, -2),
    vec3(-1, 2, -1),
    vec3(-1, 2, 0),
    vec3(-1, 2, 1),
    vec3(-1, 2, 2),
    vec3(0, -2, -2),
    vec3(0, -2, -1),
    vec3(0, -2, 0),
    vec3(0, -2, 1),
    vec3(0, -2, 2),
    vec3(0, -1, -2),
    vec3(0, -1, -1),
    vec3(0, -1, 0),
    vec3(0, -1, 1),
    vec3(0, -1, 2),
    vec3(0, 0, -2),
    vec3(0, 0, -1),
    vec3(0, 0, 0),
    vec3(0, 0, 1),
    vec3(0, 0, 2),
    vec3(0, 1, -2),
    vec3(0, 1, -1),
    vec3(0, 1, 0),
    vec3(0, 1, 1),
    vec3(0, 1, 2),
    vec3(0, 2, -2),
    vec3(0, 2, -1),
    vec3(0, 2, 0),
    vec3(0, 2, 1),
    vec3(0, 2, 2),
    vec3(1, -2, -2),
    vec3(1, -2, -1),
    vec3(1, -2, 0),
    vec3(1, -2, 1),
    vec3(1, -2, 2),
    vec3(1, -1, -2),
    vec3(1, -1, -1),
    vec3(1, -1, 0),
    vec3(1, -1, 1),
    vec3(1, -1, 2),
    vec3(1, 0, -2),
    vec3(1, 0, -1),
    vec3(1, 0, 0),
    vec3(1, 0, 1),
    vec3(1, 0, 2),
    vec3(1, 1, -2),
    vec3(1, 1, -1),
    vec3(1, 1, 0),
    vec3(1, 1, 1),
    vec3(1, 1, 2),
    vec3(1, 2, -2),
    vec3(1, 2, -1),
    vec3(1, 2, 0),
    vec3(1, 2, 1),
    vec3(1, 2, 2),
    vec3(2, -2, -2),
    vec3(2, -2, -1),
    vec3(2, -2, 0),
    vec3(2, -2, 1),
    vec3(2, -2, 2),
    vec3(2, -1, -2),
    vec3(2, -1, -1),
    vec3(2, -1, 0),
    vec3(2, -1, 1),
    vec3(2, -1, 2),
    vec3(2, 0, -2),
    vec3(2, 0, -1),
    vec3(2, 0, 0),
    vec3(2, 0, 1),
    vec3(2, 0, 2),
    vec3(2, 1, -2),
    vec3(2, 1, -1),
    vec3(2, 1, 0),
    vec3(2, 1, 1),
    vec3(2, 1, 2),
    vec3(2, 2, -2),
    vec3(2, 2, -1),
    vec3(2, 2, 0),
    vec3(2, 2, 1),
    vec3(2, 2, 2)
    );

#ifdef LAYOUTS_GLSL
    State GetState(uint primObjID, vec3 normal, float depth)
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
        vec3 worldPos = WorldPosFromDepth(vec2(gl_FragCoord.xy) / vec2(rtxState.size), depth);

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
        state.ffnormal = normal;

        // Filling material structures
        vec3 camPos = (sceneCamera.viewInverse * vec4(0, 0, 0, 1)).xyz;
        Ray camRay = Ray(camPos, normalize(worldPos - camPos));
        GetMaterialsAndTextures(state, camRay);

        return state;
    }

#endif