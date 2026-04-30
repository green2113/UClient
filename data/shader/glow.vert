layout (location = 0) in vec2 inVertex;
layout (location = 1) in vec2 inGlowCoord;
layout (location = 2) in vec4 inVertexColor;

uniform mat4x2 gPos;

noperspective out vec2 glowCoord;
noperspective out vec4 glowColor;

void main()
{
	gl_Position = vec4(gPos * vec4(inVertex, 0.0, 1.0), 0.0, 1.0);
	glowCoord = inGlowCoord;
	glowColor = inVertexColor;
}
