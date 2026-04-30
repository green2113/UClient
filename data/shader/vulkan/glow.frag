#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) noperspective in vec2 glowCoord;
layout(location = 1) noperspective in vec4 glowColor;

layout(push_constant) uniform SGlowBO {
	layout(offset = 32) vec2 gRectSize;
	float gGlowRadius;
	float gGlowStrength;
} gGlowBO;

layout(location = 0) out vec4 FragClr;
void main()
{
	vec2 HalfSize = gGlowBO.gRectSize * 0.5;
	vec2 Point = glowCoord - HalfSize;
	vec2 DistToEdge = abs(Point) - HalfSize;
	float SignedDistance = length(max(DistToEdge, vec2(0.0))) + min(max(DistToEdge.x, DistToEdge.y), 0.0);

	float GlowAlpha = 1.0;
	if(SignedDistance > 0.0)
	{
		float Radius = max(gGlowBO.gGlowRadius, 0.001);
		GlowAlpha = (1.0 - smoothstep(0.0, Radius, SignedDistance)) * gGlowBO.gGlowStrength;
	}

	FragClr = vec4(glowColor.rgb, glowColor.a * clamp(GlowAlpha, 0.0, 1.0));
}
