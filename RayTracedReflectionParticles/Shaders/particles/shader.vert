#version 450

layout(binding = 0) uniform UniformBufferObjectMVP
{
    mat4 view;
    mat4 projection;
    mat4 invViewRot;
} uboMVP;

// Vertex
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inTexCoord;

out gl_PerVertex
{
    vec4 gl_Position;
};

layout(location = 0) out vec2 outTexCoord;

void main() 
{
    vec4 worldPos = vec4(inPosition, 1.0);
    gl_Position = uboMVP.projection * uboMVP.view * worldPos;

    outTexCoord = inTexCoord;
}
