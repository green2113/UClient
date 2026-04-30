noperspective in vec2 glowCoord;
noperspective in vec4 glowColor;

uniform vec2 gRectSize;
uniform float gGlowRadius;
uniform float gGlowStrength;

out vec4 FragClr;
void main()
{
	vec2 HalfSize = gRectSize * 0.5;
	vec2 Point = glowCoord - HalfSize;
	vec2 DistToEdge = abs(Point) - HalfSize;
	float SignedDistance = length(max(DistToEdge, vec2(0.0))) + min(max(DistToEdge.x, DistToEdge.y), 0.0);

	float GlowAlpha = 1.0;
	if(SignedDistance > 0.0)
	{
		float Radius = max(gGlowRadius, 0.001);
		GlowAlpha = (1.0 - smoothstep(0.0, Radius, SignedDistance)) * gGlowStrength;
	}

	FragClr = vec4(glowColor.rgb, glowColor.a * clamp(GlowAlpha, 0.0, 1.0));
}
