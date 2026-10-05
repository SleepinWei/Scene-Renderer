#version 450
#extension GL_GOOGLE_include_directive : require
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
#include "ao-common.glsl"
const float AO_PI=3.14159265359;
float legacySsao(vec3 p,vec3 N) {
    vec3 tangent=normalize(abs(N.z)<.99?cross(N,vec3(0,0,1)):cross(N,vec3(0,1,0))),bitangent=cross(N,tangent);
    float occlusion=0.,valid=0.;
    for(int i=0;i<24;i++) {
        float t=(float(i)+.5)/24.,phi=float(i)*2.39996323,z=t,r=sqrt(1.-z*z);
        vec3 offset=(tangent*cos(phi)*r+bitangent*sin(phi)*r+N*z)*aoSettings.x*mix(.1,1.,t*t),sampleP=p+offset;
        vec4 clip=effectProjection*vec4(sampleP,1);if(clip.w<=0.)continue;
        vec2 at=clip.xy/clip.w*vec2(.5,-.5)+.5;
        if(any(lessThan(at,vec2(0)))||any(greaterThan(at,vec2(1))))continue;
        vec4 q=texture(positionBuffer,at);if(q.w==0.)continue;
        float depth=aoViewPosition(q.xyz).z,range=smoothstep(0.,1.,aoSettings.x/max(abs(p.z-depth),.0001));
        occlusion+=(depth>=sampleP.z+aoSettings.y?1.:0.)*range;valid+=1.;
    }
    return clamp(1.-occlusion/max(valid,1.),0.,1.);
}
// Integral of cos(theta-gamma)*abs(sin(theta)), split at zero.
float arcPrimitive(float theta,float gamma) {return -.25*cos(2.*theta-gamma)+.5*theta*sin(gamma);}
float visibleArc(float low,float high,float gamma) {
    return max(arcPrimitive(low,gamma)+arcPrimitive(high,gamma)-2.*arcPrimitive(0.,gamma),0.);
}
float horizonAo(ivec2 pixel,vec3 p,vec3 N) {
    ivec2 size=textureSize(positionBuffer,0);
    bool ortho=abs(effectProjection[3][3])>.5;
    vec3 V=ortho?vec3(0,0,1):normalize(-p);if(dot(N,V)<0.)N=-N;
    float radius=aoSettings.x,clipW=(effectProjection*vec4(p,1)).w;if(clipW<=0.)return 1.;
    vec2 projectedRadius=abs(vec2(effectProjection[0][0],effectProjection[1][1]))*radius*.5/clipW;
    int slices=clamp(int(aoOptions.y),2,8),steps=clamp(int(aoOptions.z),2,8);
    // Stable spatial rotation; no frame-dependent noise or AO history.
    float noise=fract(52.9829189*fract(dot(vec2(pixel),vec2(.06711056,.00583715))));
    float visible=0.,unoccluded=0.;
    for(int slice=0;slice<slices;++slice) {
        float phi=(float(slice)+noise)*AO_PI/float(slices);vec2 direction=vec2(cos(phi),sin(phi));
        vec3 D=vec3(direction.x,-direction.y,0),T=normalize(D-V*dot(D,V)),axis=normalize(cross(V,T));
        vec3 projectedN=N-axis*dot(N,axis);float normalLength=length(projectedN);if(normalLength<1e-6)continue;
        float gamma=atan(dot(projectedN,T),dot(projectedN,V));
        float low=max(-AO_PI*.5,gamma-AO_PI*.5),high=min(AO_PI*.5,gamma+AO_PI*.5);
        vec2 baseline=vec2(cos(low),cos(high)),horizon=baseline;
        for(int side=0;side<2;++side)for(int step=0;step<steps;++step) {
            float t=(float(step)+.5+.25*noise)/float(steps);vec2 offset=direction*projectedRadius*t*t;
            float pixels=length(offset*vec2(size));offset*=max(1.,1./max(pixels,1e-6));
            vec2 at=(vec2(pixel)+.5)/vec2(size)+(side==0?-offset:offset);
            // Off-screen taps are open, never repeated/clamped border occluders.
            if(any(lessThan(at,vec2(0)))||any(greaterThanEqual(at,vec2(1))))continue;
            vec4 sampleWorld=texelFetch(positionBuffer,ivec2(at*vec2(size)),0);if(sampleWorld.w==0.)continue;
            vec3 delta=aoViewPosition(sampleWorld.xyz)-p;float distance2=dot(delta,delta);
            if(distance2<1e-10||distance2>=radius*radius||dot(delta,N)<=aoSettings.y)continue;
            float falloff=1.-smoothstep(radius*radius*.36,radius*radius,distance2);
            float sampleCos=dot(delta,V)*inversesqrt(distance2);
            horizon[side]=max(horizon[side],mix(baseline[side],sampleCos,falloff));
        }
        float h0=clamp(-acos(clamp(horizon.x,-1.,1.)),low,0.),h1=clamp(acos(clamp(horizon.y,-1.,1.)),0.,high);
        visible+=normalLength*visibleArc(h0,h1,gamma);unoccluded+=normalLength*visibleArc(low,high,gamma);
    }
    // Normalize against the same unobstructed slices, avoiding finite-slice
    // darkening on isolated inclined planes and near screen edges.
    return clamp(visible/max(unoccluded,1e-6),0.,1.);
}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);vec4 world=texelFetch(positionBuffer,pixel,0);
    if(world.w==0.||aoSettings.w==0.||aoSettings.x<=0.) {color=vec4(1);return;}
    vec3 p=aoViewPosition(world.xyz);
    float ao=aoOptions.x>.5?horizonAo(pixel,p,aoGeometryNormal(pixel,p)):legacySsao(p,aoViewNormal(pixel));
    color=vec4(ao,ao,ao,1);
}
