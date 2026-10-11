#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord;
layout(set=0,binding=0,std140) uniform CameraVertex { mat4 viewProjection; };
layout(set=0,binding=1,std140) uniform ObjectData { mat4 model; mat4 normalMatrix; };
layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 worldNormal;
layout(location=2) out vec2 uv;
layout(location=3) out vec3 worldTangent;
layout(location=4) out vec3 worldBitangent;
layout(location=5) out vec3 normalTangent;
layout(location=6) out vec3 normalBitangent;
layout(location=7) out vec3 rawWorldNormal;
layout(set=0,binding=6,std430) readonly buffer MeshTangents {vec4 tangentInfo;vec4 cornerTangents[];};
void main() {
    vec4 world = model * vec4(position,1);
    worldPosition = world.xyz;
    worldNormal = normalize(mat3(normalMatrix) * normal);
    uv = texcoord;
    rawWorldNormal=mat3(normalMatrix)*normal;normalTangent=vec3(0);normalBitangent=vec3(0);
    worldTangent=vec3(0);worldBitangent=vec3(0);
    if(tangentInfo.x>0){
        vec4 frame=cornerTangents[gl_VertexIndex];
        if(frame.w!=0 && dot(frame.xyz,frame.xyz)>1e-12){
            normalTangent=mat3(normalMatrix)*frame.xyz;
            normalBitangent=mat3(normalMatrix)*(cross(normal,frame.xyz)*frame.w);
            worldTangent=mat3(model)*frame.xyz;
            worldBitangent=mat3(model)*(cross(normalize(normal),frame.xyz)*frame.w);
        }
    }
    gl_Position = viewProjection * world;
}
