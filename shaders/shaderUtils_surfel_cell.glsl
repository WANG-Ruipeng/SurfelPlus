// Surfel and cells

vec3 getCameraPosition(SceneCamera camera)
{
    return vec3(camera.viewInverse[3]);
}

uint getFlattenCellIndex(vec3 cellPos)
{

    uvec3 unsignedPos = uvec3(cellPos + ivec3(kCellDimension / 2));

    uint result = (unsignedPos.z * kCellDimension * kCellDimension) +
        (unsignedPos.y * kCellDimension) +
        unsignedPos.x;

    return result;
}

vec3 getCellPos(vec3 posW, vec3 cameraPosW)
{
    vec3 posC = posW - cameraPosW;
    posC /= cellSize;
    return round(posC);
}

bool isCellValid(vec3 cellPos)
{
    if (abs(cellPos.x) >= kCellDimension / 2)
        return false;
    if (abs(cellPos.y) >= kCellDimension / 2)
        return false;
    if (abs(cellPos.z) >= kCellDimension / 2)
        return false;

    return true;
}

bool isSurfelIntersectCell(Surfel surfel, vec3 cellPos, vec3 cameraPosW)
{
    if (!isCellValid(cellPos))
        return false;

    vec3 minPosW = cellPos * cellSize - vec3(cellSize) / 2.0f + cameraPosW;
    vec3 maxPosW = cellPos * cellSize + vec3(cellSize) / 2.0f + cameraPosW;
    vec3 closePoint = min(max(surfel.position, minPosW), maxPosW);

    float dist = distance(closePoint, surfel.position);

    return dist < surfel.radius;
}

float calcRadiusApprox(float area, float distance, float fovy, vec2 resolution) {
    float angle = sqrt(area / 3.14159265359) * fovy / max(resolution.x, resolution.y);
    return distance * tan(angle);
}

float calcSurfelRadius(float distance, float fovy, vec2 resolution) {
    return min(calcRadiusApprox(surfelSize, distance, fovy, resolution), cellSize * 0.5f);
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