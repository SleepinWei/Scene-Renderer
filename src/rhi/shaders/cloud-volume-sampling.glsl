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
ivec2 cloudVoxelTexel(ivec3 p,int n){int columns=n==128?16:8;return ivec2(p.z%columns,p.z/columns)*(n+2)+p.xy+1;}
ivec2 cloudBrickTexel(ivec3 p){return ivec2(p.z%8,p.z/8)*32+p.xy;}
vec3 cloudVolumeUV(vec3 world){return (world-cloudVolumeCenterMode.xyz-vec3(cloudWindHistory.x,0,cloudWindHistory.y)*cloudCameraTime.w)/cloudVolumeSize.xyz+.5;}
