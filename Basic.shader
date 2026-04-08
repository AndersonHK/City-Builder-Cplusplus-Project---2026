#shader vertex
#version 460 core

layout(location = 0) in vec4 position;
layout(location = 1) in vec2 newVariable;

out vec2 colors;

void main()
{
	gl_Position = position;
	colors = newVariable;
};

#shader fragment
#version 460 core

layout(location = 0) out vec4 color;

in vec2 colors;

//uniform vec4 u_Color;

void main()
{
	color = vec4(colors.x, colors.y, 0.0, 1.0);
};