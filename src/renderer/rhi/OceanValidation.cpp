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
void fft(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory,int n,bool analytic) {
    OceanSettings settings;settings.size=uint32_t(n);GpuOcean ocean(device,directory,settings);
    std::vector<Complex> input(n*n);std::vector<float> rgba(size_t(n)*n*4);
    for(int i=0;i<n*n;++i) { input[i]=analytic?Complex(i==3?1:0,0):Complex(std::sin(i*.37),std::cos(i*.61));rgba[4*i]=float(input[i].real());rgba[4*i+1]=float(input[i].imag()); }
    auto output=ocean.inverseFFT(rgba);std::vector<Complex> expected;
    if(!analytic)expected=inverseDft(input,n);
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
    fft(device,directory,8,false);fft(device,directory,16,false);fft(device,directory,1024,true);
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
    std::cout<<"RHI full Ocean spectrum -> 2D FFT -> displacement/normal/foam, seed/time updates and zero-wind validation passed; CPU error="<<error<<'\n';
}
}
