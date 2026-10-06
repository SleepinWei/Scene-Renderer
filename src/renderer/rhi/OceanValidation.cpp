#include "renderer/rhi/GpuOcean.h"
#include <algorithm>
#include <cmath>
#include <complex>
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
void validateOceanRhi(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory) {
    if(!device->computeLimits().maxStorageImages){std::cout<<"RHI Ocean storage image path deferred on this backend\n";return;}
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
        const glm::vec3 normal(normals[4*i],normals[4*i+1],normals[4*i+2]);check(std::abs(glm::length(normal)-1)<1e-5,"non-unit ocean normal");
        check(foam[4*i]>=0 && foam[4*i]<=1 && std::isfinite(foam[4*i+3]),"invalid foam/Jacobian");
    }
    check(error<2e-5 && maxImaginary/std::max(1.,maxReal)<2e-5,"Phillips spectrum/evolution or Hermitian height field failed");check(low<0 && high>0,"wave field lost signed height");
    ocean.simulate(time,settings);check(ocean.readDisplacement()==displace,"fixed seed/time simulation is not deterministic");
    ocean.simulate(time+.5f,settings);check(ocean.readDisplacement()!=displace,"time evolution ignored");
    auto changed=settings;changed.seed+=1;ocean.simulate(time,changed);check(ocean.readGaussian()!=gaussian,"seed update ignored");
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
    ripple.targetRmsHeight=0;shortWaves.simulate(8,ripple);
    const auto disabled=shortWaves.readDisplacement();for(int i=0;i<256*256;++i)for(int c=0;c<3;++c)check(disabled[4*i+c]==0,"zero RMS ripple disable is not flat");
    std::cout<<"RHI short-wave 0.5-2 m band: RMS "<<rms<<" m, RMS horizontal normal "<<tilt<<", out-of-band energy "<<outBand/inBand<<'\n';
    std::cout<<"RHI full Ocean spectrum -> 2D FFT -> displacement/normal/foam, seed/time updates and zero-wind validation passed; CPU error="<<error<<'\n';
}
}
