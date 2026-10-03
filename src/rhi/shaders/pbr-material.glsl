layout(set=1,binding=0,std140) uniform MaterialData { vec4 albedoAlpha; vec4 factors; vec4 emissiveNormal; };
layout(set=1,binding=1) uniform sampler2D albedoMap;
layout(set=1,binding=2) uniform sampler2D normalMap;
layout(set=1,binding=3) uniform sampler2D metallicMap;
layout(set=1,binding=4) uniform sampler2D roughnessMap;
layout(set=1,binding=5) uniform sampler2D aoMap;
layout(set=1,binding=6,std140) uniform MaterialExtension {vec4 materialLobes;vec4 materialSettings;};
layout(set=1,binding=7) uniform sampler2D specialMap;
vec4 mappedLobes(){vec4 map=texture(specialMap,uv);return vec4(materialLobes.x,max(map.r*materialLobes.y,.089),clamp(map.g*materialLobes.z,-.95,.95),materialLobes.w);}
vec3 mappedTangent(vec3 N){vec3 q1=dFdx(worldPosition),q2=dFdy(worldPosition);vec2 a=dFdx(uv),b=dFdy(uv);vec3 T=q1*b.y-q2*a.y;T-=N*dot(T,N);return dot(T,T)>1e-10?normalize(T):normalize(abs(N.y)<.99?cross(N,vec3(0,1,0)):cross(N,vec3(1,0,0)));}
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
