#version 450
layout(location=0) in vec2 uv;
layout(location=0) out float nearestDepth;
layout(set=1,binding=0) uniform sampler2D aoDepthSource;
void main() {
    ivec2 sourceSize=textureSize(aoDepthSource,0),destSize=max(sourceSize/2,ivec2(1));
    ivec2 pixel=ivec2(gl_FragCoord.xy),first=pixel*2;
    // The final cell absorbs an odd trailing row/column; never drop thin edges.
    ivec2 end=min(first+2,sourceSize);
    if(pixel.x==destSize.x-1)end.x=sourceSize.x;
    if(pixel.y==destSize.y-1)end.y=sourceSize.y;
    float value=3.402823e38;
    for(int y=first.y;y<end.y;++y)for(int x=first.x;x<end.x;++x) {
        float d=texelFetch(aoDepthSource,ivec2(x,y),0).r;
        if(d>0.)value=min(value,d);
    }
    nearestDepth=value==3.402823e38?0.:value;
}
