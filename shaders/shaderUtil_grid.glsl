vec3 getCellPos(vec3 posW, vec3 cameraPosW)
{
    vec3 posC = posW - cameraPosW;
    posC /= cellSize;
    return round(posC);
}

uint getFlattenCellIndex(vec3 cellPos)
{

    uvec3 unsignedPos = uvec3(cellPos + ivec3(kCellDimension / 2));

    uint result = (unsignedPos.z * kCellDimension * kCellDimension) +
        (unsignedPos.y * kCellDimension) +
        unsignedPos.x;

    return result;
}

const float d = 64.0;     // Size of the uniform cube
const int n = 32; // Split count of the uniform cube & non-unifrom frustum 
const float p = 1.3; // Split ratio of the non-uniform frustum
const int m = 16; // Layers of the non-uniform frustum

ivec4 getCellPosNonUniform(vec3 posW, vec3 cameraPosW)
{
    vec3 posC = posW - cameraPosW;

    // Determine region
    int region = 0;
    float half_d = d / 2.0;
    if (abs(posC.x) <= half_d && abs(posC.y) <= half_d && abs(posC.z) <= half_d)
    {
        // Inside the cube
        region = 0;
        int x_index = int((posC.x + d / 2.0) / (d / float(n)));
        int y_index = int((posC.y + d / 2.0) / (d / float(n)));
        int z_index = int((posC.z + d / 2.0) / (d / float(n)));

        return ivec4(x_index, y_index, z_index, region);
    }

    // Inside the frustum, determine the main frustum axis
    float main_axis_pos = 0;
    float other_axis_pos_1 = 0;
    float other_axis_pos_2 = 0;
    float absX = abs(posC.x);
    float absY = abs(posC.y);
    float absZ = abs(posC.z);
    if (absX >= absY && absX >= absZ)
    {
        region = (posC.x > 0.0) ? 1 : 2; // 1: +X, 2: -X
        main_axis_pos = posC.x;
        other_axis_pos_1 = posC.y;
        other_axis_pos_2 = posC.z;
    }
    else if (absY >= absX && absY >= absZ)
    {
        region = (posC.y > 0.0) ? 3 : 4; // 3: +Y, 4: -Y
        other_axis_pos_1 = posC.x;
        main_axis_pos = posC.y;
        other_axis_pos_2 = posC.z;
    }
    else
    {
        region = (posC.z > 0.0) ? 5 : 6; // 5: +Z, 6: -Z
        other_axis_pos_1 = posC.x;
        other_axis_pos_2 = posC.y;
        main_axis_pos = posC.z;
    }

    float s = main_axis_pos - half_d;
    int k = int(log(1 - n * s * (1 - p) / d) / log(p));
    int u = int(other_axis_pos_1 * n * 0.5 / main_axis_pos);
    int v = int(other_axis_pos_2 * n * 0.5 / main_axis_pos);
    return ivec4(k, u, v, region);
}

uint getFlattenCellIndexNonUniform(ivec4 cellPos)
{
    int region = cellPos.w;

    if (region == 0)
    {
        int x = cellPos.x;
        int y = cellPos.y;
        int z = cellPos.z;
        return uint(x + n * (y + n * z));
    }
    else
    {
        int s = region - 1; 
        int k = cellPos.x;
        int u = cellPos.y;
        int v = cellPos.z;
        return uint(n * n * n) + uint(s * m * n * n) + uint(k * n * n + u * n + v);
    }

    return 0;
}