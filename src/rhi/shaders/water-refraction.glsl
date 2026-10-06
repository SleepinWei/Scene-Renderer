float waterPixelFootprint(vec3 point) {
    return 2.*max(-(view*vec4(point,1)).z,.1)/max(coastBedInfo.z,1.);
}
bool waterCandidate(vec3 origin,vec3 V,vec3 ray,float range,vec2 uv,float nearT,float farT,out float distance,out float confidence) {
    if(!submerged(uv,origin,V))return false;
    vec3 position=waterQueryPosition(uv).xyz;
    float t=dot(position-origin,ray),tolerance=waterPixelFootprint(position)*1.5+.002;
    float error=length(position-(origin+ray*t));
    ivec2 size=waterQuerySize(),pixel=clamp(ivec2(uv*vec2(size)),ivec2(0),size-1);
    vec4 px=waterQueryTexel(min(pixel+ivec2(1,0),size-1)),py=waterQueryTexel(min(pixel+ivec2(0,1),size-1));
    vec3 dx=px.xyz-position,dy=py.xyz-position,n=cross(dx,dy);
    if(px.w>.5&&py.w>.5&&length(dx)<tolerance*6.&&length(dy)<tolerance*6.&&dot(n,n)>1e-14) {
        n=normalize(n);float denom=dot(n,ray);
        if(abs(denom)>.005){float planeT=dot(n,position-origin)/denom;vec2 projected=screenUV(origin+ray*planeT)*vec2(size);
            if(max(abs(projected.x-float(pixel.x)-.5),abs(projected.y-float(pixel.y)-.5))<1.) {t=planeT;error=0.;}}
    }
    if(t<max(0.,nearT-tolerance*2.)||t>min(range,farT+tolerance*2.)||error>tolerance)return false;
    distance=t;confidence=clamp(1.-error/max(tolerance,.001),.25,1.);return true;
}
// Perspective-correct interpolation of Q=world/clip.w and k=1/clip.w
// advances through contiguous pixels along the dominant projected axis.
bool waterDDA(vec3 origin,vec3 V,vec3 ray,float range,out vec2 hitUV,out float distance,out float confidence) {
    vec3 first=origin+ray*.01,last=origin+ray*range;
    vec4 h0=waterFragmentVP*vec4(first,1),h1=waterFragmentVP*vec4(last,1);
    float vz=(view*vec4(ray,0)).z;
    if(vz>0.) {float limit=(-.05-(view*vec4(origin,1)).z)/vz;range=min(range,max(limit,.01));last=origin+ray*range;h1=waterFragmentVP*vec4(last,1);}
    if(h0.w<=0.||h1.w<=0.)return false;
    vec2 size=vec2(waterQuerySize()),p0=screenUV(first)*size,p1=screenUV(last)*size,delta=p1-p0;
    float extent=max(abs(delta.x),abs(delta.y)),k0=1./h0.w,k1=1./h1.w;
    vec3 q0=first*k0,q1=last*k1;
    if(extent<.5) {
        hitUV=(floor(p0)+.5)/size;return waterCandidate(origin,V,ray,range,hitUV,0.,range,distance,confidence);
    }
    float traveled=0.;vec2 step=delta/extent;
    for(int iteration=0;iteration<2048;++iteration) {
        if(traveled>=extent)break;
        vec2 pixel=p0+step*traveled;
        if(any(lessThan(pixel,vec2(0)))||any(greaterThanEqual(pixel,size)))break;
        float stride=1.;
        if(coastBedInfo.w>.5) {
            // Advance to this mip cell's exit, never a whole cell from an
            // arbitrary starting point. Use exact min/max texels, not bilinear
            // filtering, which is not conservative for depth intervals.
            int mip=min(5,textureQueryLevels(sceneNormal)-1);float cell=exp2(float(mip));
            vec2 offset=vec2(queryAboveWater&&volume.w>.5?size.x:0.,0.);
            vec2 tile=floor((pixel+step*.0001+offset)/cell);
            vec2 boundary=(tile+vec2(step.x>=0.?1.:0.,step.y>=0.?1.:0.))*cell-offset;
            vec2 exitDistance=vec2(1e20);
            if(abs(step.x)>1e-6)exitDistance.x=(boundary.x-pixel.x)/step.x;
            if(abs(step.y)>1e-6)exitDistance.y=(boundary.y-pixel.y)/step.y;
            float advance=min(extent-traveled,max(min(exitDistance.x,exitDistance.y),.001));
            if(advance>1.) {
                ivec2 mipSize=textureSize(sceneNormal,mip),at=clamp(ivec2(tile),ivec2(0),mipSize-1);
                vec4 interval=texelFetch(sceneNormal,at,mip);
                float a=traveled/extent,b=(traveled+advance)/extent;
                vec3 qa=mix(q0,q1,a)/mix(k0,k1,a),qb=mix(q0,q1,b)/mix(k0,k1,b);
                float da=-(view*vec4(qa,1)).z,db=-(view*vec4(qb,1)).z,tolerance=max(waterPixelFootprint(qa),waterPixelFootprint(qb))*2.+.002;
                if(interval.z<.5||max(da,db)<interval.x-tolerance||min(da,db)>interval.y+tolerance)stride=advance;
            }
        }
        if(stride<=1.) {
            float a=traveled/extent,b=min(traveled+1.,extent)/extent;
            vec3 qa=mix(q0,q1,a)/mix(k0,k1,a),qb=mix(q0,q1,b)/mix(k0,k1,b);
            vec2 uv=(floor(p0+step*min(traveled+.5,extent))+.5)/size;
            if(waterCandidate(origin,V,ray,range,uv,dot(qa-origin,ray),dot(qb-origin,ray),distance,confidence)){hitUV=uv;return true;}
        }
        traveled+=max(stride,.001);
    }
    return false;
}
bool waterBed(vec2 xz,out float height,out vec3 base) {
    vec3 local=(coastInverse*vec4(xz.x,0,xz.y,1)).xyz;vec2 uv=local.xz*.5+.5;
    if(coastBedInfo.x<.5||any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1))))return false;
    vec2 size=vec2(textureSize(bathymetryMap,0));vec4 data=textureLod(bathymetryMap,(uv*(size-1.)+.5)/size,0);
    height=(coastModel*vec4(local.x,data.r,local.z,1)).y;base=pow(max(data.gba,vec3(0)),vec3(2.2));return true;
}
vec3 waterBedNormal(vec2 xz,float height) {
    float dx=max(length(coastModel[0].xz)*2./float(max(textureSize(bathymetryMap,0).x-1,1)),.05);
    float dz=max(length(coastModel[2].xz)*2./float(max(textureSize(bathymetryMap,0).y-1,1)),.05);
    float a,b,c,d;vec3 ignored;
    if(!waterBed(xz-vec2(dx,0),a,ignored))a=height;if(!waterBed(xz+vec2(dx,0),b,ignored))b=height;
    if(!waterBed(xz-vec2(0,dz),c,ignored))c=height;if(!waterBed(xz+vec2(0,dz),d,ignored))d=height;
    return normalize(vec3(-(b-a)/(2.*dx),1,-(d-c)/(2.*dz)));
}
bool waterBedRay(vec3 origin,vec3 ray,float range,out float distance,out vec3 base,out vec3 normal) {
    // Reject rays outside the finite heightfield before the 64-step fallback.
    // Clip in the same XZ transform used by waterBed, including rotated/scaled beds.
    vec2 local=(coastInverse*vec4(origin.x,0,origin.z,1)).xz;
    vec2 direction=(coastInverse*vec4(ray.x,0,ray.z,0)).xz;
    float entry=0.,exit=range;
    for(int axis=0;axis<2;++axis){
        if(abs(direction[axis])<1e-7){if(abs(local[axis])>1.)return false;}
        else {float a=(-1.-local[axis])/direction[axis],b=(1.-local[axis])/direction[axis];entry=max(entry,min(a,b));exit=min(exit,max(a,b));}
    }
    if(entry>=exit)return false;
    float previous=entry;bool above=false;float oldHeight;vec3 oldBase;vec3 first=origin+ray*entry;
    above=waterBed(first.xz,oldHeight,oldBase)&&first.y>oldHeight;
    for(int i=1;i<=64;++i) {
        float t=mix(entry,exit,float(i)/64.);vec3 point=origin+ray*t;float height;vec3 color;
        bool valid=waterBed(point.xz,height,color);
        if(valid&&above&&point.y<=height) {
            float low=previous,high=t;
            for(int j=0;j<8;++j){float middle=(low+high)*.5;vec3 q=origin+ray*middle;float bed;vec3 c;
                if(waterBed(q.xz,bed,c)&&q.y<=bed)high=middle;else low=middle;}
            distance=(low+high)*.5;point=origin+ray*distance;waterBed(point.xz,height,base);
            normal=waterBedNormal(point.xz,height);return true;
        }
        above=valid&&point.y>height;previous=t;
    }
    return false;
}
float waterSlabLut(float tau,float albedo,float g,float noL) {
    float t=log(1.+clamp(tau,0.,32.))/log(33.)*15.,w=(1.-sqrt(1.-clamp(albedo,0.,1.)))*15.;
    float gi=clamp(g,0.,.85)/.85*4.,a=clamp((noL-.1)/.9,0.,1.)*3.;
    float result=0.;
    for(int ai=0;ai<2;++ai)for(int gg=0;gg<2;++gg) {
        float aa=min(floor(a)+float(ai),3.),gb=min(floor(gi)+float(gg),4.);
        float weight=(ai==0?1.-fract(a):fract(a))*(gg==0?1.-fract(gi):fract(gi));
        float row=(aa*5.+gb)*16.+w;
        result+=weight*textureLod(multipleScatterLut,vec2((t+.5)/16.,(row+.5)/320.),0).r;
    }
    return result;
}
