// Raw moments survive mip reduction. Subtracting the mean only after lookup
// retains unresolved slopes instead of flattening away their reflected energy.
#ifdef WATER_SLOPE_FRAGMENT
vec2 waterSlopeLookup(sampler2DArray moments,vec2 uv,float footprintScale,out mat2 covariance,out float foam){
    vec2 at=uv*vec2(textureSize(moments,0).xy);
    float footprint=max(length(dFdx(at)),length(dFdy(at)))*footprintScale;
    vec4 m=textureLod(moments,vec3(uv,0),max(log2(max(footprint,1e-6)),0.));
    vec4 q=textureLod(moments,vec3(uv,1),max(log2(max(footprint,1e-6)),0.));
    vec2 variance=max(m.zw-m.xy*m.xy,vec2(0));
    float crossMoment=clamp(q.x-m.x*m.y,-sqrt(variance.x*variance.y),sqrt(variance.x*variance.y));
    covariance=mat2(variance.x,crossMoment,crossMoment,variance.y)*smoothstep(1.,2.,footprint);
    foam=q.y;
    if(footprint<=1.){vec3 n=textureLod(moments,vec3(uv,2),0.).xyz;return n.xz/max(n.y,.1);}
    return m.xy;
}
#endif
// Linearized reflection Jacobian maps the unresolved world slopes into the
// solar tangent plane. This is a local Gaussian approximation, not a full NDF.
mat2 waterSlopeSolarCovariance(vec2 slope,mat2 covariance,vec3 V,vec3 L){
    vec3 N=normalize(vec3(slope.x,1,slope.y));float inverseLength=inversesqrt(1.+dot(slope,slope));
    vec3 nx=(vec3(1,0,0)-N*N.x)*inverseLength,nz=(vec3(0,0,1)-N*N.z)*inverseLength;
    vec3 rx=2.*(dot(nx,V)*N+dot(N,V)*nx),rz=2.*(dot(nz,V)*N+dot(N,V)*nz),t,b;waterSolarBasis(L,t,b);
    mat2 J=mat2(vec2(dot(rx,t),dot(rx,b)),vec2(dot(rz,t),dot(rz,b)));
    return J*covariance*transpose(J);
}
