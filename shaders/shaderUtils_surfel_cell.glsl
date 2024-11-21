// Surfel and cells

const float surfelSize = 1.0f;
const float cellSize = 2.0f;
const uint kCellDimension = 64;
const uint kMaxLife = 6000u;
const uint kSleepingMaxLife = 60u;
const uint kMaxSurfelCount = 100u; 

vec3 getCameraPosition(SceneCamera camera)
{
    return vec3(camera.viewInverse[3]);
}

uint getFlattenCellIndex(vec3 cellPos)
{
    uvec3 unsignedPos = uvec3(cellPos + ivec3(kCellDimension / 2));
    return (unsignedPos.z * kCellDimension * kCellDimension) +
        (unsignedPos.y * kCellDimension) +
        unsignedPos.x;
}

uvec3 getCellPos(vec3 posW, vec3 cameraPosW, float cellUnit)
{
    vec3 posC = posW - cameraPosW;
    posC /= cellUnit;
    return uvec3(round(posC));
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

float calcRadiusApprox(float area, float distance, float fovy, vec2 resolution) {
    float angle = sqrt(area / 3.14159265359) * fovy / max(resolution.x, resolution.y);
    return distance * tan(angle);
}

float calcSurfelRadius(float distance, float fovy, vec2 resolution) {
    return min(calcRadiusApprox(surfelSize, distance, fovy, resolution), cellSize);
}

const vec3 neighborOffset[27] = vec3[27](
    // z = -1
    vec3(-1, -1, -1),
    vec3(-1, 0, -1),
    vec3(-1, 1, -1),
    vec3(0, -1, -1),
    vec3(0, 0, -1),
    vec3(0, 1, -1),
    vec3(1, -1, -1),
    vec3(1, 0, -1),
    vec3(1, 1, -1),
    // z = 0
    vec3(-1, -1, 0),
    vec3(-1, 0, 0),
    vec3(-1, 1, 0),
    vec3(0, -1, 0),
    vec3(0, 0, 0),
    vec3(0, 1, 0),
    vec3(1, -1, 0),
    vec3(1, 0, 0),
    vec3(1, 1, 0),
    // z = 1
    vec3(-1, -1, 1),
    vec3(-1, 0, 1),
    vec3(-1, 1, 1),
    vec3(0, -1, 1),
    vec3(0, 0, 1),
    vec3(0, 1, 1),
    vec3(1, -1, 1),
    vec3(1, 0, 1),
    vec3(1, 1, 1)
    );