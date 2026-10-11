#include "renderer/rhi/GpuOcean.h"
#include "renderer/rhi/OceanSpectrum.h"
#include "renderer/rhi/OceanSurface.h"
#include "rhi/ShaderAssets.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
namespace render {
namespace {
using Complex=std::complex<double>;
constexpr double pi=3.141592653589793;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(std::string("RHI Ocean: ")+reason);}
std::vector<Complex> inverseDft(const std::vector<Complex>& input,int n) {
    std::vector<Complex> result(n*n);
    for(int y=0;y<n;++y)for(int x=0;x<n;++x)for(int ky=0;ky<n;++ky)for(int kx=0;kx<n;++kx)
        result[y*n+x]+=input[ky*n+kx]*std::polar(1.0,2*pi*(kx*x+ky*y)/n);
    return result;
}
// Independent bit-reversal Cooley-Tukey CPU reference for production-size grids.
void cpuFft(std::vector<Complex>& a) {
    const size_t n=a.size();for(size_t i=1,j=0;i<n;++i){size_t bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
    for(size_t size=2;size<=n;size*=2){const auto root=std::polar(1.,2*pi/size);for(size_t base=0;base<n;base+=size){Complex w=1;for(size_t j=0;j<size/2;++j){auto u=a[base+j],v=a[base+j+size/2]*w;a[base+j]=u+v;a[base+j+size/2]=u-v;w*=root;}}}
}
std::vector<Complex> cpuIfft2(const std::vector<Complex>& a,int n){auto result=a;std::vector<Complex> line(n);for(int y=0;y<n;++y){for(int x=0;x<n;++x)line[x]=result[y*n+x];cpuFft(line);for(int x=0;x<n;++x)result[y*n+x]=line[x];}
    for(int x=0;x<n;++x){for(int y=0;y<n;++y)line[y]=result[y*n+x];cpuFft(line);for(int y=0;y<n;++y)result[y*n+x]=line[y];}return result;}
void fft(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory,int n,bool analytic) {
    OceanSettings settings;settings.size=uint32_t(n);GpuOcean ocean(device,directory,settings);
    std::vector<Complex> input(n*n);std::vector<float> rgba(size_t(n)*n*4);
    for(int i=0;i<n*n;++i) { input[i]=analytic?Complex(i==3?1:0,0):Complex(std::sin(i*.37),std::cos(i*.61));rgba[4*i]=float(input[i].real());rgba[4*i+1]=float(input[i].imag()); }
    auto output=ocean.inverseFFT(rgba);std::vector<Complex> expected;
    if(!analytic)expected=n<=16?inverseDft(input,n):cpuIfft2(input,n);
    double error=0,scale=1;
    for(int y=0;y<n;++y)for(int x=0;x<n;++x) {
        const auto value=analytic?std::polar(1.0,2*pi*3*x/n):expected[y*n+x];const size_t i=size_t(y)*n+x;
        error=std::max(error,std::abs(Complex(output[4*i],output[4*i+1])-value));scale=std::max(scale,std::abs(value));
    }
    check(error/scale<2e-5,"2D inverse FFT differs from independent CPU reference");
    std::cout<<"RHI Ocean IFFT N="<<n<<" relative max error="<<error/scale<<'\n';
}
}
namespace {
void slopeMomentTest(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory){
    OceanSettings settings;settings.size=16;settings.length=32;GpuOcean ocean(device,directory,settings);ocean.simulate(3,settings);
    const auto normal=ocean.readNormal();ocean.buildSlopeMoments();check(ocean.slopeMomentLevels()==5,"Ocean slope mip tail missing");
    std::vector<std::array<double,8>> expected(256);double maximum=0,scale=1;
    for(size_t i=0;i<expected.size();++i){double x=normal[i*4]/std::max(double(normal[i*4+1]),.1),z=normal[i*4+2]/std::max(double(normal[i*4+1]),.1);
        expected[i]={x,z,x*x,z*z,x*z,normal[i*4+3],0,0};}
    uint32_t n=16;
    for(uint32_t level=0;level<5;++level){
        for(uint32_t layer=0;layer<2;++layer){auto bytes=device->readTextureSubresource(ocean.slopeMomentTexture(),{level,layer});std::vector<float> values(bytes.size()/4);std::memcpy(values.data(),bytes.data(),bytes.size());
            check(values.size()==size_t(n)*n*4,"Ocean slope mip subresource dimensions differ");
            for(size_t i=0;i<expected.size();++i)for(size_t c=0;c<4;++c){double v=expected[i][layer*4+c];check(std::isfinite(values[i*4+c]),"Nonfinite slope moments");maximum=std::max(maximum,std::abs(values[i*4+c]-v));scale=std::max(scale,std::abs(v));}
        }
        if(n>1){std::vector<std::array<double,8>> reduced(size_t(n/2)*(n/2));
            for(uint32_t y=0;y<n/2;++y)for(uint32_t x=0;x<n/2;++x)for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)for(size_t c=0;c<8;++c)reduced[y*(n/2)+x][c]+=expected[(y*2+dy)*n+x*2+dx][c]*.25;
            expected.swap(reduced);n/=2;}
    }
    check(maximum/scale<2e-5,"Slope mip lost first/second/cross moments or foam");
    auto calm=settings;calm.amplitude=0;ocean.simulate(3,calm);ocean.buildSlopeMoments();auto bytes=device->readTextureSubresource(ocean.slopeMomentTexture(),{4,0});
    float tail[4];std::memcpy(tail,bytes.data(),sizeof(tail));for(float value:tail)check(std::abs(value)<1e-7,"Slope mips retained stale wave energy after simulation");
    std::cout<<"Ocean slope mip first/second/cross moments and foam, every level, simulation invalidation max relative error="<<maximum/scale<<" PASS\n";
}
void waterModelTest(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory){
    using namespace rhi;Resources resources(device);ComputePipelineDesc desc;
    const auto path=directory+"/water-model-validate.comp";
    desc.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};desc.threads={8,8,1};desc.label="Water physical model validation";
    desc.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"WaterModelTest",16},{1,BindingType::StorageTextureWrite,ShaderStage::Compute,"modelOutput",0}}}};
    auto pipeline=resources.computePipeline(desc);
    auto texture=resources.texture({256,256,Format::RGBA32Float,TextureUsage::Storage|TextureUsage::CopySource,"Water model numeric probe"});auto view=resources.view(texture);
    auto fresnel=[](double ci){double st2=(1-ci*ci)/(1.333*1.333),ct=std::sqrt(1-st2);
        double a=(1.333*ci-ct)/(1.333*ci+ct),b=(ci-1.333*ct)/(ci+1.333*ct);return .5*(a*a+b*b);};
    double maxF=0,maxRough=0;
    for(auto params:{glm::vec4(.005f,0,0,.02f),glm::vec4(.005f,.01f,.01f,.04f),glm::vec4(.005f,.04f,.002f,.1f),glm::vec4(.02f,.01f,.01f,.1f),glm::vec4(.005f,-.01f,-.004f,.12f)}){
        Resources frame(device);auto buffer=frame.buffer({16,BufferUsage::Uniform,"Water model parameters"},&params);
        auto group=frame.bindings({desc.bindings[0],{{0,buffer,0,16,{},{}},{1,{},0,0,view,{}}}});
        auto commands=device->createCommandList();commands.dispatch(pipeline,{group},{32,32,1});device->submit(commands);
        auto film=device->readTextureFloat(texture);double integral=0,step=2*params.w/256.;
        for(int y=0;y<256;++y)for(int x=0;x<256;++x){size_t i=(size_t(y)*256+x)*4;
            check(std::isfinite(film[i+3])&&film[i+3]<.002,"Filtered GGX zero covariance differs from unfiltered GGX");
            check(std::isfinite(film[i])&&film[i]>=0&&std::isfinite(film[i+2]),"nonfinite physical solar kernel");
            double qx=((x+.5)/256*2-1)*params.w,qy=((y+.5)/256*2-1)*params.w;
            integral+=film[i]*step*step/std::pow(1+qx*qx+qy*qy,2.);
            maxF=std::max(maxF,std::abs(film[i+1]-fresnel((x+.5)/256)));
            glm::dvec3 v=glm::normalize(glm::dvec3(.2,1,.1)),l=glm::normalize(glm::dvec3(qx,1,qy)),h=glm::normalize(v+l);
            double a2=std::pow(.12,4),q=1-h.y*h.y+a2*h.y*h.y,D=a2/(pi*q*q);
            auto smith=[&](double c){return 2*c/(c+std::sqrt(a2+(1-a2)*c*c));};
            double expected=fresnel(glm::dot(v,h))*D*smith(v.y)*smith(l.y)/(4*v.y*l.y);
            maxRough=std::max(maxRough,std::abs(film[i+2]-expected));
        }
        check(std::abs(integral-1)<.02,"physical solar footprint lost or duplicated angular energy");
        std::cout<<"Water solar radius="<<params.x<<" footprint="<<params.y<<","<<params.z<<" angular irradiance="<<integral<<'\n';
    }
    check(maxF<2e-6&&maxRough<2e-4,"physical Fresnel/GGX differs from double reference");
    std::cout<<"Water shared Fresnel / GGX numeric max error="<<maxF<<" / "<<maxRough<<" PASS\n";
    // Independent finite-difference reflection Jacobian, rather than the
    // analytic half-vector Jacobian used by the shader. Include grazing views.
    double maxSlopeError=0;
    for(float viewY:{.02f,.1f,.7f}){
        glm::vec4 params(-.005f,viewY,0,.5f);Resources frame(device);
        auto buffer=frame.buffer({16,BufferUsage::Uniform,"Slope Jacobian parameters"},&params);
        auto group=frame.bindings({desc.bindings[0],{{0,buffer,0,16,{},{}},{1,{},0,0,view,{}}}});
        auto commands=device->createCommandList();commands.dispatch(pipeline,{group},{32,32,1});device->submit(commands);
        auto film=device->readTextureFloat(texture);
        const auto V=glm::normalize(glm::dvec3(.15,viewY,1)),L=glm::normalize(glm::dvec3(.1,.45,-1)),H=glm::normalize(V+L);
        const auto t=glm::normalize(glm::cross(glm::dvec3(0,1,0),L)),b=glm::cross(L,t);
        const glm::dmat2 cov(.012,.003,.003,.025),inv=glm::inverse(cov);const double det=glm::determinant(cov);
        auto reflection=[&](glm::dvec2 slope){return glm::reflect(-V,glm::normalize(glm::dvec3(slope.x,1,slope.y)));};
        for(int y=0;y<256;++y)for(int x=0;x<256;++x){
            glm::dvec2 mean=glm::dvec2(H.x,H.z)/H.y+glm::dvec2(((x+.5)/256*2-1)*params.w,((y+.5)/256*2-1)*params.w);
            double expected=0;
            for(int j=0;j<16;++j){
                const double z=glm::mix(1.,std::cos(double(-params.x)),(j+.5)/16),r=std::sqrt(1-z*z),phi=j*2.39996322973;
                auto wi=L*z+r*(t*std::cos(phi)+b*std::sin(phi)),h=glm::normalize(V+wi);
                glm::dvec2 slope=glm::dvec2(h.x,h.z)/h.y,delta=slope-mean;
                constexpr double epsilon=1e-5;
                const auto dx=(reflection(slope+glm::dvec2(epsilon,0))-reflection(slope-glm::dvec2(epsilon,0)))/(2*epsilon);
                const auto dy=(reflection(slope+glm::dvec2(0,epsilon))-reflection(slope-glm::dvec2(0,epsilon)))/(2*epsilon);
                const double jacobian=glm::length(glm::cross(dx,dy));
                expected+=fresnel(glm::dot(V,h))*std::exp(-.5*glm::dot(delta,inv*delta))/(2*pi*std::sqrt(det)*jacobian);
            }
            expected*=2/(16*(1+std::cos(double(-params.x))));
            const double value=film[(size_t(y)*256+x)*4];check(std::isfinite(value)&&value>=0,"Nonfinite slope-space sunlight");
            maxSlopeError=std::max(maxSlopeError,std::abs(value-expected)/std::max(expected,1e-4));
        }
    }
    check(maxSlopeError<.002,"Slope solar integral differs from numeric reflection Jacobian");
    std::cout<<"Water slope-space finite sun / grazing reflection Jacobian max relative error="<<maxSlopeError<<" PASS\n";
}
void seaStateTest(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory){
    OceanSettings s;s.size=16;s.length=128;s.heightScale=1;s.seaState.enabled=true;
    s.seaState.windHeight=1;s.seaState.windPeriod=4;s.windDirection={1,0};
    s.seaState.swellHeight=.6f;s.seaState.swellPeriod=6;s.seaState.depth=7;
    s.minWavelength=16;s.maxWavelength=128;
    const double dk=2*pi/s.length;
    check(std::abs(oceanSpectrum::omega(.2,0)-std::sqrt(9.81*.2))<1e-12,"deep-water dispersion");
    check(std::abs(oceanSpectrum::omega(.0001,2)/.0001-std::sqrt(9.81*2))<1e-6,"shallow-water wave speed");
    check(oceanSpectrum::depthFactor(.2,0)==1&&oceanSpectrum::depthFactor(.2,7)<1,"TMA depth shape");
    check(oceanSpectrum::shape({-.2,0},{1,0},4,3.3,4,s)>0&&oceanSpectrum::shape({.2,0},{1,0},4,3.3,4,s)==0,"directional spectrum travels against intended direction");
    auto normalization=[&](bool swell){double energy=0;
        for(int y=1;y<16;++y)for(int x=1;x<16;++x){auto k=glm::dvec2(x-8,y-8)*dk;const auto& a=s.seaState;
            energy+=oceanSpectrum::shape(k,swell?a.swellDirection:s.windDirection,swell?a.swellPeriod:a.windPeriod,swell?1:a.windGamma,swell?a.swellSpread:a.windSpread,s);}
        double variance=2*dk*dk*energy*oceanSpectrum::normalization(s,swell);double h=swell?s.seaState.swellHeight:s.seaState.windHeight;
        check(std::abs(variance-h*h/16)<1e-9,"discrete significant height normalization");
    };normalization(false);normalization(true);
    GpuOcean ocean(device,directory,s);const float time=2.3f;ocean.simulate(time,s);
    auto gaussian=ocean.readGaussian(),height=ocean.readHeight(),displace=ocean.readDisplacement();
    std::vector<Complex> spectrum(256);double aw=oceanSpectrum::normalization(s),as=oceanSpectrum::normalization(s,true);
    auto h0=[&](int i,glm::dvec2 k){const auto& a=s.seaState;
        double pw=aw*oceanSpectrum::shape(k,s.windDirection,a.windPeriod,a.windGamma,a.windSpread,s);
        double ps=as*oceanSpectrum::shape(k,a.swellDirection,a.swellPeriod,1,a.swellSpread,s);
        return Complex(gaussian[4*i],gaussian[4*i+1])*std::sqrt(pw*.5)+Complex(gaussian[4*i+2],gaussian[4*i+3])*std::sqrt(ps*.5);};
    for(int y=1;y<16;++y)for(int x=1;x<16;++x){if(x==8&&y==8)continue;auto k=glm::dvec2(x-8,y-8)*dk;
        int i=y*16+x,mirror=((16-y)%16)*16+(16-x)%16;
        double phase=oceanSpectrum::omega(glm::length(k),s.seaState.depth)*time;
        spectrum[i]=(h0(i,k)*std::polar(1.,phase)+std::conj(h0(mirror,-k))*std::polar(1.,-phase))*dk*256.;}
    auto expected=inverseDft(spectrum,16);double error=0,imaginary=0;
    for(int i=0;i<256;++i){error=std::max(error,std::abs(Complex(height[4*i],height[4*i+1])-expected[i])/256.);imaginary=std::max(imaginary,double(std::abs(height[4*i+1]))/256.);}
    check(error<2e-5&&imaginary<2e-5,"JONSWAP/TMA two independent wave systems differ from CPU DFT");
    ocean.simulate(time,s);check(ocean.readDisplacement()==displace,"sea state lost frozen-time determinism");
    if(!std::getenv("SCENERENDERER_OCEAN_UNCACHED")){
        check(ocean.initialSpectrumBuilds()==1,"frozen-time spectrum cache missed");
        ocean.simulate(time+1,s);auto warm=ocean.readDisplacement();
        GpuOcean fresh(device,directory,s);fresh.simulate(time+1,s);
        check(warm==fresh.readDisplacement()&&ocean.initialSpectrumBuilds()==1,"time evolution reused stale or rebuilt initial spectrum");
        auto shading=s;shading.choppiness=.1f;shading.foamScale=4;shading.foamThreshold=.2f;
        ocean.simulate(time,shading);check(ocean.initialSpectrumBuilds()==1,"non-spectral controls invalidated initial spectrum");
        auto seed=s;seed.seed+=1;ocean.simulate(time,seed);
        GpuOcean newSeed(device,directory,seed);newSeed.simulate(time,seed);
        check(ocean.readDisplacement()==newSeed.readDisplacement()&&ocean.initialSpectrumBuilds()==2,"seed edit reused stale initial spectrum");
        ocean.simulate(time,s);check(ocean.readDisplacement()==displace&&ocean.initialSpectrumBuilds()==3,"restored seed did not rebuild spectrum");
    }
    auto doubled=s;doubled.seaState.windHeight*=2;doubled.seaState.swellHeight*=2;ocean.simulate(time,doubled);auto doubledD=ocean.readDisplacement();
    for(size_t i=0;i<displace.size();++i)if(i%4<3)check(std::abs(doubledD[i]-2*displace[i])<2e-5,"Hs edit failed linear amplitude/cache update");
    auto swell=s;swell.seaState.windHeight=0;swell.windSpeed=0;ocean.simulate(time,swell);auto swellD=ocean.readDisplacement();
    swell.windDirection={0,0};ocean.simulate(time,swell);check(ocean.readDisplacement()==swellD,"swell depends on local wind");
    check(std::any_of(swellD.begin(),swellD.end(),[](float v){return std::abs(v)>.01f;}),"independent swell disappeared in calm wind");
    auto calm=s;calm.seaState.windHeight=calm.seaState.swellHeight=0;ocean.simulate(time,calm);auto calmD=ocean.readDisplacement();
    for(size_t i=0;i<calmD.size();++i)if(i%4<3)check(calmD[i]==0,"zero Hs sea state is not flat");
    auto deep=s;deep.seaState.depth=0;ocean.simulate(time,deep);auto deepD=ocean.readDisplacement();deep.seaState.depth=1e6f;ocean.simulate(time,deep);auto finiteDeep=ocean.readDisplacement();
    for(size_t i=0;i<deepD.size();++i)check(std::abs(deepD[i]-finiteDeep[i])<2e-5,"TMA failed deep-water limit");
    auto target=s;target.windDirection={-.3f,1};ocean.simulate(time,target);auto destination=ocean.readDisplacement();
    target.windTransition=true;target.previousWind=s.windDirection*s.windSpeed;target.windBlend=0;ocean.simulate(time,target);auto start=ocean.readDisplacement();
    target.windBlend=.5f;ocean.simulate(time,target);auto middle=ocean.readDisplacement();
    for(size_t i=0;i<displace.size();++i)if(i%4<3){check(std::abs(start[i]-displace[i])<2e-5,"sea-state transition source changed");check(std::abs(middle[i]-.5f*(displace[i]+destination[i]))<2e-5,"sea-state wind blend changed swell or phase");}
    OceanSurfaceSettings surface;surface.spectrum=s;auto detail=oceanDetailSettings(surface);
    check(!detail.seaState.enabled&&detail.minWavelength==.5f&&detail.maxWavelength==2&&detail.foamScale==0,"detail FFT double-counts sea-state energy");
    auto bad=s;bad.seaState.windGamma=.5f;bool rejected=false;try{ocean.simulate(time,bad);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"invalid sea state accepted");
    std::cout<<"RHI Ocean sea state: Hs calibration, wind/swell independence, TMA/deep limit, phase blend, CPU DFT max error="<<error<<" PASS\n";
}
}
void validateOceanRhi(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory) {
    if(!device->computeLimits().maxStorageImages){std::cout<<"RHI Ocean storage image path deferred on this backend\n";return;}
    slopeMomentTest(device,directory);waterModelTest(device,directory);
    seaStateTest(device,directory);
    fft(device,directory,8,false);fft(device,directory,16,false);fft(device,directory,256,false);fft(device,directory,512,false);fft(device,directory,1024,true);
    OceanSettings settings;settings.size=16;settings.length=64;settings.windSpeed=12;
    GpuOcean ocean(device,directory,settings);const float time=1.7f;ocean.simulate(time,settings);
    const auto gaussian=ocean.readGaussian(), heights=ocean.readHeight(), displace=ocean.readDisplacement(), normals=ocean.readNormal(), foam=ocean.readFoam();
    const int n=16;const double dk=2*pi/settings.length,L=144/9.81;
    std::vector<Complex> spectrum(n*n);
    for(int y=1;y<n;++y)for(int x=1;x<n;++x) {
        const double kx=(x-n/2)*dk,kz=(y-n/2)*dk,k2=kx*kx+kz*kz;if(k2==0)continue;
        const double alignment=(kx+kz)/std::sqrt(2*k2);
        const double p=settings.amplitude*std::exp(-1/(k2*L*L))/(k2*k2)*alignment*alignment*std::exp(-k2*L*L*1e-6);
        const int i=y*n+x,mirror=((n-y)%n)*n+(n-x)%n;
        const auto a=Complex(gaussian[4*i],gaussian[4*i+1])*std::sqrt(p*.5), b=std::conj(Complex(gaussian[4*mirror],gaussian[4*mirror+1]))*std::sqrt(p*.5);
        const double phase=std::sqrt(9.81*std::sqrt(k2))*time;spectrum[i]=(a*std::polar(1.0,phase)+b*std::polar(1.0,-phase))*(dk*n*n);
    }
    const auto expected=inverseDft(spectrum,n);double error=0,maxReal=0,maxImaginary=0;float low=0,high=0;
    for(int i=0;i<n*n;++i) {
        error=std::max(error,std::abs(Complex(heights[4*i],heights[4*i+1])-expected[i])/(n*n));
        maxReal=std::max(maxReal,double(std::abs(heights[4*i])));maxImaginary=std::max(maxImaginary,double(std::abs(heights[4*i+1])));
        const float sign=((i%n+i/n)&1)?-1.f:1.f;
        check(std::abs(displace[4*i+1]-heights[4*i]*sign/(n*n)*settings.heightScale)<1e-5,"signed height/centering/normalization mismatch");
        low=std::min(low,displace[4*i+1]);high=std::max(high,displace[4*i+1]);
        check(std::abs(normals[4*i+3]-foam[4*i])<1e-6,"packed normal foam differs from standalone foam");
        const glm::vec3 normal(normals[4*i],normals[4*i+1],normals[4*i+2]);check(std::abs(glm::length(normal)-1)<1e-5,"non-unit ocean normal");
        check(foam[4*i]>=0 && foam[4*i]<=1 && std::isfinite(foam[4*i+3]),"invalid foam/Jacobian");
    }
    check(error<2e-5 && maxImaginary/std::max(1.,maxReal)<2e-5,"Phillips spectrum/evolution or Hermitian height field failed");check(low<0 && high>0,"wave field lost signed height");
    ocean.simulate(time,settings);check(ocean.readDisplacement()==displace,"fixed seed/time simulation is not deterministic");
    ocean.simulate(time+.5f,settings);check(ocean.readDisplacement()!=displace,"time evolution ignored");
    auto changed=settings;changed.seed+=1;ocean.simulate(time,changed);check(ocean.readGaussian()!=gaussian,"seed update ignored");
    {
        auto target=settings;target.windDirection={-1,.3f};target.windSpeed=25;
        ocean.simulate(time,target);const auto destination=ocean.readDisplacement();
        auto blended=target;blended.windTransition=true;
        blended.previousWind=glm::normalize(settings.windDirection)*settings.windSpeed;
        blended.windBlend=0;ocean.simulate(time,blended);auto start=ocean.readDisplacement();
        blended.windBlend=.5f;ocean.simulate(time,blended);auto middle=ocean.readDisplacement();
        blended.windBlend=1;ocean.simulate(time,blended);auto end=ocean.readDisplacement();
        for(size_t i=0;i<displace.size();++i)if(i%4<3){
            check(std::abs(start[i]-displace[i])<2e-5,"Wave blend start changed the source spectrum");
            check(std::abs(end[i]-destination[i])<2e-5,"Wave blend end differs from destination spectrum");
            check(std::abs(middle[i]-.5f*(displace[i]+destination[i]))<2e-5,"Wave blend lost phase/amplitude continuity");
        }
        check(ocean.readGaussian()==gaussian,"Wind transition changed Gaussian samples");
        blended.windBlend=NAN;try{ocean.simulate(time,blended);throw std::runtime_error("Ocean accepted NaN blend");}catch(const std::invalid_argument&){}
        std::cout<<"RHI wave wind: source/destination endpoints, linear spectral displacement and Gaussian phase preservation passed\n";
    }
    settings.windSpeed=0;ocean.simulate(time,settings);auto flat=ocean.readDisplacement(),flatNormals=ocean.readNormal();
    for(int i=0;i<n*n;++i) { check(flat[4*i]==0 && flat[4*i+1]==0 && flat[4*i+2]==0,"zero-wind field is not flat");check(flatNormals[4*i]==0 && flatNormals[4*i+1]==1 && flatNormals[4*i+2]==0,"flat ocean normal incorrect"); }
    auto invalid=settings;invalid.size=32;try{ocean.simulate(time,invalid);throw std::runtime_error("Ocean accepted grid mutation");}catch(const std::invalid_argument&){}
    // A short-wave band must retain centimetre energy, resolved slopes and
    // phase motion, while excluding metre-to-domain-size long waves.
    OceanSettings ripple;ripple.size=256;ripple.length=32;ripple.windSpeed=7.2f;ripple.heightScale=.6f;
    ripple.choppiness=.25f;ripple.minWavelength=.5f;ripple.maxWavelength=2;ripple.targetRmsHeight=.025f;ripple.foamScale=0;
    GpuOcean shortWaves(device,directory,ripple);shortWaves.simulate(8,ripple);
    const auto rp=shortWaves.readDisplacement(),rn=shortWaves.readNormal(),rh=shortWaves.readHeight();
    double variance=0,slopeVariance=0;std::vector<Complex> spatial(256*256);
    for(int i=0;i<256*256;++i){variance+=rp[4*i+1]*rp[4*i+1];slopeVariance+=rn[4*i]*rn[4*i]+rn[4*i+2]*rn[4*i+2];
        spatial[i]=Complex(rh[4*i],rh[4*i+1]);}
    const double rms=std::sqrt(variance/spatial.size()),tilt=std::sqrt(slopeVariance/spatial.size());
    check(rms>.021 && rms<.029,"short-wave ensemble RMS normalization failed");
    check(tilt>.06 && tilt<.25,"short-wave normals lost resolved wave slopes");
    // A second positive transform moves centred spatial data back to frequency
    // coordinates (with mirrored bins); radial support is invariant to this.
    auto frequencies=cpuIfft2(spatial,256);double inBand=0,outBand=0;
    for(int y=0;y<256;++y)for(int x=0;x<256;++x){double k=std::hypot(double(x-128),double(y-128))*2*pi/32;
        auto energy=std::norm(frequencies[y*256+x]);if(k>=2*pi/2-1e-5 && k<=2*pi/.5+1e-5)inBand+=energy;else outBand+=energy;}
    check(outBand/std::max(inBand,1e-10)<1e-9,"short-wave spectrum contains long-wave or aliased energy");
    shortWaves.simulate(8.35f,ripple);const auto later=shortWaves.readDisplacement();double motion=0;
    for(int i=0;i<256*256;++i)motion+=std::pow(later[4*i+1]-rp[4*i+1],2);
    check(std::sqrt(motion/spatial.size())>.01,"short-wave phase is static");
    {
        auto target=ripple;target.windDirection={-1,.2f};target.windSpeed=12.7f;
        shortWaves.simulate(8,target);const auto end=shortWaves.readDisplacement();
        target.windTransition=true;target.previousWind=glm::normalize(ripple.windDirection)*ripple.windSpeed;target.windBlend=.5f;
        shortWaves.simulate(8,target);const auto middle=shortWaves.readDisplacement();
        for(size_t i=0;i<middle.size();++i)if(i%4<3)
            check(std::abs(middle[i]-.5f*(rp[i]+end[i]))<3e-5,"Normalized short-wave endpoints failed phase-continuous blending");
        std::cout<<"RHI normalized short-wave wind endpoints: source/destination RMS caches and spectral blend passed\n";
    }
    ripple.targetRmsHeight=0;shortWaves.simulate(8,ripple);
    const auto disabled=shortWaves.readDisplacement();for(int i=0;i<256*256;++i)for(int c=0;c<3;++c)check(disabled[4*i+c]==0,"zero RMS ripple disable is not flat");
    std::cout<<"RHI short-wave 0.5-2 m band: RMS "<<rms<<" m, RMS horizontal normal "<<tilt<<", out-of-band energy "<<outBand/inBand<<'\n';
    std::cout<<"RHI full Ocean spectrum -> 2D FFT -> displacement/normal/foam, seed/time updates and zero-wind validation passed; CPU error="<<error<<'\n';
}
}
