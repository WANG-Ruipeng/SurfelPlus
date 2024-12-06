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

ivec4 getCellPosNonUniform(vec3 posW, vec3 cameraPosW)
{
    vec3 posC = posW - cameraPosW;

    // Determine region
    int region = 0;
    float half_d = d / 2.0;
	int half_n = n / 2;
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

    float s = abs(main_axis_pos) - half_d;
    int k = int(log(1 - n * s * (1 - p) / d) / log(p));
    int u = int(ceil(other_axis_pos_1 * n * 0.5 / main_axis_pos)) + half_n;
    int v = int(ceil(other_axis_pos_2 * n * 0.5 / main_axis_pos)) + half_n;

    if (u > 0) u--;
	if (v > 0) v--;

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
	int half_n = n / 2;

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

        if (u < half_n) u = u - half_n;
		else u = u - half_n + 1;

        if (v < half_n) v = v - half_n;
        else v = v - half_n + 1;

        // Compute s0 and s1
        float s0 = delta * (1.0 - pow(p, float(k))) / (1.0 - p);
        float s1 = delta * (1.0 - pow(p, float(k + 1))) / (1.0 - p);

        float main_axis_min;
        float main_axis_max;

        if (region % 2 == 0) // Negative direction
        {
            main_axis_min = -(half_d + s1);
            main_axis_max = -(half_d + s0);
        }
		else { // Positive direction
			main_axis_min = half_d + s0;
			main_axis_max = half_d + s1;
        }

        // Other axis 1
        float other_axis_a_1 = 0.0f;
        float other_axis_b_1 = 0.0f;
        float other_axis_c_1 = 0.0f;
        float other_axis_d_1 = 0.0f;
        if (u > 0) {
            other_axis_a_1 =  2.0 * (u - 1) / n * (half_d + main_axis_min);
            other_axis_b_1 =  2.0 * u / n * (half_d + main_axis_max);
            other_axis_c_1 = 2.0 * (u - 1) / n * (half_d + main_axis_max);
            other_axis_d_1 = 2.0 * u / n * (half_d + main_axis_min);
        }
        else {
            other_axis_a_1 = 2.0 * (u + 1) / n * (half_d + main_axis_min);
            other_axis_b_1 = 2.0 * u / n * (half_d + main_axis_max);
            other_axis_c_1 = 2.0 * (u + 1) / n * (half_d + main_axis_max);
            other_axis_d_1 = 2.0 * u / n * (half_d + main_axis_min);
        }

        float other_axis_min_1 = min(min(other_axis_a_1, other_axis_b_1), min(other_axis_c_1, other_axis_d_1));
        float other_axis_max_1 = max(max(other_axis_a_1, other_axis_b_1), max(other_axis_c_1, other_axis_d_1));

        // Other axis 2
        float other_axis_a_2 = 0.0f;
        float other_axis_b_2 = 0.0f;
        float other_axis_c_2 = 0.0f;
        float other_axis_d_2 = 0.0f;

        if (v > 0) {
            other_axis_a_2 = 2.0 * (v - 1) / n * (half_d + main_axis_min);
            other_axis_b_2 = 2.0 * v / n * (half_d + main_axis_max);
            other_axis_c_2 = 2.0 * (v - 1) / n * (half_d + main_axis_max);
            other_axis_d_2 = 2.0 * v / n * (half_d + main_axis_min);
        }
        else {
            other_axis_a_2 = 2.0 * (v + 1) / n * (half_d + main_axis_min);
            other_axis_b_2 = 2.0 * v / n * (half_d + main_axis_max);
            other_axis_c_2 = 2.0 * (v + 1) / n * (half_d + main_axis_max);
            other_axis_d_2 = 2.0 * v / n * (half_d + main_axis_min);
        }

        // Calculate min and max for Other axis 2
        float other_axis_min_2 = min(min(other_axis_a_2, other_axis_b_2), min(other_axis_c_2, other_axis_d_2));
        float other_axis_max_2 = max(max(other_axis_a_2, other_axis_b_2), max(other_axis_c_2, other_axis_d_2));

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
    return distanceSquared <= surfel.radius * surfel.radius * surfel.radius;
    //return true;
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


float getSurfelMaxSize(float distance) {
    if (distance < d / 2.0) {
		return d * 0.5f / float(n);
    }
    else {
		float s = distance - d / 2.0;
        int k = int(log(1 - n * s * (1 - p) / d) / log(p));
		return d * 0.5f / float(n) * pow(p, float(k));
    }
}