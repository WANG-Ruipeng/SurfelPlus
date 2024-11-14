
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
    float z = depth * 2.0 - 1.0;
    vec4 clipSpacePosition = vec4(uv * 2.0 - 1.0, z, 1.0);
    vec4 viewSpacePosition = sceneCamera.projInverse * clipSpacePosition;
    vec4 worldSpacePosition = sceneCamera.viewInverse * viewSpacePosition;

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

const float cellSize = 0.05f;
const uint kCellDimension = 64;

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