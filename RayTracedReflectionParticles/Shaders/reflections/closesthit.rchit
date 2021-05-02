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
layout(location = 1) rayPayloadNV bool isShadowed;
hitAttributeNV vec3 attribs;

layout(binding = 0, set = 0) uniform accelerationStructureNV topLevelAS;
layout(binding = 4, set = 0) buffer Vertices { vec4 v[]; } vertices;
layout(binding = 5, set = 0) buffer Indices { uint i[]; } indices;

layout(binding = 8, set = 0) uniform sampler textureSampler;
layout(binding = 9, set = 0) uniform texture2D[] textureImages;

layout(binding = 13, set = 0) uniform LightProperties
{
  mat4 model;
  vec4 directionDirectionalLight;
	vec4 colorDirectionalLight;
} light;

uint vertexSize = 3;

struct Vertex
{
  vec3 pos;
  vec3 norm;
  vec3 tangent;
  vec2 texCoord;
  uint matIndex;
};

Vertex unpackVertex(uint index)
{
  Vertex v;

  vec4 d0 = vertices.v[vertexSize * index + 0];
  vec4 d1 = vertices.v[vertexSize * index + 1];
  vec4 d2 = vertices.v[vertexSize * index + 2];

  v.pos = d0.xyz;
  v.norm = vec3(d0.w, d1.x, d1.y);
  v.tangent = vec3(d1.z, d1.w, d2.x);
  v.texCoord = vec2(d2.y, d2.z);
  v.matIndex = floatBitsToInt(d2.w);

  return v;
}

float DistributionGGX(vec3 N, vec3 H, float roughness);
float GeometrySchlickGGX(float NdotV, float roughness);
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness);
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness);

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
  vec3 normal = v0.norm * barycentrics.x + v1.norm * barycentrics.y + v2.norm * barycentrics.z;
  vec3 tangent = v0.tangent * barycentrics.x + v1.tangent * barycentrics.y + v2.tangent * barycentrics.z;
  vec3 position = v0.pos * barycentrics.x + v1.pos * barycentrics.y + v2.pos * barycentrics.z;
  position = vec3(light.model * vec4(position, 1.0));

  // Normal calculation
  mat3 usedModelMatrix = transpose(inverse(mat3(light.model)));
  vec3 n = normalize(usedModelMatrix * normal);
	vec3 t = normalize(usedModelMatrix * tangent);
	t = normalize(t - dot(t, n) * n);
	vec3 b = normalize(cross(t, n));
	mat3 tbn = inverse(mat3(t, b, n));

  vec3 calculatedNormal = normalize((texture(sampler2D(textureImages[v0.matIndex * 5 + 1], textureSampler), texCoord).rgb * 2.0 - vec3(1.0)) * tbn);

  // Get data from textures
  vec3 albedo = sRGBToLinear(texture(sampler2D(textureImages[v0.matIndex * 5], textureSampler), texCoord).rgb);
  float roughness = texture(sampler2D(textureImages[v0.matIndex * 5 + 2], textureSampler), texCoord).r;
  float metalness = texture(sampler2D(textureImages[v0.matIndex * 5 + 3], textureSampler), texCoord).r;
  float ao = texture(sampler2D(textureImages[v0.matIndex * 5 + 4], textureSampler), texCoord).r;

  // Shadows 
  vec3 origin = gl_WorldRayOriginNV + gl_WorldRayDirectionNV * gl_HitTNV;
  float tmin = 0.001;
  float tmax = 100.0;
  isShadowed = true; // default is true, miss shader will set to false
  traceNV(topLevelAS, 
          gl_RayFlagsTerminateOnFirstHitNV|gl_RayFlagsOpaqueNV|gl_RayFlagsSkipClosestHitShaderNV, 
          0xFF, 0 /* sbtRecordOffset */, 0 /* sbtRecordStride */,
          1 /* missIndex */, origin, tmin, -light.directionDirectionalLight.xyz, tmax, 1 /*payload location*/);

  // Direct lighting calculations
  vec3 V = normalize(gl_WorldRayOriginNV - position);
  vec3 R = reflect(-V, calculatedNormal);

  vec3 F0 = vec3(0.04);
  F0 = mix(F0,albedo, metalness);

  vec3 Lo = vec3(0.0);
  vec3 L = normalize(-light.directionDirectionalLight.xyz);
  vec3 H = normalize(V + L);
  vec3 radiance = light.colorDirectionalLight.xyz;

  // cook-torrance brdf
  float NDF = DistributionGGX(calculatedNormal, H, roughness);
  float G   = GeometrySmith(calculatedNormal, V, L, roughness);
  vec3 F    = fresnelSchlickRoughness(max(dot(H, V), 0.0), F0, roughness);

  vec3 kS = F;
  vec3 kD = vec3(1.0) - kS;
  kD *= 1.0 - metalness;

  vec3 nominator    = NDF * G * F;
  float denominator = 4 * max(dot(calculatedNormal, V), 0.0) * max(dot(calculatedNormal, L), 0.0);
  vec3 specular     = nominator / max(denominator, 0.001);

  // add to outgoing radiance Lo
  float NdotL = max(dot(calculatedNormal, L), 0.0);
  Lo += (kD * albedo / PI + specular) * radiance * NdotL;

  if(isShadowed)
    Lo = albedo * 0.1;
  
  payload.value = Lo;
  payload.rayLength = gl_HitTNV;
}

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a      = roughness*roughness;
    float a2     = a*a;
    float NdotH  = max(dot(N, H), 0.0);
    float NdotH2 = NdotH*NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - cosTheta, 5.0);
}
