#version 450
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texcoord;
layout(set=0,binding=0,std140) uniform CameraVertex { mat4 viewProjection; };
layout(set=0,binding=1,std140) uniform ObjectData { mat4 model; mat4 normalMatrix; };
layout(location=0) out vec3 worldPosition;
layout(location=1) out vec3 worldNormal;
layout(location=2) out vec2 uv;
void main() {
    vec4 world = model * vec4(position,1);
    worldPosition = world.xyz;
    worldNormal = normalize(mat3(normalMatrix) * normal);
    uv = texcoord;
    gl_Position = viewProjection * world;
}
