// CPU counterpart: roughDielectric / visibleNormal in PT/CpuPathTracer.cpp.
float dielectricFresnel(float cosine,float etaI,float etaT){
    cosine=clamp(abs(cosine),0,1);float st=etaI/etaT*sqrt(max(0,1-cosine*cosine));if(st>=1)return 1;
    float ct=sqrt(max(0,1-st*st)),a=(etaT*cosine-etaI*ct)/(etaT*cosine+etaI*ct),b=(etaI*cosine-etaT*ct)/(etaI*cosine+etaT*ct);return .5*(a*a+b*b);
}
float thinDielectricReflectance(float cosine,float exteriorIor,float sheetIor){float f=dielectricFresnel(cosine,exteriorIor,sheetIor);return 2*f/(1+f);}
vec3 visibleNormal(vec3 v,vec3 n,float roughness,vec2 uv){
    float a=roughness*roughness;vec3 t=unit(cross(abs(n.y)<.99?vec3(0,1,0):vec3(1,0,0),n),vec3(1,0,0)),b=cross(n,t);
    vec3 vh=unit(vec3(a*dot(v,t),a*dot(v,b),dot(v,n)),vec3(0,0,1));float len=vh.x*vh.x+vh.y*vh.y;
    vec3 t1=len>0?vec3(-vh.y,vh.x,0)/sqrt(len):vec3(1,0,0),t2=cross(vh,t1);
    float radius=sqrt(uv.x),phi=2*PI*uv.y,x=radius*cos(phi),blend=.5*(1+vh.z),y=mix(sqrt(max(0,1-x*x)),radius*sin(phi),blend);
    vec3 nh=t1*x+t2*y+vh*sqrt(max(0,1-x*x-y*y)),ne=unit(vec3(a*nh.x,a*nh.y,max(0,nh.z)),vec3(0,0,1));return t*ne.x+b*ne.y+n*ne.z;
}
vec2 roughDielectric(Surface s,vec3 v,vec3 l){
    float nv=dot(s.n,v),nl=dot(s.n,l);if(nv<=0||nl==0||dot(v,s.gn)<=0)return vec2(0);
    bool reflection=nl>0;if((dot(l,s.gn)>0)!=reflection)return vec2(0);
    float etaI=s.frontFace?s.exteriorIor:s.ior,etaT=s.frontFace?s.ior:s.exteriorIor,etap=etaT/etaI;
    vec3 sum=v+l*(reflection?1:etap);if(dot(sum,sum)<1e-20)return vec2(0);vec3 h=unit(sum,s.n);if(dot(h,s.n)<0)h=-h;
    float vh=dot(v,h),lh=dot(l,h);if(vh<=0||lh*nl<=0)return vec2(0);
    float nh=clamp(dot(h,s.n),0,1),a=s.transmissionRoughness*s.transmissionRoughness,a2=a*a,q=max(0,1-nh*nh)+a2*nh*nh;
    float D=a2/(PI*q*q),Gv=smith(nv,s.transmissionRoughness),Gl=smith(abs(nl),s.transmissionRoughness),G=s.bsdfModel==2u?Gv*Gl/(Gv+Gl-Gv*Gl):Gv*Gl,F=dielectricFresnel(reflection?.5*(vh+lh):vh,etaI,etaT),normalPdf=D*Gv*vh/nv;
    if(reflection)return vec2(F*D*G/(4*nv*nl),F*normalPdf/(4*vh));
    float denominator=lh+vh/etap;if(abs(denominator)<1e-10)return vec2(0);
    return vec2((1-F)*D*G*abs(vh*lh)/(nv*abs(nl)*denominator*denominator*etap*etap),(1-F)*normalPdf*abs(lh)/(denominator*denominator));
}
