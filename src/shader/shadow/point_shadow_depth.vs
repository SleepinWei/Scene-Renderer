#version 460 core
layout(location=0) in vec3 aPos;
layout(location=2) in vec2 aUV;
out vec2 VertexUV;

uniform mat4 model;

void main()
{
    VertexUV = aUV;
    gl_Position = model * vec4(aPos, 1.0);
}
