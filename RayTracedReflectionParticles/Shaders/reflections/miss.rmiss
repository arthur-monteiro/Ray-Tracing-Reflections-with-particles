#version 460
#extension GL_NV_ray_tracing : require

struct CommonPayload
{
    // From closest hit values
    vec3 value;
    float rayLength;
};

layout(location = 0) rayPayloadInNV CommonPayload payload;

void main()
{
    payload.value = vec3(0.0);
}