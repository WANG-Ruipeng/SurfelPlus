// ref: https://github.com/Apress/ray-tracing-gems/blob/master/Ch_25_Hybrid_Rendering_for_Real-Time_Ray_Tracing/MultiscaleMeanEstimator.hlsl


vec3 MSME(vec3 y, inout MSMEData data, float shortWindowBlend)
{
    vec3 mean = data.mean;
    vec3 shortMean = data.shortMean;
    float vbbr = data.vbbr;
    vec3 variance = data.variance;
    float inconsistency = data.inconsistency;

    // suppress fireflies.
    {
        vec3 dev = sqrt(max(vec3(1e-5), variance));
        vec3 highThreshold = 0.1 + shortMean + dev * 8.0;
        vec3 overflow = max(vec3(0.0), y - highThreshold);
        y -= overflow;
    }

    vec3 delta = y - shortMean;
    shortMean = mix(shortMean, y, shortWindowBlend);
    vec3 delta2 = y - shortMean;

    float varianceBlend = shortWindowBlend * 0.5;
    variance = mix(variance, delta * delta2, varianceBlend);
    variance = max(vec3(1e-3), variance);
    vec3 dev = sqrt(max(vec3(1e-5), variance));

    vec3 shortDiff = mean - shortMean;

    float relativeDiff = dot(vec3(0.299, 0.587, 0.114), abs(shortDiff) / max(vec3(1e-5), dev));
    inconsistency = mix(inconsistency, relativeDiff, 0.08);

    float varianceBasedBlendReduction = clamp(dot(vec3(0.299, 0.587, 0.114), 0.5 * shortMean / max(vec3(1e-5), dev)), 1.0 / 32, 1.0);

    float catchUpBlend = clamp(smoothstep(0, 1, relativeDiff * max(0.02, inconsistency - 0.2)), 1.0 / 256, 1.0);
    catchUpBlend *= vbbr;

    vbbr = mix(vbbr, varianceBasedBlendReduction, 0.1);
    mean = mix(mean, y, clamp(catchUpBlend, 0.0, 1.0));

    // Output
    data.mean = mean;
    data.shortMean = shortMean;
    data.vbbr = vbbr;
    data.variance = variance;
    data.inconsistency = inconsistency;

    return mean;
}