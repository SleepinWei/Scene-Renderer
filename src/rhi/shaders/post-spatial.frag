#version 450
layout(location=0) in vec2 uv;
layout(location=0) out vec4 color;
layout(set=0,binding=0,std140) uniform SpatialParameters {
    mat4 inverseVP,currentView,previousVP,stableVP;
    vec4 lens; // focus,range,radius,mode (0 DOF, 1 camera blur)
    vec4 motion; // shutter,max pixels,history,unused
};
layout(set=0,binding=1) uniform sampler2D sourceColor;
layout(set=0,binding=2) uniform sampler2D sceneDepth;
vec4 world(vec2 p){float d=texture(sceneDepth,p).r;vec4 w=inverseVP*vec4(p*vec2(2,-2)+vec2(-1,1),d,1);return w/max(abs(w.w),1e-8)*sign(w.w);}
float distanceAt(vec2 p){return max(-(currentView*world(p)).z,.001);}
void main(){
    vec3 center=texture(sourceColor,uv).rgb;
    vec2 pixel=1./vec2(textureSize(sourceColor,0));float z=distanceAt(uv);
    vec3 sum=center;float weight=1;
    if(lens.w<.5){
        float radius=lens.z*smoothstep(lens.y*.5,lens.y,abs(z-lens.x));
        for(int i=0;i<32;++i){
            float angle=float(i)*2.39996323;vec2 offset=vec2(cos(angle),sin(angle))*sqrt((float(i)+.5)/32.)*radius*pixel;
            vec2 p=clamp(uv+offset,pixel*.5,vec2(1)-pixel*.5);float q=distanceAt(p);
            // Prevent defocused background bleeding across foreground silhouettes.
            float w=q>z?exp(-abs(q-z)/max(z*.05,.1)):1.;
            sum+=texture(sourceColor,p).rgb*w;weight+=w;
        }
    }else if(motion.z>.5 && texture(sceneDepth,uv).r<.999999){
        vec4 w=world(uv),old=previousVP*w,current=stableVP*w;
        if(old.w>0. && current.w>0.){
            vec2 velocity=((old.xy/old.w-current.xy/current.w)*vec2(.5,-.5))*motion.x;
            float pixels=length(velocity/pixel);velocity*=min(1.,motion.y/max(pixels,1e-5));
            for(int i=0;i<12;++i){
                vec2 p=clamp(uv+velocity*((float(i)+.5)/12.-.5),pixel*.5,vec2(1)-pixel*.5);
                float w=exp(-abs(distanceAt(p)-z)/max(z*.1,.1));
                sum+=texture(sourceColor,p).rgb*w;weight+=w;
            }
        }
    }
    color=vec4(min(max(sum/weight,vec3(0)),vec3(65000)),1);
}
