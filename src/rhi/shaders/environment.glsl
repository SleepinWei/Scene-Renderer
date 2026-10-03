layout(set=0,binding=0) uniform sampler2D skyRadianceLut;
layout(set=0,binding=6) uniform sampler2D skyIrradianceLut;
layout(set=0,binding=1,std140) uniform SkyData {mat4 inverseViewProjection;vec4 skyCamera;vec4 skySettings;};
vec2 skyUV(vec3 d){return vec2(mod(atan(d.x,-d.z)+6.2831853,6.2831853)/6.2831853,.5+.5*sign(d.y)*sqrt(abs(asin(clamp(d.y,-1.,1.)))/(1.5707963+.2)));}
vec3 environmentAmbient(vec3 p,vec3 N,vec3 V,vec3 F0,float roughness,vec3 albedo,float metallic,float ao){
    if(skySettings.x==0)return cameraAmbient.w*albedo*ao;
    vec3 F=F0+(max(vec3(1.-roughness),F0)-F0)*pow(1.-max(dot(N,V),0),5.);
    vec3 diffuse=texture(skyIrradianceLut,skyUV(N)).rgb*albedo*(1.-F)*(1.-metallic);
    vec3 R=reflect(-V,N),T=normalize(abs(R.y)<.99?cross(R,vec3(0,1,0)):cross(R,vec3(1,0,0))),B=cross(R,T),reflection=vec3(0);
    for(int i=0;i<8;i++){float a=float(i)*2.3999632;vec3 d=normalize(R+(T*cos(a)+B*sin(a))*roughness*roughness*.8);reflection+=texture(skyRadianceLut,skyUV(d)).rgb/8.;}
    return (diffuse+reflection*F)*ao+cameraAmbient.w*albedo*ao;
}
#define RHI_AMBIENT environmentAmbient
