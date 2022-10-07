#version 430 core 
layout(location=0) in vec3 aPos; 

out vec3 TexCoords;

// uniform mat4 projection; 
layout(std140,binding=0) uniform VP{
   mat4 projection; 
   mat4 view_; 
};

uniform mat4 view;  // not the same view as view_ 

void main(){
    TexCoords = aPos; 
    vec4 temp = projection * view * vec4(aPos,1.0);
    gl_Position = temp.xyww;
}