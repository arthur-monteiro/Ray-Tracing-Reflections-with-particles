#version 460
#extension GL_NV_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

struct CommonPayload
{
  // From closest hit values
  vec3 value;
  float rayLength;
};

layout(location = 0) rayPayloadInNV CommonPayload payload;
hitAttributeNV vec3 attribs;

layout(binding = 1, set = 0) uniform accelerationStructureNV topLevelAS;
layout(binding = 6, set = 0) buffer Vertices { float v[]; } vertices;
layout(binding = 7, set = 0) buffer Indices { uint i[]; } indices;

layout(binding = 14, set = 0) uniform sampler textureSampler;
layout(binding = 15, set = 0) uniform texture2D textureImage;

uint vertexSize = 5;

struct Vertex
{
  vec3 pos;
  vec2 texCoord;
};

Vertex unpackVertex(uint index)
{
  Vertex v;

  float d0 = vertices.v[vertexSize * index + 0];
  float d1 = vertices.v[vertexSize * index + 1];
  float d2 = vertices.v[vertexSize * index + 2];
  float d3 = vertices.v[vertexSize * index + 3];
  float d4 = vertices.v[vertexSize * index + 4];

  v.pos = vec3(d0, d1, d2);
  v.texCoord = vec2(d3, d4);

  return v;
}

vec3 sRGBToLinear(vec3 color)
{
	if (length(color) <= 0.04045)
    	return color / 12.92;
	else
		return pow((color + vec3(0.055)) / 1.055, vec3(2.4));
}

const float PI = 3.14159265359;

void main()
{
  // Get vertices
  ivec3 ind = ivec3(indices.i[3 * gl_PrimitiveID], indices.i[3 * gl_PrimitiveID + 1],
                    indices.i[3 * gl_PrimitiveID + 2]);

  Vertex v0 = unpackVertex(ind.x);
  Vertex v1 = unpackVertex(ind.y);
  Vertex v2 = unpackVertex(ind.z);

  const vec3 barycentrics = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

  // Get data for hit point
  vec2 texCoord = v0.texCoord * barycentrics.x + v1.texCoord * barycentrics.y + v2.texCoord * barycentrics.z;
  vec3 position = v0.pos * barycentrics.x + v1.pos * barycentrics.y + v2.pos * barycentrics.z;

  // Get data from textures
  vec4 inputColor = texture(sampler2D(textureImage, textureSampler), texCoord);
  if(inputColor.a > 0.8)
  {
    payload.value = inputColor.rgb * vec3(2.0, 2.0, 0.1);
    payload.rayLength = gl_HitTNV;
  }
}