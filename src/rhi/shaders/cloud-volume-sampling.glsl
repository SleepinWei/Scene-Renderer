// A real XYZ voxel volume packed in a 2D atlas for the shared RHI.
// Density uses a one-voxel apron; values outside the authored box are zero.
vec4 cloudVolumeSample(sampler2D atlas,vec3 uv,int n) {
    if(any(lessThan(uv,vec3(0))) || any(greaterThan(uv,vec3(1))))return vec4(0);
    int columns=n==128?16:8;vec3 p=uv*float(n)-.5;float z=floor(p.z);vec4 value=vec4(0);
    for(int i=0;i<2;i++){
        int slice=clamp(int(z)+i,0,n-1);vec2 tile=vec2(slice%columns,slice/columns);
        vec2 at=(tile*float(n+2)+1.+p.xy+.5)/vec2(textureSize(atlas,0));
        value+=textureLod(atlas,at,0)*(i==0?1.-fract(p.z):fract(p.z));
    }return value;
}
// Potential is filtered before thresholding, so occupancy must cover every
// positive potential; the green mask only marks saturated potential cores.
float cloudBaseDensity(vec4 sampleValue,float coverage){return clamp((sampleValue.b*.425-(1.-coverage)*.3)*8.,0.,1.);}
float cloudCoverageFade(float coverage){return smoothstep(0.,.05,coverage);}
float cloudCachedDensity(vec4 sampleValue){
    return cloudCoverageControl.x>.5?cloudBaseDensity(sampleValue,cloudLayer.z)*cloudCoverageFade(cloudLayer.z):sampleValue.r;
}
// Four atlas panels, each RGBA storing four coverage nodes (16 total).
vec4 cloudCoverageLightSample(sampler2D atlas,vec3 uv,int panel){
    if(any(lessThan(uv,vec3(0)))||any(greaterThan(uv,vec3(1))))return vec4(0);
    vec3 p=uv*64.-.5;float z=floor(p.z);vec4 value=vec4(0);
    for(int i=0;i<2;++i){int slice=clamp(int(z)+i,0,63);vec2 tile=vec2(slice%8,slice/8);
        vec2 at=(vec2(panel*528,0)+tile*66.+1.+p.xy+.5)/vec2(textureSize(atlas,0));
        value+=textureLod(atlas,at,0)*(i==0?1.-fract(p.z):fract(p.z));
    }return value;
}
float cloudCachedTau(sampler2D atlas,vec3 uv){
    if(cloudCoverageControl.x<.5)return cloudVolumeSample(atlas,uv,64).r;
    float knot=clamp(cloudLayer.z,0.,1.)*15.;int i=min(int(knot),14),panel=i/4,channel=i%4;
    vec4 a=cloudCoverageLightSample(atlas,uv,panel);
    float next=channel==3?cloudCoverageLightSample(atlas,uv,panel+1).r:a[min(channel+1,3)];
    return mix(a[channel],next,knot-float(i))*cloudCoverageFade(cloudLayer.z);
}
ivec2 cloudVoxelTexel(ivec3 p,int n){int columns=n==128?16:8;return ivec2(p.z%columns,p.z/columns)*(n+2)+p.xy+1;}
ivec2 cloudBrickTexel(ivec3 p){return ivec2(p.z%8,p.z/8)*32+p.xy;}
vec3 cloudVolumeUV(vec3 world){return (world-cloudVolumeCenterMode.xyz-vec3(cloudAdvection.x,0,cloudAdvection.y))/cloudVolumeSize.xyz+.5;}
