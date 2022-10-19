#version 330
layout(location=0) in vec3 aPos;
layout(location=1) in vec2 TexCoords;

out vec2 texCoords;

void main(){
    texCoords = TexCoords;
    gl_Position = vec4(aPos,1.0f);
}
