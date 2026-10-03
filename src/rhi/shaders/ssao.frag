#version 450
layout(location=0) in vec2 uv;layout(location=0) out vec4 color;
layout(set=0,binding=0,std140) uniform EffectsParameters {mat4 effectView;mat4 effectProjection;vec4 aoSettings;};
layout(set=1,binding=1) uniform sampler2D positionBuffer;
layout(set=1,binding=2) uniform sampler2D normalBuffer;
void main(){
    vec4 world=texture(positionBuffer,uv);if(world.w==0||aoSettings.w==0){color=vec4(1);return;}
    vec3 p=(effectView*vec4(world.xyz,1)).xyz,N=normalize(mat3(effectView)*texture(normalBuffer,uv).xyz);
    vec3 tangent=normalize(abs(N.z)<.99?cross(N,vec3(0,0,1)):cross(N,vec3(0,1,0))),bitangent=cross(N,tangent);
    float occlusion=0,valid=0;
    for(int i=0;i<24;i++){
        float t=(float(i)+.5)/24., phi=float(i)*2.39996323, z=t,r=sqrt(1.-z*z);
        vec3 offset=(tangent*cos(phi)*r+bitangent*sin(phi)*r+N*z)*aoSettings.x*mix(.1,1.,t*t),sampleP=p+offset;
        vec4 clip=effectProjection*vec4(sampleP,1);if(clip.w<=0)continue;
        vec2 at=clip.xy/clip.w*vec2(.5,-.5)+.5;if(any(lessThan(at,vec2(0)))||any(greaterThan(at,vec2(1))))continue;
        vec4 q=texture(positionBuffer,at);if(q.w==0)continue;
        float depth=(effectView*vec4(q.xyz,1)).z;float range=smoothstep(0.,1.,aoSettings.x/max(abs(p.z-depth),.0001));
        occlusion+=(depth>=sampleP.z+aoSettings.y?1:0)*range;valid+=1;
    }
    float ao=pow(clamp(1.-occlusion/max(valid,1.),0.,1.),aoSettings.z);color=vec4(ao,ao,ao,1);
}
