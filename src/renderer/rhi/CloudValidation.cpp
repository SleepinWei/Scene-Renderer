#include "renderer/rhi/GpuClouds.h"
#include "renderer/rhi/ForwardPbrRenderer.h"
#include "component/Cloud.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <iostream>
#include <numeric>
#include <limits>
#include <deque>
namespace render {
namespace {void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}}
void validateCloudsRhi(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory){
    if(!device->computeLimits().maxStorageImages)return;
    Cloud component;const auto original=component.settings();auto invalid=original;invalid.density=std::numeric_limits<float>::quiet_NaN();
    bool rejected=false;try{component.setSettings(invalid);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected && component.settings()==original,"Invalid clouds partially published component settings");
    json config={{"enabled",true},{"coverage",.7f},{"wind",{5,9}},{"downsample",4}};component.loadFromJson(config);
    check(component.settings().enabled && component.settings().downsample==4 && component.settings().wind==glm::vec2(5,9),"Cloud JSON settings were not captured");
    auto runtime=component.settings();runtime.advection=glm::dvec2(0);component.setSettings(runtime);
    runtime.advection=glm::dvec2(100,200);component.setSettings(runtime);
    check(component.settings().advection==runtime.advection,"Cloud settings equality discarded a runtime position update");
    ForwardPbrRenderer renderer(device,directory,80,60,PbrPath::Scene);
    FrameData f;f.sky=true;f.sunAngle=35;f.clouds.enabled=true;f.clouds.voxel=false;f.clouds.coverage=.85f;f.clouds.steps=32;f.clouds.lightSteps=3;
    f.viewportWidth=80;f.viewportHeight=60;f.cameraPosition={0,10,0};f.nearPlane=.1f;f.farPlane=100000;f.timeSeconds=8;
    auto camera=[&](glm::vec3 direction,float fov=50.f){f.view=glm::lookAt(f.cameraPosition,f.cameraPosition+glm::normalize(direction),glm::vec3(0,1,0));glm::mat4 correction(1);correction[2][2]=.5f;correction[3][2]=.5f;f.viewProjection=correction*glm::perspective(glm::radians(fov),float(f.viewportWidth)/f.viewportHeight,f.nearPlane,f.farPlane)*f.view;};
    camera({0,.7f,-1});renderer.render(f,{});auto counts=renderer.cloudTileCounts();auto values=renderer.readClouds();auto meta=renderer.readCloudMetadata();
    check(counts[0]>0 && counts[0]<=counts[1],"Cloud GPU tile queue count outside capacity");
    double opacity=0;for(size_t i=0;i<values.size();i+=4){for(int c=0;c<4;c++)check(std::isfinite(values[i+c]),"Cloud output nonfinite");check(values[i+3]>=0 && values[i+3]<=1,"Cloud transmittance outside 0..1");opacity+=1-values[i+3];check(meta[i+2]<=f.clouds.steps,"Cloud primary raymarch exceeded budget");}
    check(opacity>1,"Dense clouds did not attenuate the sky");
    // A paused wind edit must not rewrite the cloud's accumulated position.
    f.clouds.temporal=false;renderer.render(f,{});auto paused=renderer.readClouds();
    f.clouds.wind={-90,70};renderer.render(f,{});check(renderer.readClouds()==paused,"Paused layer wind edit teleported cloud density");
    f.clouds.advection=glm::dvec2(200,-100);renderer.render(f,{});auto prescribed=renderer.readClouds();
    f.clouds.wind={20,-5};renderer.render(f,{});check(renderer.readClouds()==prescribed,"Shared layer offset depends on instantaneous wind");
    check(prescribed!=paused,"Layer shader ignored prescribed advection");
    f.clouds.advection.reset();f.clouds.temporal=true;
    f.timeSeconds+=.05f;renderer.render(f,{});for(float v:renderer.readClouds())check(std::isfinite(v),"Wind reprojection produced nonfinite history");
    camera({0,.7f,-1},65.f);renderer.render(f,{});auto projectionCut=renderer.readClouds();
    renderer.resetTemporal();renderer.render(f,{});check(renderer.readClouds()==projectionCut,"Cloud FOV cut retained temporal history");
    f.clouds.coverage=0;renderer.render(f,{});check(renderer.cloudTileCounts()[0]==0,"Zero coverage failed GPU dispatch culling");
    values=renderer.readClouds();for(size_t i=0;i<values.size();i+=4)check(values[i]==0 && values[i+1]==0 && values[i+2]==0 && values[i+3]==1,"Zero work retained stale cloud history");
    f.clouds.coverage=.85f;f.clouds.density=0;renderer.render(f,{});check(renderer.cloudTileCounts()[0]==0,"Zero density failed GPU dispatch culling");f.clouds.density=.0018f;
    camera({0,-1,-1});renderer.render(f,{});check(renderer.cloudTileCounts()[0]==0,"Clouds rendered through the planet below horizon");
    f.cameraPosition.y=2000;camera({0,.4f,-1});renderer.render(f,{});check(renderer.cloudTileCounts()[0]>0,"Inside-cloud shell intersection lost all rays");
    f.cameraPosition.y=6000;camera({0,-1,-1});renderer.render(f,{});check(renderer.cloudTileCounts()[0]>0,"Above-cloud view lost the front shell segment");
    f.cameraPosition.y=10;camera({0,.7f,-1});const auto inverseView=glm::inverse(f.view);
    glm::vec3 forward=-glm::vec3(inverseView[2]),right=glm::vec3(inverseView[0]),up=glm::vec3(inverseView[1]),center=f.cameraPosition+forward*10.f;
    std::vector<MeshVertex> vertices;for(auto p:{center-right*20.f-up*20.f,center+right*20.f-up*20.f,center+right*20.f+up*20.f,center-right*20.f+up*20.f})vertices.push_back({p,-forward,{0,0}});
    auto mesh=std::make_shared<GpuMesh>(device,vertices,std::vector<uint32_t>{0,1,2,0,2,3});MaterialDesc material;material.parameters.albedoAlpha={.2f,.4f,.3f,1};material.parameters.emissiveNormal.w=0;
    auto gpuMaterial=std::make_shared<GpuMaterial>(device,material);DrawPacket packet{mesh,gpuMaterial};
    renderer.render(f,{packet});check(renderer.cloudTileCounts()[0]==0,"Opaque foreground did not cull cloud tiles");auto blocked=renderer.readHDR();
    f.clouds.enabled=false;renderer.render(f,{packet});auto clear=renderer.readHDR();check(blocked==clear,"Cloud composite altered fully occluded geometry");
    renderer.resize(65,49);f.viewportWidth=65;f.viewportHeight=49;f.clouds.enabled=true;f.clouds.downsample=4;camera({0,.7f,-1});renderer.render(f,{});
    check(renderer.readClouds().size()==17*13*4 && renderer.cloudTileCounts()[1]==6,"Odd cloud viewport or resize lost edge tiles");
    f.clouds.seed++;renderer.render(f,{});for(float v:renderer.readClouds())check(std::isfinite(v),"Cloud seed invalidation produced nonfinite output");
    renderer.resize(80,60);f.viewportWidth=80;f.viewportHeight=60;f.cameraPosition={0,1100,1800};
    f.clouds.voxel=true;f.clouds.temporal=false;f.clouds.downsample=1;f.clouds.voxelResolution=64;
    f.clouds.coverage=.8f;f.clouds.density=.004f;f.clouds.steps=128;f.clouds.wind={0,0};camera({0,.2f,-1});renderer.render(f,{});
    auto pausedVoxel=renderer.readClouds();auto bakedVoxel=renderer.readCloudVoxels();
    f.clouds.wind={100,80};renderer.render(f,{});
    check(renderer.readClouds()==pausedVoxel&&renderer.readCloudVoxels()==bakedVoxel,"Paused voxel wind edit moved/rebaked the cloud");
    f.clouds.advection=glm::dvec2(0);renderer.render(f,{});auto originVoxel=renderer.readClouds();
    f.clouds.advection=glm::dvec2(200,-100);renderer.render(f,{});
    check(renderer.readClouds()!=originVoxel&&renderer.readCloudVoxels()==bakedVoxel,"Voxel advection ignored displacement or rebaked density");
    f.clouds.advection=glm::dvec2(0);renderer.render(f,{});
    auto voxels=renderer.readCloudVoxels(),distance=renderer.readCloudDistance();auto lightCache=renderer.readCloudLight();
    check(voxels.size()==528*528*4 && distance.size()==256*128*4,"Voxel atlas storage dimensions differ");
    for(float v:lightCache)check(std::isfinite(v) && v>=0,"Voxel light cache nonfinite or negative");
    f.clouds.lightSteps=6;renderer.render(f,{});check(renderer.readCloudLight()!=lightCache,"Voxel light quality change retained a stale cache");
    auto brickIndex=[](int x,int y,int z){return ((z/8*32+y)*256+z%8*32+x)*4;};
    const int count=32*32*32;
    // Exact 26-neighbor graph distance is the conservative Chebyshev field.
    // Compare both signs against a CPU multi-source breadth-first transform.
    for(int field=0;field<3;field++){
        const int channel=field==2?3:field;
        std::vector<int> reference(count,127);std::deque<int> queue;
        for(int z=0;z<32;z++)for(int y=0;y<32;y++)for(int x=0;x<32;x++){
            const int id=(z*32+y)*32+x;bool occupied=distance[brickIndex(x,y,z)+2]!=0;
            bool seed=occupied==(channel==0);
            if(field==2){
                bool core=true;for(int dz=-1;dz<=2;dz++)for(int dy=-1;dy<=2;dy++)for(int dx=-1;dx<=2;dx++){
                    int a=x*2+dx,b=y*2+dy,c=z*2+dz;
                    if(a<0 || b<0 || c<0 || a>=64 || b>=64 || c>=64){core=false;continue;}
                    const size_t texel=((c/8*66+b+1)*528+c%8*66+a+1)*4;
                    core=core && voxels[texel]==255;
                }
                seed=!core;
            }
            if(seed){reference[id]=0;queue.push_back(id);}
        }
        while(!queue.empty()){
            int id=queue.front();queue.pop_front();int x=id%32,y=id/32%32,z=id/1024;
            for(int dz=-1;dz<=1;dz++)for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
                int a=x+dx,b=y+dy,c=z+dz;if(a<0 || b<0 || c<0 || a>=32 || b>=32 || c>=32)continue;
                int next=(c*32+b)*32+a;if(reference[next]>reference[id]+1){reference[next]=reference[id]+1;queue.push_back(next);}
            }
        }
        for(int z=0;z<32;z++)for(int y=0;y<32;y++)for(int x=0;x<32;x++)check(distance[brickIndex(x,y,z)+channel]==reference[(z*32+y)*32+x],"GPU compressed signed distance differs from CPU transform");
    }
    auto accelerated=renderer.readClouds();auto acceleratedMeta=renderer.readCloudMetadata();double skipped=0;for(size_t i=3;i<acceleratedMeta.size();i+=4)skipped+=acceleratedMeta[i];
    check(skipped>0,"Voxel distance field did not accelerate empty space");
    f.clouds.coreIntegration=false;renderer.render(f,{});auto fineCore=renderer.readClouds();
    double coreError=0,coreEnergy=0;for(size_t i=0;i<fineCore.size();i+=4){
        for(int c=0;c<3;c++){coreError+=std::abs(fineCore[i+c]-accelerated[i+c]);coreEnergy+=std::abs(fineCore[i+c]);}
        check(std::abs(fineCore[i+3]-accelerated[i+3])<=.011f,"Homogeneous cloud core changed extinction beyond cutoff tolerance");
    }
    check(coreError/std::max(coreEnergy,1e-8)<.05,"Cloud core light integration exceeded 5 percent relative L1");
    f.clouds.distanceSkipping=false;renderer.render(f,{});auto uniform=renderer.readClouds();
    for(size_t i=0;i<uniform.size();i++)check(std::abs(uniform[i]-fineCore[i])<.002f,"Conservative voxel skipping changed the integrated cloud");
    f.clouds.distanceSkipping=true;f.clouds.coreIntegration=true;f.cameraPosition={0,2200,-3000};camera({0,.3f,-1});renderer.render(f,{});
    check(renderer.cloudTileCounts()[0]>0,"Immersive camera lost the volume interval");
    for(float v:renderer.readClouds())check(std::isfinite(v),"Cloud interior produced nonfinite output");
    f.clouds.storm=1;f.clouds.lightning=25;renderer.render(f,{});auto flashed=renderer.readClouds();
    f.clouds.lightning=0;renderer.render(f,{});auto unlit=renderer.readClouds();double emission=0;for(size_t i=0;i<flashed.size();i+=4)emission+=flashed[i]+flashed[i+1]+flashed[i+2]-unlit[i]-unlit[i+1]-unlit[i+2];
    check(emission>0,"Internal cloud lightning added no emission");
    f.cameraPosition+=glm::vec3(500,200,-900);f.clouds.wind={100,80};f.timeSeconds+=.05f;camera({.4f,.1f,-1});renderer.render(f,{});auto fast=renderer.readClouds();
    renderer.resetTemporal();renderer.render(f,{});check(renderer.readClouds()==fast,"Full-resolution no-history voxel clouds retained fast-motion ghosts");
    f.clouds.voxelResolution=128;f.clouds.storm=0;renderer.render(f,{});check(renderer.readCloudVoxels().size()==2080*1040*4,"128-cubed voxel resize failed");
    f.cameraPosition={-1100,2100,-3600};f.clouds.wind={0,0};camera({0,.15f,-1});renderer.render(f,{});auto coreImage=renderer.readClouds();auto coreMeta=renderer.readCloudMetadata();
    f.clouds.coreIntegration=false;renderer.render(f,{});auto fineImage=renderer.readClouds();auto fineMeta=renderer.readCloudMetadata();
    double coreCost=0,fineCost=0,coreComparison=0,coreReference=0;
    for(size_t i=0;i<coreImage.size();i+=4){coreCost+=coreMeta[i+2];fineCost+=fineMeta[i+2];
        for(int c=0;c<3;c++){coreComparison+=std::abs(coreImage[i+c]-fineImage[i+c]);coreReference+=std::abs(fineImage[i+c]);}
        check(std::abs(coreImage[i+3]-fineImage[i+3])<=.011f,"128-cubed core aggregation lost homogeneous extinction");
    }
    check(coreCost<fineCost,"Homogeneous cloud core did not reduce primary samples");
    check(coreComparison/std::max(coreReference,1e-8)<.05,"128-cubed core radiance approximation exceeds tolerance");
    std::cout<<"RHI cloud core aggregate samples "<<coreCost<<" / fine "<<fineCost<<", radiance relative L1 "<<coreComparison/std::max(coreReference,1e-8)<<"\n";
    f.clouds.coreIntegration=true;
    f.clouds.coverage=0;renderer.render(f,{});check(renderer.cloudTileCounts()[0]==0,"Zero voxel coverage retained indirect work");
    // Runtime coverage changes must reuse all three caches, including crossing
    // zero. The occupancy envelope covers every threshold of the potential.
    f.cameraPosition={0,1100,1800};camera({0,.2f,-1});f.clouds.voxelResolution=64;
    f.clouds.dynamicCoverage=true;f.clouds.coverage=0;f.clouds.temporal=false;
    f.clouds.advection=glm::dvec2(0);f.clouds.coreIntegration=false;renderer.render(f,{});
    const auto cacheCounts=renderer.cloudCacheBakes();const auto envelope=renderer.readCloudVoxels();
    const auto envelopeDistance=renderer.readCloudDistance();const auto coverageLight=renderer.readCloudLight();
    check(renderer.cloudTileCounts()[0]==0,"Dynamic clear sky retained indirect work");
    check(coverageLight.size()==2112*528*4,"16-node coverage cache layout differs");
    for(size_t i=0;i<coverageLight.size();++i)check(std::isfinite(coverageLight[i])&&coverageLight[i]>=0,"Dynamic coverage tau nonfinite");
    for(int z=0;z<64;++z)for(int y=0;y<64;++y)for(int x=0;x<64;++x){
        float prior=0;for(int k=0;k<16;++k){size_t at=((z/8*66+y+1)*2112+k/4*528+z%8*66+x+1)*4+k%4;
            check(coverageLight[at]>=prior,"Solar tau knots are not monotone with coverage");prior=coverageLight[at];}
    }
    // Independent CPU quadrature at solar-cache voxel centers. This verifies
    // all channels against the same quantized potential, not another GPU pass.
    auto potentialAt=[&](glm::dvec3 uv){
        if(glm::any(glm::lessThan(uv,glm::dvec3(0)))||glm::any(glm::greaterThan(uv,glm::dvec3(1))))return 0.;
        glm::dvec3 pos=uv*64.-.5,base=glm::floor(pos),frac=pos-base;double result=0;
        for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x){
            int iz=std::clamp(int(base.z)+z,0,63),ix=int(base.x)+x,iy=int(base.y)+y;
            const size_t at=((iz/8*66+iy+1)*528+iz%8*66+ix+1)*4;
            result+=double(envelope[at+2])/255*.425*(x?frac.x:1-frac.x)*(y?frac.y:1-frac.y)*(z?frac.z:1-frac.z);
        }return result;
    };
    const double elevation=glm::radians(double(f.sunAngle)),azimuth=glm::radians(double(f.sunAzimuth));
    const glm::dvec3 direction{std::cos(elevation)*std::sin(azimuth),std::sin(elevation),-std::cos(elevation)*std::cos(azimuth)};
    const double reach=glm::length(glm::dvec3(f.clouds.volumeSize));const int lightSamples=int(f.clouds.lightSteps)*4;
    double maxKnotError=0,maxIntermediateTransmissionError=0;
    for(int z:{0,16,32,48,63})for(int y:{0,16,32,48,63})for(int x:{0,16,32,48,63}){
        glm::dvec3 uv=(glm::dvec3(x,y,z)+.5)/64.;std::array<double,31> taus{};
        for(int j=0;j<lightSamples;++j){
            double a=double(j)/lightSamples,b=double(j+1)/lightSamples,lo=a*a*reach,hi=b*b*reach;
            double potential=potentialAt(uv+direction*(lo+hi)*.5/glm::dvec3(f.clouds.volumeSize));
            for(int k=0;k<31;++k)taus[k]+=std::clamp((potential-(1.-double(k)/30)*.3)*8.,0.,1.)*f.clouds.density*(hi-lo);
        }
        for(int k=0;k<16;++k){const size_t at=((z/8*66+y+1)*2112+k/4*528+z%8*66+x+1)*4+k%4;
            double error=std::abs(coverageLight[at]-taus[k*2]);maxKnotError=std::max(maxKnotError,error);
            check(error<=.002+taus[k*2]*.001,"Solar coverage knot differs from CPU potential quadrature");}
        for(int k=0;k<15;++k)maxIntermediateTransmissionError=std::max(maxIntermediateTransmissionError,
            std::abs(std::exp(-.5*(taus[k*2]+taus[k*2+2]))-std::exp(-taus[k*2+1])));
    }
    check(maxIntermediateTransmissionError<.06,"Coverage midpoint transmission approximation exceeded sampled 0.06 guard");
    std::cout<<"RHI coverage solar cache CPU knot max tau error "<<maxKnotError<<", sampled intermediate transmission max absolute approximation error "<<maxIntermediateTransmissionError<<"\n";
    double priorOpacity=0;
    for(float coverage:{.01f,.05f,.25f,.5f,.75f,1.f}){
        f.clouds.coverage=coverage;f.clouds.distanceSkipping=true;renderer.render(f,{});auto skip=renderer.readClouds();
        double sum=0;for(size_t i=0;i<skip.size();i+=4){for(int c=0;c<4;++c)check(std::isfinite(skip[i+c]),"Dynamic coverage output nonfinite");sum+=1-skip[i+3];}
        check(sum>=priorOpacity,"Increasing dynamic coverage reduced opacity");priorOpacity=sum;
        f.clouds.distanceSkipping=false;renderer.render(f,{});auto dense=renderer.readClouds();
        for(size_t i=0;i<dense.size();++i)check(std::abs(dense[i]-skip[i])<.002f,"Coverage envelope incorrectly skipped growing cloud density");
        check(renderer.cloudCacheBakes()==cacheCounts,"Continuous coverage rebuilt a GPU cache");
    }
    check(priorOpacity>1,"Dynamic coverage never grew a visible cloud");
    check(renderer.readCloudVoxels()==envelope&&renderer.readCloudDistance()==envelopeDistance&&renderer.readCloudLight()==coverageLight,
          "Dynamic coverage mutated baked cache contents");
    auto pausedCoverage=renderer.readClouds();renderer.render(f,{});check(renderer.readClouds()==pausedCoverage,"Paused dynamic coverage changed output");
    // Saturated-potential cores are conservative even at the lowest threshold.
    f.clouds.distanceSkipping=true;f.clouds.coreIntegration=true;renderer.render(f,{});auto coreCoverage=renderer.readClouds();
    for(size_t i=3;i<coreCoverage.size();i+=4)check(std::abs(coreCoverage[i]-pausedCoverage[i])<=.011f,"Dynamic core integration changed extinction");
    f.clouds.temporal=true;f.clouds.coverage=.5f;renderer.render(f,{});
    f.clouds.coverage=0;renderer.render(f,{});auto dynamicClear=renderer.readClouds();
    for(size_t i=0;i<dynamicClear.size();i+=4)check(dynamicClear[i]==0&&dynamicClear[i+1]==0&&dynamicClear[i+2]==0&&dynamicClear[i+3]==1,
          "Clearing dynamic coverage retained a temporal ghost");
    check(renderer.cloudCacheBakes()==cacheCounts,"Dynamic zero endpoint rebuilt caches");
    f.clouds.seed++;renderer.render(f,{});auto reseeded=renderer.cloudCacheBakes();
    check(reseeded[0]==cacheCounts[0]+1&&reseeded[1]==cacheCounts[1]+1&&reseeded[2]==cacheCounts[2]+1,"Structural edit failed to invalidate dynamic caches once");
    std::cout<<"RHI continuous cloud coverage: monotone growth/clear, zero-work history, conservative skip/core, fixed tau knots, pause, no cache rebakes and structural invalidation passed\n";
    std::cout<<"RHI immersive voxel clouds: quantized density, signed distance CPU reference, conservative skip equivalence, light cache, inside camera, internal lightning and 128-cubed resize passed; skipped primary samples "<<skipped<<", core radiance relative L1 "<<coreError/std::max(coreEnergy,1e-8)<<"\n";
    std::cout<<"RHI shared cloud wind: paused layer/voxel continuity, prescribed displacement and unchanged density cache passed\n";
    std::cout<<"RHI GPU-driven clouds: compact indirect queue, zero-work clear, depth occlusion, inside/above shell, wind history, seed and odd resize passed; initial summed opacity "<<opacity<<"\n";
}
}
