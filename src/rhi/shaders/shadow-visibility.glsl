float shadowLinearDepth(float depth,vec4 projection){
    return projection.z>0.?mix(projection.x,projection.y,depth):
        projection.x*projection.y/max(projection.y-depth*(projection.y-projection.x),1e-8);
}
int shadowTile(int light,vec3 position){
    ivec4 info=shadowLights[light];int tile=info.x;
    if(info.z==0){float depth=-(shadowCameraView*vec4(position,1)).z;vec4 s=shadowSplits[light];tile+=depth>s.w?4:depth>s.z?3:depth>s.y?2:depth>s.x?1:0;}
    else if(info.z==1){vec3 d=position-lights[light].positionType.xyz,a=abs(d);tile+=a.x>=a.y&&a.x>=a.z?(d.x>0?0:1):a.y>=a.z?(d.y>0?2:3):(d.z>0?4:5);}
    return tile;
}
vec2 shadowDisk(int i,int count){
    float angle=float(i)*2.39996323;
    float radius=i==0?0.:sqrt((float(i)-.5)/float(count-1));
    return radius*vec2(cos(angle),sin(angle));
}
float shadowCompare(vec2 center,vec2 offset,float receiver,float bias,vec2 gradient,vec4 rect){
    ivec2 size=textureSize(shadowAtlas,0);
    vec2 p=(center+offset)*vec2(size)-.5,weight=fract(p);ivec2 base=ivec2(floor(p));
    ivec2 low=ivec2(round(rect.xy*vec2(size))),high=ivec2(round((rect.xy+rect.zw)*vec2(size)))-1;
    float taps[4];
    for(int i=0;i<4;i++){
        ivec2 at=clamp(base+ivec2(i%2,i/2),low,high);
        vec2 coord=(vec2(at)+.5)/vec2(size);
        taps[i]=receiver+dot(gradient,coord-center)-bias<=texelFetch(shadowAtlas,at,0).r?1.:0.;
    }
    return mix(mix(taps[0],taps[1],weight.x),mix(taps[2],taps[3],weight.x),weight.y);
}
float shadowTileVisibility(int light,int tile,vec3 position,vec3 planeNormal){
    vec4 q=shadowMatrices[tile]*vec4(position,1);if(q.w<=0.)return 1.;
    vec3 p=q.xyz/q.w;if(p.z<0. || p.z>1. || any(greaterThan(abs(p.xy),vec2(1))))return 1.;
    vec4 rect=shadowRects[tile],projection=shadowLightDepth[light];
    if(projection.z>0.)projection.z=2./length(vec3(shadowMatrices[tile][0][0],shadowMatrices[tile][1][0],shadowMatrices[tile][2][0]));
    vec2 pixel=1./vec2(textureSize(shadowAtlas,0)),center=(p.xy*vec2(.5,-.5)+.5)*rect.zw+rect.xy;
    // Analytic receiver plane: derivatives of projected coordinates across a
    // cascade boundary mix two different matrices and produce invalid bias.
    vec3 tangent=normalize(cross(planeNormal,abs(planeNormal.y)<.9?vec3(0,1,0):vec3(1,0,0)));
    vec3 bitangent=cross(planeNormal,tangent);
    vec4 a=shadowMatrices[tile]*vec4(tangent,0),b=shadowMatrices[tile]*vec4(bitangent,0);
    vec3 da=(a.xyz-p*a.w)/q.w,db=(b.xyz-p*b.w)/q.w;
    vec2 dx=da.xy*rect.zw*vec2(.5,-.5),dy=db.xy*rect.zw*vec2(.5,-.5);
    float determinant=dx.x*dy.y-dx.y*dy.x;
    vec2 gradient=abs(determinant)>1e-12?vec2(da.z*dy.y-db.z*dx.y,db.z*dx.x-da.z*dy.x)/determinant:vec2(0);
    float receiverDistance=shadowLinearDepth(p.z,projection);
    float depthPerMeter=projection.z>0.?1./(projection.y-projection.x):
        projection.x*projection.y/((projection.y-projection.x)*receiverDistance*receiverDistance);
    float bias=shadowSettings.x*depthPerMeter+dot(abs(gradient),pixel)*.5;
    float radius=1.;
    if(shadowFilter.x>.5){
        float tilePixels=rect.z/pixel.x;
        float search=projection.z>0.?tan(shadowFilter.y)*receiverDistance/projection.z:
            shadowFilter.z*(receiverDistance-projection.x)/(2.*projection.w*projection.x*receiverDistance);
        float searchPixels=clamp(search*tilePixels,1.,shadowFilter.w),sum=0.,count=0.;
        vec2 lo=rect.xy+pixel*.5,hi=rect.xy+rect.zw-pixel*.5;
        for(int i=0;i<24;i++){
            vec2 coord=clamp(center+shadowDisk(i,24)*searchPixels*pixel,lo,hi);
            float depth=textureLod(shadowAtlas,coord,0.).r;
            if(depth<p.z+dot(gradient,coord-center)-bias){sum+=shadowLinearDepth(depth,projection);count+=1.;}
        }
        if(count==0.)return 1.;
        float blocker=sum/count,separation=max(receiverDistance-blocker,0.);
        // Orthographic sun maps use angular size and world separation. Local
        // lights use emitter size and linear light-space distances, never raw Z.
        float penumbra=projection.z>0.?tan(shadowFilter.y)*separation/projection.z:
            shadowFilter.z*separation/(max(blocker,projection.x)*2.*projection.w*receiverDistance);
        radius=clamp(penumbra*tilePixels,1.,shadowFilter.w);
    }
    float visibility=0.;
    if(radius<=1.001){
        for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++)visibility+=shadowCompare(center,vec2(x,y)*pixel,p.z,bias,gradient,rect);
        return visibility/9.;
    }
    for(int i=0;i<32;i++)visibility+=shadowCompare(center,shadowDisk(i,32)*radius*pixel,p.z,bias,gradient,rect);
    return visibility/32.;
}
float lightVisibility(int light,vec3 position,vec3 N,vec3 L){
    if(shadowSettings.y==0. || shadowLights[light].y==0)return 1.;
#ifndef SHADOW_COMPUTE
    vec3 geometry=cross(dFdx(position),dFdy(position));
    if(dot(geometry,geometry)>1e-16)N=normalize(geometry);
#endif
    int tile=shadowTile(light,position),type=shadowLights[light].z;
    float depth=-(shadowCameraView*vec4(position,1)).z;
    if(type==0 && (depth<0. || depth>=shadowCascades.y))return 1.;
    float result=shadowTileVisibility(light,tile,position,N);
    if(type==0){
        int cascade=tile-shadowLights[light].x;
        if(cascade<4 && shadowCascades.z>0.){
            vec4 splits=shadowSplits[light];float end=splits[cascade],start=cascade==0?shadowCascades.x:splits[cascade-1];
            float weight=smoothstep(end-(end-start)*shadowCascades.z,end,depth);
            if(weight>0.)result=mix(result,shadowTileVisibility(light,tile+1,position,N),weight);
        }
        float start=mix(shadowSplits[light].w,shadowCascades.y,1.-shadowCascades.w);
        result=mix(result,1.,smoothstep(start,shadowCascades.y,depth));
    }
    return result;
}
#define RHI_VISIBILITY lightVisibility
