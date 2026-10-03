// One encoding for display, convolution, PBR, RSM and water. Longitude wraps.
const float SKY_PI=3.141592653589793;
vec2 skyUV(vec3 d) {
    d=normalize(d);float longitude=dot(d.xz,d.xz)<1e-12?0.0:atan(d.x,-d.z);
    float latitude=asin(clamp(d.y,-1.0,1.0));
    return vec2(fract(longitude/(2.0*SKY_PI)+1.0),.5+.5*sign(latitude)*sqrt(abs(latitude)/(SKY_PI*.5)));
}
vec3 sampleSkyLut(sampler2D image,vec3 direction) {
    vec2 uv=skyUV(direction);float height=float(textureSize(image,0).y);
    uv.y=.5/height+uv.y*(1.0-1.0/height);
    return texture(image,uv).rgb;
}
vec3 skyTexelDirection(ivec2 pixel,ivec2 size) {
    float y=2.0*float(pixel.y)/float(size.y-1)-1.0;
    float latitude=sign(y)*y*y*(SKY_PI*.5),longitude=(float(pixel.x)+.5)/float(size.x)*2.0*SKY_PI;
    return vec3(cos(latitude)*sin(longitude),sin(latitude),-cos(latitude)*cos(longitude));
}
