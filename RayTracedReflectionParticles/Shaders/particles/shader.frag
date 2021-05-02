#version 450

layout (binding = 1) uniform sampler2D tex;

layout(location = 0) in vec2 inTexCoords;

layout(location = 0) out vec4 outColor;

vec3 sRGBToLinear(vec3 color)
{
	if (length(color) <= 0.04045)
    	return color / 12.92;
	else
		return pow((color + vec3(0.055)) / 1.055, vec3(2.4));
}

void main() 
{
	vec4 inputColor = texture(tex, inTexCoords).rgba;
	if(inputColor.a < 0.8)
		discard;
	outColor = vec4(inputColor.rgb * vec3(2.0, 2.0, 0.1), inputColor.a);
}
