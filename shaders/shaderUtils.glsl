
float hash13(vec3 p3)
{
    p3 = fract(p3 * .1031);
    p3 += dot(p3, p3.zyx + 31.32);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 WorldPosFromDepth(in vec2 uv, in float depth)
{
    float z = depth * 2.0 - 1.0;
    vec4 clipSpacePosition = vec4(uv * 2.0 - 1.0, z, 1.0);
    vec4 viewSpacePosition = sceneCamera.projInverse * clipSpacePosition;
    vec4 worldSpacePosition = sceneCamera.viewInverse * viewSpacePosition;

    return worldSpacePosition.xyz;
}