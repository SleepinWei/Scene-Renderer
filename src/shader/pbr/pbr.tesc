#version 410 core

layout(vertices=3) out;

uniform mat4 model;
// uniform mat4 view; 
layout(std140) uniform VP{
    mat4 projection;
    mat4 view;
};

struct Object{
    vec3 Position; 
    vec3 Normal; 
    vec2 TexCoords;
};
out Object object_ts[]; 
in Object object_tc[];

float computeLOD(float dist){
    const float MIN_LEVEL = 1; 
    const float MAX_LEVEL = 10; 
    const float MIN_DIST = 0.2; 
    const float MAX_DIST = 1; 

    float level;
    if(dist < MIN_DIST)
        level = MAX_LEVEL;
    else if (dist > MAX_DIST) 
        level =  MIN_LEVEL;
    else{
        float u = (dist-MIN_DIST)/(MAX_DIST-MIN_DIST);
        level = (u * MIN_LEVEL + (1-u) * MAX_LEVEL);
    }
    return level;
}

void main(){
    // gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;
    // object_ts[gl_InvocationID] = object_tc[gl_InvocationID];
    object_ts[gl_InvocationID].Position = object_tc[gl_InvocationID].Position;
    object_ts[gl_InvocationID].Normal = object_tc[gl_InvocationID].Normal;
    object_ts[gl_InvocationID].TexCoords = object_tc[gl_InvocationID].TexCoords;

    if (gl_InvocationID == 0){
        float dist0 = length((view * vec4(object_tc[0].Position,1.0f)).xyz); 
        float dist1 = length((view * vec4(object_tc[1].Position,1.0f)).xyz);
        float dist2 = length((view * vec4(object_tc[2].Position,1.0f)).xyz);

        float level[3]; 
        level[0] = computeLOD(dist0);
        level[1] = computeLOD(dist1);
        level[2] = computeLOD(dist2);
        // object_ts[gl_InvocationID].TexCoords = vec2(level[gl_InvocationID]/5.0f);

        gl_TessLevelOuter[2] = max(level[0],level[1]);
        gl_TessLevelOuter[0] = max(level[1],level[2]);
        gl_TessLevelOuter[1] = max(level[0],level[2]);

        gl_TessLevelInner[0] = max(max(level[0],max(level[1],level[2])),1.0f);
    }
}