#version 450
#extension GL_ARB_separate_shader_objects : enable

// Fog client: one triangle that covers the screen, for the post-process pass
layout(location = 0) noperspective out vec2 vUV;

void main()
{
	vec2 Pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	vUV = Pos;
	gl_Position = vec4(Pos * 2.0 - 1.0, 0.0, 1.0);
}
