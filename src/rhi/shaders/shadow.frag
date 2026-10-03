#version 450
layout(location=0) in vec3 worldPosition;
layout(location=1) in vec3 worldNormal;
layout(location=2) in vec2 uv;
layout(location=0) out vec4 flux;
layout(location=1) out vec4 position;
layout(location=2) out vec4 normal;
layout(set=0,binding=2,std140) uniform RsmLight {vec4 lightPositionType;vec4 lightColorInner;vec4 lightDirectionOuter;vec4 captureSettings;};
layout(set=1,binding=0,std140) uniform MaterialData {vec4 albedoAlpha;vec4 factors;vec4 emissiveNormal;};
layout(set=1,binding=1) uniform sampler2D albedoMap;
layout(set=1,binding=3) uniform sampler2D metallicMap;
layout(set=1,binding=2) uniform sampler2D normalMap;
layout(set=0,binding=4) uniform sampler2D skyIrradiance;
vec2 skyUV(vec3 d){return vec2(mod(atan(d.x,-d.z)+6.2831853,6.2831853)/6.2831853,.5+.5*sign(d.y)*sqrt(abs(asin(clamp(d.y,-1.,1.)))/(1.5707963+.2)));}
vec3 mappedNormal() {
    vec3 N = normalize(worldNormal);
    vec3 q1 = dFdx(worldPosition), q2 = dFdy(worldPosition);
    vec2 t1 = dFdx(uv), t2 = dFdy(uv);
    float determinant = t1.x * t2.y - t1.y * t2.x;
    if (abs(determinant) < 1e-8) return N;
    vec3 T = (q1 * t2.y - q2 * t1.y) / determinant;
    vec3 B = (-q1 * t2.x + q2 * t1.x) / determinant;
    if (dot(T,T) < 1e-10 || dot(B,B) < 1e-10) return N;
    T -= N * dot(T,N);B -= N * dot(B,N);
    if (dot(T,T) < 1e-10 || dot(B,B) < 1e-10) return N;
    T = normalize(T);B = normalize(B);
    vec3 sampleNormal = texture(normalMap, uv).xyz * 2 - 1;
    sampleNormal.xy *= emissiveNormal.w;
    vec3 mapped = mat3(T,B,N) * sampleNormal;
    return dot(mapped,mapped) < 1e-10 ? N : normalize(mapped);
}

void main(){
    vec4 base=texture(albedoMap,uv)*albedoAlpha;if(base.a<factors.w)discard;
    vec3 N=mappedNormal(), delta=lightPositionType.w<.5?-lightDirectionOuter.xyz:lightPositionType.xyz-worldPosition;
    vec3 L=length(delta)>1e-6?normalize(delta):N;
    float cone=lightPositionType.w>1.5?clamp((dot(L,-normalize(lightDirectionOuter.xyz))-lightDirectionOuter.w)/max(lightColorInner.w-lightDirectionOuter.w,1e-5),0,1):1;
    vec3 incident=lightColorInner.rgb*max(dot(N,L),0)*cone;
    if(captureSettings.y>0. && lightPositionType.w>.5)incident/=max(dot(delta,delta),1e-6);
    if(captureSettings.x>0.)incident+=3.14159265*texture(skyIrradiance,skyUV(N)).rgb;
    float area=captureSettings.y>0.?length(cross(dFdx(worldPosition),dFdy(worldPosition))):1.;
    float metallic=clamp(texture(metallicMap,uv).b*factors.x,0.,1.);
    flux=vec4(pow(max(base.rgb,vec3(0)),vec3(2.2))*incident*(1.-metallic)*area,1);
    position=vec4(worldPosition,1);normal=vec4(N,1);
}
