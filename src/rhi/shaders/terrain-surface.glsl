// Shared terrain vertex placement and grass attachment. The coarse lattice is
// anchored globally; touching leaves therefore agree even across LOD 0/5 seams.
int terrainInterval(ivec2 grid,int minimumInterval){
    ivec2 hi=clamp(grid/8,ivec2(0),ivec2(159)),lo=clamp((grid-ivec2(1))/8,ivec2(0),ivec2(159));
    int lod=0;for(int y=0;y<2;y++)for(int x=0;x<2;x++)lod=max(lod,int(round(imageLoad(lodMap,ivec2(x==0?lo.x:hi.x,y==0?lo.y:hi.y)).r*5.)));
    return max(minimumInterval,1<<lod);
}
ivec2 terrainStitch(ivec2 grid){int step=terrainInterval(grid,1);return ((grid+step/2)/step)*step;}
float terrainMorphHeight(vec2 uv,int interval,mat4 terrainModel,mat4 terrainView,mat4 terrainVP,vec4 screen){
    float fine=sampleHeight(uv);if(screen.w<.5)return fine;
    float step=float(interval*2)/1280.;vec2 origin=floor(uv/step)*step,at=clamp((uv-origin)/step,0.,1.);
    float a=sampleHeight(origin),b=sampleHeight(min(origin+vec2(step,0),1.)),c=sampleHeight(min(origin+vec2(step),1.)),d=sampleHeight(min(origin+vec2(0,step),1.));
    float coarse=at.x>=at.y?a+(b-a)*at.x+(c-b)*at.y:a+(d-a)*at.y+(c-d)*at.x;
    vec3 eye=(terrainView*terrainModel*vec4(uv.x*2.-1.,fine,uv.y*2.-1.,1)).xyz;
    mat4 projection=terrainVP*inverse(terrainView);float pixels=.5*max(abs(projection[0][0])*screen.x,abs(projection[1][1])*screen.y);
    if(abs(projection[3][3])<.5)pixels/=max(length(eye),.001);
    float footprint=max(length(terrainModel[0].xyz),length(terrainModel[2].xyz))*step*pixels;
    float blend=1.-smoothstep(screen.z*.5,screen.z,footprint);
    return mix(fine,coarse,blend);
}
vec3 terrainVertex(ivec2 grid,int interval,mat4 terrainModel,mat4 terrainView,mat4 terrainVP,vec4 screen){
    grid=terrainStitch(grid);vec2 uv=vec2(grid)/1280.;interval=terrainInterval(grid,interval);
    return vec3(uv.x*2.-1.,terrainMorphHeight(uv,interval,terrainModel,terrainView,terrainVP,screen),uv.y*2.-1.);
}
