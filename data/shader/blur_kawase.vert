layout (location = 0) in vec2 inVertex;
layout (location = 1) in vec2 inTexCoord;
layout (location = 2) in vec4 inVertexColor;

noperspective out vec2 texCoord;

void main()
{
	gl_Position = vec4(inVertex, 0.0, 1.0);
	texCoord = inTexCoord;
}
