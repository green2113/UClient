#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) noperspective in vec2 blurCoord;
layout(location = 1) noperspective in vec4 blurColor;

layout(push_constant) uniform SBlurBO {
	layout(offset = 32) vec2 gRectSize;
	float gRounding;
	float gBlurRadius;
	float gBlurStrength;
} gBlurBO;

layout(location = 0) out vec4 FragClr;
void main()
{
	FragClr = vec4(0.0);
}
