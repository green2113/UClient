noperspective in vec2 texCoord;

uniform sampler2D gTextureSampler;
uniform vec2 gTexelSize;
uniform float gOffset;

out vec4 FragClr;
void main()
{
	vec2 Offset = gTexelSize * gOffset;
	vec4 Color = texture(gTextureSampler, texCoord + vec2(Offset.x, Offset.y));
	Color += texture(gTextureSampler, texCoord + vec2(-Offset.x, Offset.y));
	Color += texture(gTextureSampler, texCoord + vec2(Offset.x, -Offset.y));
	Color += texture(gTextureSampler, texCoord + vec2(-Offset.x, -Offset.y));
	FragClr = Color * 0.25;
}
