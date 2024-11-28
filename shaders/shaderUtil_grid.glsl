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

const float d = 128.0;     // Size of the uniform cube
const int n = 64; // Split count of the uniform cube & non-unifrom frustum 
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

bool isSurfelIntersectCellNonUniform(Surfel surfel, ivec4 cellPos, vec3 cameraPosW)
{
    int region = cellPos.w;
    vec3 minPos;
    vec3 maxPos;
    float half_d = d / 2.0;
    float delta = d / float(n);

    if (region == 0)
    {
        // Inside the cube
        
        int x_index = cellPos.x;
        int y_index = cellPos.y;
        int z_index = cellPos.z;

        minPos.x = -half_d + x_index * delta;
        maxPos.x = minPos.x + delta;

        minPos.y = -half_d + y_index * delta;
        maxPos.y = minPos.y + delta;

        minPos.z = -half_d + z_index * delta;
        maxPos.z = minPos.z + delta;
    }
    else
    {
        // Inside the frustum
        int s = region;
        int k = cellPos.x;
        int u = cellPos.y;
        int v = cellPos.z;

        // Compute s0 and s1
        float s0 = delta * (1.0 - pow(p, float(k))) / (1.0 - p);
        float s1 = delta * (1.0 - pow(p, float(k + 1))) / (1.0 - p);

        float main_axis_min = half_d + s0;
        float main_axis_max = half_d + s1;

        if (region % 2 == 0) // Negative direction
        {
            main_axis_min = -(half_d + s1);
            main_axis_max = -(half_d + s0);
        }

        float main_axis_length = main_axis_max - main_axis_min;

        // Other axes
        float other_axis_min_1 = -main_axis_length / 2.0 + float(u) * (main_axis_length / float(n));
        float other_axis_max_1 = other_axis_min_1 + main_axis_length / float(n);

        float other_axis_min_2 = -main_axis_length / 2.0 + float(v) * (main_axis_length / float(n));
        float other_axis_max_2 = other_axis_min_2 + main_axis_length / float(n);

        // Assign minPos and maxPos based on region
        if (region == 1 || region == 2) // X-axis frustums
        {
            minPos.x = main_axis_min;
            maxPos.x = main_axis_max;
            minPos.y = other_axis_min_1;
            maxPos.y = other_axis_max_1;
            minPos.z = other_axis_min_2;
            maxPos.z = other_axis_max_2;
        }
        else if (region == 3 || region == 4) // Y-axis frustums
        {
            minPos.y = main_axis_min;
            maxPos.y = main_axis_max;
            minPos.x = other_axis_min_1;
            maxPos.x = other_axis_max_1;
            minPos.z = other_axis_min_2;
            maxPos.z = other_axis_max_2;
        }
        else if (region == 5 || region == 6) // Z-axis frustums
        {
            minPos.z = main_axis_min;
            maxPos.z = main_axis_max;
            minPos.x = other_axis_min_1;
            maxPos.x = other_axis_max_1;
            minPos.y = other_axis_min_2;
            maxPos.y = other_axis_max_2;
        }
    }

    // Convert to world coordinates
    minPos += cameraPosW;
    maxPos += cameraPosW;

    // AABB-Sphere intersection test
    vec3 closestPoint = clamp(surfel.position, minPos, maxPos);
    float distanceSquared = dot(closestPoint - surfel.position, closestPoint - surfel.position);
    return distanceSquared <= surfel.radius * surfel.radius;
}

bool isCellValid(ivec4 cellPos)
{
    int k = cellPos.x;
    int u = cellPos.y;
    int v = cellPos.z;
    int region = cellPos.w;

    if (region < 0 || region > 6)
    {
        return false;
    }

    if (region == 0)
    {
        if (k < 0 || k >= n) return false;
        if (u < 0 || u >= n) return false;
        if (v < 0 || v >= n) return false;
        return true;
    }
    else
    {
        if (k < 0 || k >= m) return false;
        if (u < 0 || u >= n) return false;
        if (v < 0 || v >= n) return false;
        return true;
    }
}
