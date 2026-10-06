// One workgroup keeps a complete complex row in shared memory. Same Stockham
// butterfly/order as the reference kernels, without global ping-pong per stage.
#include "ocean-parameters.glsl"
layout(local_size_x=256) in;
layout(rgba32f,set=1,binding=5) readonly uniform image2D InputRT;
layout(rgba32f,set=1,binding=6) writeonly uniform image2D OutputRT;
shared vec2 fftRow[512];
vec2 butterfly(int i,int ns) {
    if(i>=N)return vec2(0);
    int a=(i/(2*ns))*ns+i%ns,b=a+N/2;
    float phase=6.28318530718*float(i%(2*ns))/float(2*ns);
    vec2 w=vec2(cos(phase),sin(phase)),v=fftRow[b];
    return fftRow[a]+vec2(w.x*v.x-w.y*v.y,w.x*v.y+w.y*v.x);
}
ivec2 fftPixel(int i){return FFT_VERTICAL!=0?ivec2(gl_WorkGroupID.y,i):ivec2(i,gl_WorkGroupID.y);}
void main() {
    int i=int(gl_LocalInvocationID.x);
    if(i<N)fftRow[i]=imageLoad(InputRT,fftPixel(i)).xy;
    if(i+256<N)fftRow[i+256]=imageLoad(InputRT,fftPixel(i+256)).xy;
    barrier();
    for(int ns=1;ns<N;ns*=2) {
        vec2 a=butterfly(i,ns),b=butterfly(i+256,ns);
        barrier(); // Every reader finishes before any in-place write.
        if(i<N)fftRow[i]=a;
        if(i+256<N)fftRow[i+256]=b;
        barrier();
    }
    if(i<N)imageStore(OutputRT,fftPixel(i),vec4(fftRow[i],0,0));
    if(i+256<N)imageStore(OutputRT,fftPixel(i+256),vec4(fftRow[i+256],0,0));
}
