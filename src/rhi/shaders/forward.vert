#version 450
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
void main() {
    vec4 world = model * vec4(position,1);
    worldPosition = world.xyz;
    worldNormal = normalize(mat3(normalMatrix) * normal);
    uv = texcoord;
    rawWorldNormal=mat3(normalMatrix)*normal;normalTangent=vec3(0);normalBitangent=vec3(0);
    worldTangent=vec3(0);worldBitangent=vec3(0);
    gl_Position = viewProjection * world;
}
