#include "PT/CpuPathTracer.h"
#include "PT/ValidationScenes.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>
namespace {
constexpr float pi=3.14159265358979323846f;
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
bool near(glm::vec3 a,glm::vec3 b,float tolerance=1e-4f){return glm::length(a-b)<tolerance;}
render::SnapshotDraw triangle(glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 color={1,1,1}) {
    render::SnapshotDraw draw;auto mesh=std::make_shared<render::MeshPayload>();const auto n=glm::normalize(glm::cross(b-a,c-a));
    mesh->vertices={{a,n,{.25f,.25f}},{b,n,{.25f,.25f}},{c,n,{.25f,.25f}}};mesh->indices={0,1,2};draw.mesh=mesh;draw.parameters.factors={0,.8f,1,0};draw.parameters.albedoAlpha=glm::vec4(color,1);return draw;
}
render::RenderWorldSnapshot snapshot(){render::RenderWorldSnapshot s;s.frame.cameraPosition={0,0,2};glm::mat4 conversion(1);conversion[2][2]=.5f;conversion[3][2]=.5f;s.frame.viewProjection=conversion*glm::perspective(glm::radians(50.f),1.f,.1f,100.f)*glm::lookAt(s.frame.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));return s;}
void samplers() {
    for(uint32_t dimension:{0u,1u,10u,511u,32767u}){std::vector<int> bins(1024,0);for(uint32_t i=0;i<1024;++i){float value=pt::sobolSample(738,i,dimension);check(value>=0 && value<1,"Sobol sample out of range");++bins[uint32_t(value*1024)];}for(auto count:bins)check(count==1,"Sobol lost one-dimensional stratification");}
    std::vector<int> cells(1024,0);for(uint32_t i=0;i<1024;++i){uint32_t x=uint32_t(pt::sobolSample(79,i,0)*32),y=uint32_t(pt::sobolSample(79,i,1)*32);++cells[y*32+x];}for(auto count:cells)check(count==1,"Sobol 2D net missed a stratum");
    auto a=pt::Random::forPixel(31,19,5,true),b=pt::Random::forPixel(31,19,5,true);a.dimension(259);b.dimension(259);check(a.uniform()==b.uniform(),"Sobol random access differs across clones");
    // A digital shift alone locks distant bounce coordinates together: this
    // integral becomes 0 or .5 instead of .25, regardless of sample count.
    for(uint32_t seed:{79u,738u,31u})for(uint32_t d:{258u,514u,24578u}){
        double product=0;uint32_t quadrant=0;constexpr uint32_t count=16384;
        for(uint32_t i=0;i<count;++i){float x=pt::sobolSample(seed,i,2),y=pt::sobolSample(seed,i,d);product+=x*y;quadrant+=x<.5f&&y<.5f;}
        check(std::abs(product/count-.25)<.008,"Sobol cross-bounce product integral is correlated");
        check(std::abs(double(quadrant)/count-.25)<.012,"Sobol cross-bounce quadrant integral is correlated");
    }
}
void roughDielectric() {
    for(bool front:{true,false})for(float roughness:{.35f,.65f}){
        pt::Surface s;s.normal=s.geometricNormal={0,0,1};s.ior=1.54f;s.frontFace=front;s.transmissionRoughness=roughness;
        glm::vec3 view=glm::normalize(glm::vec3(.7f,0,1));pt::Random rng(813);double accepted=0,pdfIntegral=0,sampleEnergy=0,integralEnergy=0;
        constexpr int samples=150000;for(int i=0;i<samples;++i){auto sample=pt::sampleBsdf(s,view,rng,pt::TransportMode::Importance);if(sample.pdf<=0)continue;++accepted;
            check(!sample.delta&&sample.transmission==(sample.direction.z<0),"Rough boundary flags or medium transition hemisphere differ");
            check(std::abs(sample.pdf-pt::bsdfPdf(s,view,sample.direction))<1e-5f,"Rough dielectric sample/pdf disagree");
            auto weight=sample.value*std::abs(sample.direction.z)/sample.pdf;check(std::isfinite(weight.x)&&weight.x>=0&&weight.x<=1.0001f,"Single-scatter dielectric gains energy");sampleEnergy+=weight.x;
            pt::Surface reverse=s;if(sample.transmission){reverse.frontFace=!front;reverse.normal=reverse.geometricNormal=-s.normal;}
            auto reciprocal=pt::evaluateBsdf(reverse,sample.direction,view,pt::TransportMode::Radiance);
            if(glm::length(reciprocal-sample.value)>=.003f*std::max(1.f,sample.value.x))std::cerr<<"Reciprocity r "<<roughness<<", front "<<front<<", trans "<<sample.transmission<<", direction "<<sample.direction.x<<", "<<sample.direction.y<<", "<<sample.direction.z<<", value "<<sample.value.x<<", reverse "<<reciprocal.x<<'\n';
            check(glm::length(reciprocal-sample.value)<.003f*std::max(1.f,sample.value.x),"Rough transmission radiance/importance reciprocity failed");
        }
        // Independent equal-solid-angle quadrature, not samples from the tested PDF.
        constexpr int nz=768,np=1024;for(int z=0;z<nz;++z)for(int p=0;p<np;++p){float cosine=-1+2.f*(z+.5f)/nz,phi=2*pi*(p+.5f)/np,sine=std::sqrt(1-cosine*cosine);glm::vec3 l{sine*std::cos(phi),sine*std::sin(phi),cosine};pdfIntegral+=pt::bsdfPdf(s,view,l)*4*pi/(nz*np);integralEnergy+=pt::evaluateBsdf(s,view,l,pt::TransportMode::Importance).x*std::abs(cosine)*4*pi/(nz*np);}
        std::cout<<"Rough dielectric front "<<front<<", r "<<roughness<<", accepted/pdf "<<accepted/samples<<" / "<<pdfIntegral<<", energy sample/integral "<<sampleEnergy/samples<<" / "<<integralEnergy<<'\n';
        check(std::abs(pdfIntegral-accepted/samples)<.025,"Rough dielectric PDF integral misses reflection/transmission mass");
        check(std::abs(integralEnergy-sampleEnergy/samples)<.025,"Rough dielectric estimator disagrees with independent integration");
    }
    pt::Surface matched;matched.normal=matched.geometricNormal={0,0,1};matched.ior=matched.exteriorIor=1.54f;matched.transmissionRoughness=.5f;pt::Random random(18);auto pass=pt::sampleBsdf(matched,{0,0,1},random);check(pass.delta&&pass.transmission&&near(pass.direction,{0,0,-1}),"Matched IOR did not become transparent delta boundary");
    auto source=pt::makeCausticsScene(8,8);source.snapshot.draws.back().pathTracingIor=1.54f;source.snapshot.draws.back().pathTracingRoughness=.4f;pt::CpuScene scene(source.snapshot);check(scene.exportData().materials.back().absorption.w==.4f,"Dielectric roughness ABI mismatch");bool rejected=false;try{scene.validateBidirectional();}catch(const std::invalid_argument &){rejected=true;}check(rejected,"BDPT accepted unimplemented rough strategy densities");
    for(float invalid:{-1.f,1.1f,NAN}){source.snapshot.draws.back().pathTracingRoughness=invalid;rejected=false;try{pt::CpuScene bad(source.snapshot);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Invalid dielectric roughness accepted");}
    // From the center of an absorbing cube, the first exit is one unit away.
    // Its transmitted environment connection lies in air, not the cube medium.
    auto box=pt::makeMediumValidationScene(8,8,true).snapshot;box.draws[0].pathTracingScattering=glm::vec3(0);box.draws[0].pathTracingAbsorption=glm::vec3(.4f);box.draws[0].pathTracingRoughness=.65f;
    pt::CpuScene exit(box);exit.environment=std::make_shared<pt::Environment>(8,4,std::vector<glm::vec3>(32,glm::vec3(1)));
    pt::Surface face;check(exit.intersect({0,0,0},{0,0,1},0,2,face),"Rough exit fixture missed boundary");face.exteriorIor=1;
    double expected=0,observed=0;constexpr int nz=512,np=512;
    for(int z=0;z<nz;++z)for(int p=0;p<np;++p){float cosine=(z+.5f)/nz,phi=2*pi*(p+.5f)/np,sine=std::sqrt(1-cosine*cosine);glm::vec3 l{sine*std::cos(phi),sine*std::sin(phi),cosine};expected+=pt::evaluateBsdf(face,{0,0,-1},l).x*cosine*2*pi/(nz*np)*std::exp(-.4f);}
    uint64_t rays=0;for(int i=0;i<60000;++i){pt::Random rng(i+101);observed+=exit.trace({0,0,0},{0,0,1},rng,1,rays).x/60000;}
    std::cout<<"Rough exit medium NEE sample/reference "<<observed<<" / "<<expected<<'\n';check(std::abs(observed-expected)<.015,"Transmitted NEE attenuated an air shadow segment with interior extinction");
}
void geometry() {
    auto s=snapshot();s.draws.push_back(triangle({-1,-1,0},{1,-1,0},{0,1,0},{.5f,.2f,.1f}));pt::CpuScene scene(s);pt::Surface hit;
    check(scene.meshCount()==1 && scene.triangles()==1,"Single-mesh objects must be imported");
    check(scene.intersect({0,0,2},{0,0,-1},0,10,hit),"Planar triangle AABB missed");check(std::abs(hit.distance-2)<1e-6f,"Triangle distance mismatch");
    check(near(hit.albedo,glm::pow(glm::vec3(.5f,.2f,.1f),glm::vec3(2.2f))),"Albedo factor / encoded-to-linear conversion mismatch");
    check(!scene.intersect({0,0,0},{1,0,0},0,10,hit),"Parallel ray accepted as a NaN hit");
    s.draws[0].model=glm::translate(glm::mat4(1),glm::vec3(2,0,0))*glm::scale(glm::mat4(1),glm::vec3(2,.5f,1));pt::CpuScene transformed(s);
    check(transformed.intersect({2,0,2},{0,0,-1},0,10,hit) && near(hit.normal,{0,0,1}),"Transform / inverse transpose normal mismatch");
    auto image=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,1,{255,0,0,255,0,255,0,0}});auto material=std::make_shared<render::MaterialPayload>();material->images[0]=image;
    s.draws[0].model=glm::mat4(1);s.draws[0].material=material;s.draws[0].parameters.albedoAlpha=glm::vec4(1);s.draws[0].parameters.factors.w=.5f;pt::CpuScene opaque(s);
    check(opaque.intersect({0,0,2},{0,0,-1},0,10,hit) && near(hit.albedo,{1,0,0}),"Texture orientation / alpha opaque texel mismatch");
    auto mesh=std::make_shared<render::MeshPayload>(*s.draws[0].mesh);for(auto &v:mesh->vertices)v.uv.x=.75f;s.draws[0].mesh=mesh;pt::CpuScene masked(s);check(!masked.intersect({0,0,2},{0,0,-1},0,10,hit),"Alpha mask did not skip geometry");
    auto mapped=std::make_shared<render::MaterialPayload>();mapped->images[1]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{255,128,255,255}});mapped->images[2]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{0,0,128,255}});mapped->images[3]=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{1,1,{0,64,0,255}});
    auto uvMesh=std::make_shared<render::MeshPayload>(*s.draws[0].mesh);uvMesh->vertices[0].uv={0,0};uvMesh->vertices[1].uv={1,0};uvMesh->vertices[2].uv={.5f,1};s.draws[0].mesh=uvMesh;s.draws[0].material=mapped;s.draws[0].parameters.factors={1,1,1,0};s.draws[0].parameters.emissiveNormal.w=1;pt::CpuScene normal(s);
    check(normal.intersect({0,0,2},{0,0,-1},0,10,hit) && hit.normal.x>.6f && hit.normal.z>.6f,"UV-derived normal-map tangent basis failed");check(std::abs(hit.metallic-128/255.f)<1e-5f && std::abs(hit.roughness-64/255.f)<1e-5f,"Packed metallic B / roughness G conversion failed");
    auto invalidImage=std::make_shared<render::ImageRGBA8>(render::ImageRGBA8{2,2,{0,0,0,255}});auto invalidMaterial=std::make_shared<render::MaterialPayload>();invalidMaterial->images[0]=invalidImage;s.draws[0].material=invalidMaterial;bool rejectedImage=false;try{pt::CpuScene invalid(s);}catch(const std::invalid_argument &){rejectedImage=true;}check(rejectedImage,"Truncated CPU material image was accepted");
    s.draws.clear();pt::Random r(9);
    for(int i=0;i<300;++i){glm::vec3 a{r.uniform()*8-4,r.uniform()*8-4,r.uniform()*8-4};s.draws.push_back(triangle(a,a+glm::vec3(.4f,0,.1f),a+glm::vec3(0,.5f,0)));}
    pt::CpuScene bvh(s);
    for(int i=0;i<10000;++i){glm::vec3 origin{r.uniform()*12-6,r.uniform()*12-6,r.uniform()*12-6};glm::vec3 direction=glm::normalize(glm::vec3(r.uniform()-.5f,r.uniform()-.5f,r.uniform()-.5f));pt::Surface fast,reference;bool a=bvh.intersect(origin,direction,1e-4f,100,fast),b=bvh.intersect(origin,direction,1e-4f,100,reference,true);check(a==b && (!a || std::abs(fast.distance-reference.distance)<1e-4f),"SAH BVH differs from brute-force triangles");}
    pt::CpuScene empty(snapshot());check(empty.nodeCount()==0 && !empty.intersect({0,0,0},{0,0,1},0,10,hit),"Empty scene acceleration failed");
}
void environment() {
    pt::Environment uniform(32,16,std::vector<glm::vec3>(512,glm::vec3(2)));pt::Random rng(123);glm::vec3 mean(0);
    for(int i=0;i<50000;++i){auto sample=uniform.sample(rng);check(std::abs(sample.pdf-1/(4*pi))<1e-5f,"Uniform HDR solid-angle PDF mismatch");check(std::abs(sample.pdf-uniform.pdf(sample.direction))<1e-5f,"Sample and evaluate environment PDF disagree");mean+=sample.direction;}
    check(glm::length(mean/50000.f)<.02f,"Environment sampling is not uniform in solid angle");
    std::vector<glm::vec3> pixels(32*16,glm::vec3(.01f));for(int y=4;y<8;++y)for(int x=8;x<12;++x)pixels[y*32+x]=glm::vec3(10);pt::Environment bright(32,16,pixels);double normalization=0;
    for(int y=0;y<16;++y)for(int x=0;x<32;++x){float theta=(y+.5f)/16*pi,phi=(x+.5f)/32*2*pi;glm::vec3 d{std::sin(theta)*std::sin(phi),std::cos(theta),-std::sin(theta)*std::cos(phi)};double solid=2*pi/32*(std::cos(pi*y/16)-std::cos(pi*(y+1)/16));normalization+=bright.pdf(d)*solid;}
    check(std::abs(normalization-1)<1e-5,"Nonuniform environment PDF does not integrate to one");
    int chosen=0;for(int i=0;i<10000;++i){auto sample=bright.sample(rng);check(std::abs(sample.pdf-bright.pdf(sample.direction))<1e-3f,"HDR importance PDF mismatch");if(sample.radiance.x>1)++chosen;}check(chosen>9000,"HDR importance sampler missed the bright patch");
    check(near(uniform.evaluate({0,1,0}),{2,2,2}),"Environment pole sampling failed");
}
void bsdf() {
    pt::Surface s;s.albedo={.8f,.4f,.1f};s.roughness=.6f;s.normal=s.geometricNormal={0,0,1};pt::Random rng(817);
    for(float metallic:{0.f,1.f}) {s.metallic=metallic;glm::vec3 estimate(0),integral(0);
        for(int i=0;i<100000;++i){auto sample=pt::sampleBsdf(s,{0,0,1},rng);if(sample.pdf>0){check(std::abs(sample.pdf-pt::bsdfPdf(s,{0,0,1},sample.direction))<1e-5f,"BSDF sample PDF mismatch");estimate+=sample.value*(std::max(0.f,sample.direction.z)/sample.pdf);}const float z=rng.uniform(),phi=2*pi*rng.uniform(),r=std::sqrt(1-z*z);glm::vec3 d{r*std::cos(phi),r*std::sin(phi),z};integral+=pt::evaluateBsdf(s,{0,0,1},d)*(z*2*pi);}
        estimate/=100000.f;integral/=100000.f;check(near(estimate,integral,.015f),"GGX/diffuse sample estimator does not match independent hemispherical integration");check(glm::all(glm::lessThanEqual(estimate,glm::vec3(1.02f))),"White furnace produces energy gain");
    }
    s.metallic=1;s.roughness=.55f;glm::vec3 view=glm::normalize(glm::vec3(1,0,.12f)),estimate(0),integral(0);
    for(int i=0;i<150000;++i){auto sample=pt::sampleBsdf(s,view,rng);if(sample.pdf>0){check(std::abs(sample.pdf-pt::bsdfPdf(s,view,sample.direction))<1e-5f,"Grazing VNDF PDF mismatch");estimate+=sample.value*(std::max(0.f,sample.direction.z)/sample.pdf);}float z=rng.uniform(),phi=2*pi*rng.uniform(),q=std::sqrt(1-z*z);integral+=pt::evaluateBsdf(s,view,{q*std::cos(phi),q*std::sin(phi),z})*(z*2*pi);}
    check(near(estimate/150000.f,integral/150000.f,.015f),"Grazing VNDF estimator loses energy");
    check(near(pt::evaluateBsdf(s,{0,0,1},{0,0,-1}),{0,0,0}),"BSDF leaks below the geometric surface");
}
void transport() {
    auto s=snapshot();s.draws.push_back(triangle({-20,-20,0},{20,-20,0},{0,20,0},{.7f,.25f,.1f}));pt::CpuScene scene(s);
    scene.environment=std::make_shared<pt::Environment>(16,8,std::vector<glm::vec3>(128,glm::vec3(1)));
    pt::Random r(58);glm::vec3 result(0),reference(0);uint64_t rays=0;
    pt::Surface surface;check(scene.intersect({0,0,2},{0,0,-1},1e-4f,10,surface),"Transport fixture missed");
    for(int i=0;i<50000;++i){result+=scene.trace({0,0,2},{0,0,-1},r,1,rays);const float z=r.uniform(),phi=2*pi*r.uniform(),q=std::sqrt(1-z*z);reference+=pt::evaluateBsdf(surface,{0,0,1},{q*std::cos(phi),q*std::sin(phi),z})*(z*2*pi);}
    check(near(result/50000.f,reference/50000.f,.007f),"Environment NEE + BSDF MIS double counts or loses energy");
    s.frame.lights.push_back({{0,0,0,0},{2,2,2,0},{0,0,-1,0}});pt::CpuScene visible(s);glm::vec3 lit=visible.trace({0,0,2},{0,0,-1},r,1,rays);
    s.draws.push_back(triangle({-.5f,-.5f,1},{.5f,-.5f,1},{0,.5f,1}));pt::CpuScene blocked(s);auto dark=blocked.trace({0,0,.5f},{0,0,-1},r,1,rays);check(lit.r>.05f && dark.r<1e-6f,"Direct-light visibility failed");
}
void rendering() {
    auto s=snapshot();s.draws.push_back(triangle({-20,-20,0},{20,-20,0},{0,20,0},{.7f,.25f,.1f}));
    s.frame.lights.push_back({{0,0,0,0},{3,3,3,0},{0,0,-1,0}});pt::CpuScene scene(s);scene.environment=std::make_shared<pt::Environment>(16,8,std::vector<glm::vec3>(128,glm::vec3(.3f)));
    pt::Options options;options.width=32;options.height=24;options.samples=20;options.maxDepth=4;options.threads=1;
    int callbacks=0;auto first=pt::render(scene,options,[&](const auto &image){++callbacks;check(image.samples==4 || image.samples==16 || image.samples==20,"Unexpected progressive sample count");});options.threads=3;auto second=pt::render(scene,options);
    check(callbacks==3 && first.samples==20 && first.radiance==second.radiance,"Renderer depends on worker scheduling or misses tiles");
    check(first.nonFiniteSamples==0 && first.radiance[0].r>first.radiance[0].g*2,"Material colors lost during transport");
    pt::writeImage(first,1,"build/path-tracing/tests/fixture");pt::writeReport(first,scene,options,"build/path-tracing/tests/fixture","fixture");check(std::filesystem::file_size("build/path-tracing/tests/fixture.pfm")==size_t(32*24*12+14),"PFM float layout/header mismatch");
    bool rejected=false;options.samples=0;try{pt::render(scene,options);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"Zero samples were accepted");
    options.samples=256;options.width=16;options.height=16;options.minimumSamples=32;options.adaptive=true;options.threads=1;
    pt::CpuScene empty(snapshot());empty.environment=std::make_shared<pt::Environment>(8,4,std::vector<glm::vec3>(32,glm::vec3(.5f)));auto adaptive=pt::render(empty,options);options.threads=3;auto again=pt::render(empty,options);
    check(adaptive.convergedPixels==256 && adaptive.totalSamples<uint64_t(256)*256,"Adaptive pixels did not stop on constant radiance");check(adaptive.sampleCounts==again.sampleCounts && adaptive.radiance==again.radiance,"Adaptive stopping depends on thread count");for(auto value:adaptive.radiance)check(near(value,glm::vec3(.5f),1e-6f),"Adaptive mean changed constant environment");
    options.adaptive=false;auto fixed=pt::render(empty,options);check(fixed.totalSamples==uint64_t(256)*256 && fixed.radiance==adaptive.radiance,"Fixed spp reference lost samples");
    // An emissive triangle must illuminate a receiving surface through explicit light sampling.
    s.frame.lights.clear();auto emitter=triangle({-.5f,-.5f,1},{0,.5f,1},{.5f,-.5f,1});emitter.parameters.emissiveNormal={10,10,10,0};s.draws.push_back(emitter);pt::CpuScene area(s);pt::Random r(33);glm::vec3 energy(0);uint64_t rays=0;
    for(int i=0;i<1000;++i)energy+=area.trace({.8f,0,.5f},{0,0,-1},r,1,rays);check(energy.r/1000>.02f,"Emissive triangle NEE produced no illumination");
}
void dielectricAndBdpt() {
    check(std::abs(pt::dielectricFresnel(1,1,1.5f)-.04f)<1e-6f,"Glass normal-incidence Fresnel is not 4%");
    check(pt::dielectricFresnel(.3f,1.5f,1)==1,"Glass total internal reflection failed");
    pt::Surface glass;glass.ior=1.5f;glass.normal=glass.geometricNormal={0,0,1};glass.albedo=glm::vec3(1);pt::Random random(418);glm::vec3 radiance(0),importance(0);int reflections=0;
    for(int i=0;i<25000;++i){auto a=pt::sampleBsdf(glass,{0,0,1},random),b=pt::sampleBsdf(glass,{0,0,1},random,pt::TransportMode::Importance);check(a.delta&&a.pdf>0,"Glass event must use discrete delta PDF");radiance+=a.value*(std::abs(a.direction.z)/a.pdf);importance+=b.value*(std::abs(b.direction.z)/b.pdf);reflections+=a.direction.z>0;}
    check(std::abs(float(reflections)/25000-.04f)<.006f,"Fresnel branch frequencies changed");check(near(importance/25000.f,glm::vec3(1),.001f),"Importance glass gained/lost energy");check(near(radiance/25000.f,glm::vec3(.04f+.96f/2.25f),.007f),"Radiance glass omitted eta squared");
    auto s=snapshot();s.draws.push_back(triangle({-20,-20,0},{20,-20,0},{0,20,0},{.7f,.7f,.7f}));auto emitter=triangle({-.3f,-.3f,1},{0,.3f,1},{.3f,-.3f,1});emitter.parameters.albedoAlpha={0,0,0,1};emitter.parameters.emissiveNormal={6,6,6,0};s.draws.push_back(emitter);pt::CpuScene scene(s);
    for(int y=0;y<10;++y)for(int x=0;x<10;++x){glm::vec3 o,d;float u=(x+.5f)/10,v=(y+.5f)/10;scene.cameraRay(u,v,o,d);glm::vec2 uv;float pdf;check(scene.project(o+d*2.f,uv,pdf)&&glm::length(uv-glm::vec2(u,v))<1e-5f&&pdf>0,"BDPT camera projection/directional PDF failed");}
    pt::Options options;options.width=16;options.height=16;options.samples=512;options.maxDepth=2;options.threads=2;options.adaptive=false;auto reference=pt::render(scene,options);options.bdpt=true;auto bidirectional=pt::render(scene,options);double a=0,b=0;
    for(size_t i=0;i<reference.radiance.size();++i){a+=reference.radiance[i].x;b+=bidirectional.radiance[i].x;}std::cout<<"BDPT/PT area-light energy ratio: "<<b/a<<"\n";check(std::abs(b/a-1)<.04,"BDPT connections/splats double count or lose area-light energy");
    bool rejected=false;scene.environment=std::make_shared<pt::Environment>(2,1,std::vector<glm::vec3>(2,glm::vec3(1)));try{pt::render(scene,options);}catch(const std::invalid_argument &){rejected=true;}check(rejected,"BDPT must reject unsupported HDR endpoint instead of losing energy");
    auto validation=pt::makeCausticsScene(32,24);pt::CpuScene caustics(validation.snapshot,validation.dielectrics);options.width=32;options.height=24;options.samples=64;options.maxDepth=6;auto image=pt::render(caustics,options);check(image.nonFiniteSamples==0,"BDPT caustics produced non-finite pixels");float maximum=0;for(auto value:image.radiance)maximum=std::max(maximum,value.x);check(maximum>.1f,"BDPT refractive caustic scene has no energy");
    double causticEnergy=0;for(size_t i=0;i<image.caustics.size();++i){causticEnergy+=image.caustics[i].x;check(glm::all(glm::lessThanEqual(image.caustics[i],image.radiance[i]+glm::vec3(1e-5f))),"Caustic AOV exceeds complete transport");}check(causticEnergy>.1,"BDPT did not construct specular-to-diffuse caustic paths");
    auto noGlass=pt::makeCausticsScene(32,24,false);pt::CpuScene control(noGlass.snapshot);auto clean=pt::render(control,options);for(auto value:clean.caustics)check(near(value,glm::vec3(0)),"No-glass control contains false caustic paths");
}

}
int main(){try{samplers();roughDielectric();geometry();environment();bsdf();transport();rendering();dielectricAndBdpt();std::cout<<"CPU path tracing geometry, HDR PDF, GGX, MIS/render scheduling and output tests passed\n";return 0;}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
