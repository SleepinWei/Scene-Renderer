#version 330
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 TexCoords;

uniform mat4 projection;
uniform mat4 view;
uniform mat4 model;

out vec3 fragColor; 
out vec3 normal;

void main(){
    fragColor = vec3(TexCoords,1);
    normal = mat3(transpose(inverse(model))) * aNormal;
    gl_Position = projection * view * model * vec4(aPos,1.0);
}
