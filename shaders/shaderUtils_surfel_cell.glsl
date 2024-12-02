#include "shaderUtil_grid.glsl"
// Surfel and cells

vec3 getCameraPosition(SceneCamera camera)
{
    return vec3(camera.viewInverse[3]);
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
    float angle = sqrt(area / 3.14159265359) * fovy * 2.0 / max(resolution.x, resolution.y);
    return distance * tan(angle);
}

float calcSurfelRadius(float distance, float fovy, vec2 resolution) {
    return calcRadiusApprox(surfelSize, distance, fovy, resolution);
}

vec3 calcCellIndirectLighting(vec3 camPos, vec3 worldPos, vec3 worldNor)
{
    //vec3 cellPosIndex = getCellPos(worldPos, camPos);
    //uint flattenIndex = getFlattenCellIndex(cellPosIndex);

    ivec4 cellPosIndex = getCellPosNonUniform(worldPos, camPos);
    uint flattenIndex = getFlattenCellIndexNonUniform(cellPosIndex);

    CellInfo cellInfo = cellBuffer[flattenIndex];
    uint cellOffset = cellInfo.surfelOffset;
    uint cellSurfelCount = cellInfo.surfelCount;

	vec3 indirectLighting = vec3(0.0f);

    for (uint i = 0; i < cellSurfelCount; i++)
    {
        //uint surfelIndex = surfelAlive[i];
        uint surfelIndex = cellToSurfel[cellOffset + i];
        Surfel surfel = surfelBuffer[surfelIndex];
        vec3 bias = worldPos - surfel.position;
        float dist2 = dot(bias, bias);

        if (dist2 < surfel.radius * surfel.radius)
        {
            vec3 surfelNor = decompress_unit_vec(surfel.normal);
            float dotN = dot(worldNor, surfelNor);
            if (dotN > 0.f)
            {
                float dist = sqrt(dist2);
                float contribution = 1.f;

                contribution *= clamp(dotN, 0.f, 1.f);
                contribution *= clamp(1.f - dist / surfel.radius, 0.f, 1.f);
                contribution = smoothstep(0, 1, contribution);

                indirectLighting += surfel.radiance * contribution * smoothstep(0.f, 50.f, float(surfelRecycleInfo[surfelIndex].frame));
            }

        }
    }

    return indirectLighting;
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



#ifdef LAYOUTS_GLSL

bool finalizePathWithSurfel(vec3 worldPos, vec3 worldNor, inout vec4 irradiance)
{
    irradiance = vec4(0.0f);
    vec3 camPos = getCameraPosition(sceneCamera);
    //vec3 cellPosIndex = getCellPos(worldPos, camPos);
    ivec4 cellPosIndex = getCellPosNonUniform(worldPos, camPos);
    if (!isCellValid(cellPosIndex))
        return false;

    uint flattenIndex = getFlattenCellIndexNonUniform(cellPosIndex);

    CellInfo cellInfo = cellBuffer[flattenIndex];
    uint cellOffset = cellInfo.surfelOffset;
    uint cellSurfelCount = cellInfo.surfelCount;

    float coverage = 0.f;
    float maxContribution = 0.f;
    uint maxContributionSleepingSurfelIndex = 0xffffffff;

    const uint searchRange = min(16, cellInfo.surfelCount);
	uint searchCnt = 0;

    for (uint i = 0; i < cellInfo.surfelCount; i++)
    {
        if (searchCnt == searchRange) break;
        uint surfelIndex = cellToSurfel[cellOffset + i];
        Surfel surfel = surfelBuffer[surfelIndex];
        vec3 neiNor = decompress_unit_vec(surfel.normal);
        bool isSleeping = (surfelRecycleInfo[surfelIndex].status & 0x0001) != 0;
        vec3 bias = surfel.position - worldPos;
		float dist = length(bias);
        float cosineTheta = dot(bias, worldNor) / dist;
        if (cosineTheta < -0.2 || dot(-bias, neiNor) / dist < -0.2)
            continue;

        if (dist < surfel.radius)
        {
            vec3 surfelNor = decompress_unit_vec(surfel.normal);
            float dotN = dot(worldNor, surfelNor);
            float contribution = 1.f;
            if (dotN > 0.f)
            {

                contribution *= clamp(dotN, 0.f, 1.f);
                contribution *= clamp(1.f - dist / surfel.radius, 0.f, 1.f);
                contribution = smoothstep(0, 1, contribution);

                irradiance += vec4(surfel.radiance, 1.f) * contribution;
				coverage += contribution;
                if (maxContribution < contribution && isSleeping)
                {
                    maxContribution = contribution;
                    maxContributionSleepingSurfelIndex = surfelIndex;
                }

                /*if (isSleeping)
                {
                    sleepingCoverage += contribution;
                    if (maxContribution < contribution)
                    {
                        maxContribution = contribution;
                        maxContributionSleepingSurfelIndex = surfelIndex;
                    }
                }*/
            }
            else
            {
                contribution *= max(cosineTheta, 0.f);
                contribution *= pow(1.f - dist / surfel.radius, 2.0);
                irradiance += vec4(surfel.radiance, 1.f) * contribution;
            }
			searchCnt++;
            surfelRecycleInfo[surfelIndex].status |= 0x0004u;
        }


    }

	if (irradiance.w > 0.1f)
	{
		irradiance /= irradiance.w;
	}

    //uint randSeed = initRandom(uvec2(rtxState.totalFrames, floatBitsToUint(worldPos.x)),
    //    uvec2(floatBitsToUint(worldPos.y), floatBitsToUint(worldPos.z)), rtxState.frame);
    //
    // spawn sleeping surfel if coverage is low.
    //if (surfelCounter.aliveSurfelCnt < kMaxSurfelCount &&
    //    coverage < 1.f && cellInfo.surfelCount < 32)
    //{
    //    uint surfelAliveIndex = atomicAdd(surfelCounter.aliveSurfelCnt, 1);
    //    if (surfelAliveIndex < kMaxSurfelCount && rand(randSeed) < 0.2)
    //    {
    //        uint surfelID = surfelDead[kMaxSurfelCount - surfelAliveIndex - 1];
    //        surfelAlive[surfelAliveIndex] = surfelID;

    //        Surfel newSurfel;
    //        //newSurfel.objID = objID;
    //        newSurfel.position = worldPos;
    //        newSurfel.normal = compress_unit_vec(worldNor);
    //        newSurfel.radiance = irradiance.xyz;
    //        newSurfel.rayCount = 0;
    //        newSurfel.msmeData.mean = irradiance.xyz;
    //        newSurfel.msmeData.shortMean = irradiance.xyz;
    //        newSurfel.msmeData.vbbr = 0.f;
    //        newSurfel.msmeData.variance = vec3(1.f);
    //        newSurfel.msmeData.inconsistency = 1.f;
    //        float surfelToCameraDistance = distance(worldPos, getCameraPosition(sceneCamera));
    //        newSurfel.radius = min(calcSurfelRadius(surfelToCameraDistance, sceneCamera.fov, vec2(rtxState.size)), getSurfelMaxSize(surfelToCameraDistance) * 2.f);

    //        surfelBuffer[surfelID] = newSurfel;

    //        SurfelRecycleInfo newSurfelRecycleInfo;
    //        newSurfelRecycleInfo.life = kMaxLife / 2;
    //        newSurfelRecycleInfo.frame = 0;
    //        newSurfelRecycleInfo.status = 1u;
    //        surfelRecycleInfo[surfelID] = newSurfelRecycleInfo;
    //    }
    //    else
    //    {
    //        atomicAdd(surfelCounter.aliveSurfelCnt, -1);
    //    }
    //}

    //if (maxContributionSleepingSurfelIndex != 0xffffffff && coverage > 3.f)
    //{
    //    
    //    if (rand(randSeed) < 0.2)
    //    {
    //        surfelBuffer[maxContributionSleepingSurfelIndex].radius = 0.f;
    //    }
    //}

    return true;

}

vec3 surfelPathTrace(Ray r, int maxDepth, uint surfelIndex, inout float firstDepth)
{
    vec3 radiance = vec3(0.0);
    vec3 throughput = vec3(1.0);
    vec3 absorption = vec3(0.0);
    ShadeState sstate;
    bool valid = true;
    int depth;

    for (depth = 0; depth < maxDepth; depth++)
    {
        valid = true;
        ClosestHit(r);
        if (depth == 0)
        {
            firstDepth = prd.hitT;
        }

        // Hitting the environment
        if (prd.hitT == INFINITY)
        {
            vec3 env;
            if (_sunAndSky.in_use == 1)
                env = sun_and_sky(_sunAndSky, r.direction);
            else
            {
                vec2 uv = GetSphericalUv(r.direction);  // See sampling.glsl
                env = texture(environmentTexture, uv).rgb;
            }
            // Done sampling return
            return radiance + (env * rtxState.hdrMultiplier * throughput);
        }


        BsdfSampleRec bsdfSampleRec;

        // Get Position, Normal, Tangents, Texture Coordinates, Color
        sstate = GetShadeState(prd);

        State state;
        state.position = sstate.position;
        state.normal = sstate.normal;
        state.tangent = sstate.tangent_u[0];
        state.bitangent = sstate.tangent_v[0];
        state.texCoord = sstate.text_coords[0];
        state.matID = sstate.matIndex;
        state.isEmitter = false;
        state.specularBounce = false;
        state.isSubsurface = false;
        state.ffnormal = dot(state.normal, r.direction) <= 0.0 ? state.normal : -state.normal;

        // Filling material structures
        GetMaterialsAndTextures(state, r);

        // Color at vertices
        state.mat.albedo *= sstate.color;

        // Debugging info
        /*if (rtxState.debugging_mode != eNoDebug && rtxState.debugging_mode < eRadiance)
            return DebugInfo(state);*/

        // KHR_materials_unlit
        if (state.mat.unlit)
        {
            return radiance + state.mat.albedo * throughput;
        }

        // Reset absorption when ray is going out of surface
        if (dot(state.normal, state.ffnormal) > 0.0)
        {
            absorption = vec3(0.0);
        }

        // Emissive material
        radiance += state.mat.emission * throughput;

        // Add absoption (transmission / volume)
        throughput *= exp(-absorption * prd.hitT);

        // Light and environment contribution
        VisibilityContribution vcontrib = DirectLight(r, state);
        vcontrib.radiance *= throughput;

        // Sampling for the next ray
        bsdfSampleRec.f = Sample(state, -r.direction, state.ffnormal, bsdfSampleRec.L, bsdfSampleRec.pdf, prd.seed);

        // Set absorption only if the ray is currently inside the object.
        if (dot(state.ffnormal, bsdfSampleRec.L) < 0.0)
        {
            absorption = -log(state.mat.attenuationColor) / vec3(state.mat.attenuationDistance);
        }

        if (bsdfSampleRec.pdf > 0.0)
        {
            throughput *= bsdfSampleRec.f * abs(dot(state.ffnormal, bsdfSampleRec.L)) / bsdfSampleRec.pdf;
        }
        else
        {
            valid = false;
            break;
        }

        // For Russian-Roulette (minimizing live state)
        /*float rrPcont = (depth >= RR_DEPTH) ?
            min(max(throughput.x, max(throughput.y, throughput.z)) * state.eta * state.eta + 0.001, 0.95) :
            1.0;*/

        // Next ray
        r.direction = bsdfSampleRec.L;
        r.origin = OffsetRay(sstate.position, dot(bsdfSampleRec.L, state.ffnormal) > 0 ? state.ffnormal : -state.ffnormal);

        // We are adding the contribution to the radiance only if the ray is not occluded by an object.
        // This is done here to minimize live state across ray-trace calls.
        if (vcontrib.visible == true)
        {
            // Shoot shadow ray up to the light (1e32 == environement)
            Ray  shadowRay = Ray(r.origin, vcontrib.lightDir);
            bool inShadow = AnyHit(shadowRay, vcontrib.lightDist);
            if (!inShadow)
            {
                radiance += vcontrib.radiance;
                valid = false;
            }
        }


        //if (rand(prd.seed) >= rrPcont)
        //    break;                // paths with low throughput that won't contribute
        //throughput /= rrPcont;  // boost the energy of the non-terminated paths
    }

	// use surfel indirect when the path reach max depth
    if (depth == maxDepth && valid)
    {
		vec3 surfelPos = surfelBuffer[surfelIndex].position;
		float radius = surfelBuffer[surfelIndex].radius;
		if (dot(sstate.position, surfelPos) < radius * radius)
		{
			vec4 irradiance = vec4(0.0);
			bool rst = finalizePathWithSurfel(sstate.position, sstate.normal, irradiance);
			if (rst)
			{
				radiance += irradiance.xyz * throughput;
			}
		}
    }


    return radiance;
}

#endif // LAYOUTS_GLSL