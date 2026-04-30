noperspective in vec2 blurCoord;
noperspective in vec4 blurColor;

uniform sampler2D gTextureSampler;
uniform vec2 gTextureSize;
uniform vec2 gRectSize;
uniform float gRounding;
uniform float gBlurRadius;
uniform float gBlurStrength;

out vec4 FragClr;
void main()
{
	float Radius = max(gBlurRadius, 0.001);
	float Rounding = max(gRounding, 0.0);
	vec2 HalfSize = gRectSize * 0.5;
	vec2 Point = blurCoord - HalfSize;
	vec2 RoundedSize = max(HalfSize - vec2(Rounding), vec2(0.0));
	vec2 DistToRound = abs(Point) - RoundedSize;
	float SignedDistance = length(max(DistToRound, vec2(0.0))) + min(max(DistToRound.x, DistToRound.y), 0.0) - Rounding;
	float EdgeAlpha = 1.0 - smoothstep(0.0, Radius, SignedDistance);

	vec2 ScreenUv = gl_FragCoord.xy / max(gTextureSize, vec2(1.0));
	vec4 Blurred = texture(gTextureSampler, ScreenUv);
	vec3 Tinted = mix(Blurred.rgb, blurColor.rgb, blurColor.a);
	float Alpha = clamp(EdgeAlpha * gBlurStrength, 0.0, 1.0);

	FragClr = vec4(Tinted, Alpha);
}
