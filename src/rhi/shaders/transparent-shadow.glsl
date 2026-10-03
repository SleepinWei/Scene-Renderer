layout(set=2,binding=1,std140) uniform ShadowData {
    mat4 shadowCameraView;mat4 shadowMatrices[180];vec4 shadowRects[180];ivec4 shadowLights[30];vec4 shadowSplits[30];vec4 shadowSettings;mat4 rsmMatrix;vec4 rsmRect;vec4 rsmSettings;
};
layout(set=2,binding=4) uniform sampler2D shadowAtlas;
int shadowTile(int light,vec3 position){
    ivec4 info=shadowLights[light];int tile=info.x;
    if(info.z==0){float depth=abs((shadowCameraView*vec4(position,1)).z);vec4 s=shadowSplits[light];tile+=depth>s.w?4:depth>s.z?3:depth>s.y?2:depth>s.x?1:0;}
    else if(info.z==1){vec3 d=position-lights[light].positionType.xyz,a=abs(d);tile+=a.x>=a.y&&a.x>=a.z?(d.x>0?0:1):a.y>=a.z?(d.y>0?2:3):(d.z>0?4:5);}
    return tile;
}
float lightVisibility(int light,vec3 position,vec3 N,vec3 L){
    if(shadowSettings.y==0)return 1;
    int tile=shadowTile(light,position);vec4 q=shadowMatrices[tile]*vec4(position,1);
    if(q.w<=0)return 1;vec3 p=q.xyz/q.w;if(p.z<0||p.z>1||any(greaterThan(abs(p.xy),vec2(1))))return 1;
    vec4 rect=shadowRects[tile];vec2 center=(p.xy*vec2(.5,-.5)+.5)*rect.zw+rect.xy, pixel=1./vec2(textureSize(shadowAtlas,0));
    vec2 dx=dFdx(center),dy=dFdy(center);float zx=dFdx(p.z),zy=dFdy(p.z),det=dx.x*dy.y-dx.y*dy.x;
    vec2 gradient=abs(det)>1e-12?vec2(zx*dy.y-zy*dx.y,zy*dx.x-zx*dy.x)/det:vec2(0);
    float bias=shadowSettings.x*max(1.-dot(N,L),.2)+dot(abs(gradient),pixel)*.6, sum=0;
    for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){
        vec2 coord=clamp(center+vec2(x,y)*pixel,rect.xy+pixel*.5,rect.xy+rect.zw-pixel*.5);
        sum+=p.z+dot(gradient,coord-center)-bias<=texture(shadowAtlas,coord).r?1:0;
    }return sum/9.;
}

#define RHI_VISIBILITY lightVisibility
