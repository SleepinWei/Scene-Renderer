#version 430 core
layout(early_fragment_tests) in;

layout(location=0) out vec4 gDepth;

in float Depth;
void main(){
    gDepth = vec4(vec3(abs(Depth)),1.0f); 
}