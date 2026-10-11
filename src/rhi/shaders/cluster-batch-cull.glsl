float screenError(ClusterMesh mesh,ClusterNode n){
    if(n.lowError.w==0.0)return 0.0;
    vec3 center=(n.lowError.xyz+n.high.xyz)*0.5,extent=(n.high.xyz-n.lowError.xyz)*0.5;
    float depth=-(mesh.view*vec4(center,1)).z-dot(abs(vec3(mesh.view[0].z,mesh.view[1].z,mesh.view[2].z)),extent);
    return n.lowError.w*mesh.projection.x*mesh.projection.y/(mesh.guard.z!=0.0?1.0:max(depth,mesh.projection.z));
}
bool visible(ClusterMesh mesh,GeometryCluster c){
    if(mesh.counts.w==0u)return true;
    vec3 center=(c.low.xyz+c.high.xyz)*0.5,extent=(c.high.xyz-c.low.xyz)*0.5;
    vec4 p=mesh.vp*vec4(center,1);
    vec4 ex=abs(mesh.vp[0])*extent.x+abs(mesh.vp[1])*extent.y+abs(mesh.vp[2])*extent.z;
    // Support function of all 8 box corners; retain near/eye intersections.
    for(uint plane=0u;plane<6u;++plane){
        vec4 row=plane==0u?vec4(mesh.vp[0].w*mesh.guard.x+mesh.vp[0].x,mesh.vp[1].w*mesh.guard.x+mesh.vp[1].x,mesh.vp[2].w*mesh.guard.x+mesh.vp[2].x,mesh.vp[3].w*mesh.guard.x+mesh.vp[3].x):
          plane==1u?vec4(mesh.vp[0].w*mesh.guard.x-mesh.vp[0].x,mesh.vp[1].w*mesh.guard.x-mesh.vp[1].x,mesh.vp[2].w*mesh.guard.x-mesh.vp[2].x,mesh.vp[3].w*mesh.guard.x-mesh.vp[3].x):
          plane==2u?vec4(mesh.vp[0].w*mesh.guard.y+mesh.vp[0].y,mesh.vp[1].w*mesh.guard.y+mesh.vp[1].y,mesh.vp[2].w*mesh.guard.y+mesh.vp[2].y,mesh.vp[3].w*mesh.guard.y+mesh.vp[3].y):
          plane==3u?vec4(mesh.vp[0].w*mesh.guard.y-mesh.vp[0].y,mesh.vp[1].w*mesh.guard.y-mesh.vp[1].y,mesh.vp[2].w*mesh.guard.y-mesh.vp[2].y,mesh.vp[3].w*mesh.guard.y-mesh.vp[3].y):
          plane==4u?vec4(mesh.vp[0].z,mesh.vp[1].z,mesh.vp[2].z,mesh.vp[3].z):
          vec4(mesh.vp[0].w-mesh.vp[0].z,mesh.vp[1].w-mesh.vp[1].z,mesh.vp[2].w-mesh.vp[2].z,mesh.vp[3].w-mesh.vp[3].z);
        float upper=dot(row,vec4(center,1))+dot(abs(row.xyz),extent);
        float margin=2e-5*(dot(abs(row.xyz),abs(center)+extent)+abs(row.w)+1.0);
        margin+=plane<2u?mesh.rounding.x+mesh.guard.x*mesh.rounding.w:
            plane<4u?mesh.rounding.y+mesh.guard.y*mesh.rounding.w:
            plane==4u?mesh.rounding.z:mesh.rounding.z+mesh.rounding.w;
        if(upper < -margin)return false;
    }
    return true;
}
