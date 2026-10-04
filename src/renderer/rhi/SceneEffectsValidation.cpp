#include "renderer/rhi/ForwardPbrRenderer.h"
#include "renderer/rhi/ShadowRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <cmath>
#include <algorithm>
namespace render {
namespace {void check(bool v,const char* text){if(!v)throw std::runtime_error(text);}}
void validateSceneEffects(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& directory){
    auto quad=[&](float radius){std::vector<MeshVertex> v{{{-radius,-radius,0},{0,0,1},{0,1}},{{radius,-radius,0},{0,0,1},{1,1}},{{radius,radius,0},{0,0,1},{1,0}},{{-radius,radius,0},{0,0,1},{0,0}}};return std::make_shared<GpuMesh>(device,v,std::vector<uint32_t>{0,1,2,0,2,3});};
    MaterialDesc m;m.parameters.factors={0,.8f,1,0};auto material=std::make_shared<GpuMaterial>(device,m);
    auto plane=quad(2),blocker=quad(.3f);glm::mat4 model(1);model[3]={-.45f,0,.7f,1};
    std::vector<DrawPacket> draws{{plane,material,glm::mat4(1)},{blocker,material,model}};
    FrameData f;f.view=glm::lookAt(glm::vec3(0,0,3),glm::vec3(0),glm::vec3(0,1,0));f.nearPlane=.1f;f.farPlane=20;
    glm::mat4 correction(1);correction[2][2]=.5f;correction[3][2]=.5f;f.viewProjection=correction*glm::perspective(glm::radians(70.f),1.f,f.nearPlane,f.farPlane)*f.view;
    f.lights={{{0,0,0,0},{3,3,3,0},{.6f,0,-1,0}}};
    ForwardPbrRenderer renderer(device,directory,96,96,PbrPath::Scene);renderer.render(f,draws);const auto lit=renderer.readHDR();
    f.forwardShading=true;renderer.render(f,draws);const auto forward=renderer.readHDR();float maxDifference=0;for(size_t i=0;i<lit.size();++i)maxDifference=std::max(maxDifference,std::abs(lit[i]-forward[i]));check(maxDifference<.025f,"Scene forward/deferred PBR disagreement");f.forwardShading=false;
    f.inverseSquareLocalLights=true;f.lights={{{0,0,2,1},{3,3,3,0},{0,0,0,0}}};renderer.render(f,{{plane,material,glm::mat4(1)}});auto closeLight=renderer.readHDR();f.lights[0].positionType.z=4;renderer.render(f,{{plane,material,glm::mat4(1)}});auto farLight=renderer.readHDR();const size_t sample=(48*96+48)*4;float ambient=f.ambient;check(std::abs((closeLight[sample]-ambient)/(farLight[sample]-ambient)-4)<.08f,"Local light did not retain inverse-square attenuation");f.inverseSquareLocalLights=false;f.lights={{{0,0,0,0},{3,3,3,0},{.6f,0,-1,0}}};
    f.shadows=true;renderer.render(f,draws);const auto shadowed=renderer.readHDR(),positions=renderer.readGBuffer(0),depth=renderer.readShadowDepth();
    size_t darker=0,unchanged=0;for(size_t i=0;i<lit.size();i+=4){check(std::isfinite(shadowed[i]),"Shadow output nonfinite");if(positions[i+3]==1 && std::abs(positions[i+2])<.01f){if(lit[i]-shadowed[i]>.15f)++darker;if(std::abs(lit[i]-shadowed[i])<.002f)++unchanged;}}
    check(darker>8 && unchanged>100,"Directional shadow visibility did not separate blocker and lit receiver");
    size_t written=0;for(float d:depth){check(std::isfinite(d)&&d>=0&&d<=1,"Shadow depth outside 0..1");if(d<1)++written;}check(written>100,"Shadow atlas was not populated");
    if(device->computeLimits().maxStorageImages){
        f.taa=true;renderer.render(f,draws);auto first=renderer.readShadowDepth();renderer.render(f,draws);auto second=renderer.readShadowDepth();
        check(first==second,"TSAA jitter moved the CSM depth atlas");f.taa=false;
    }
    {
        // Kilometre coordinates must preserve sub-metre positions and the same
        // shadows in forward/deferred paths, including geometric receiver bias.
        auto large=f;const glm::vec3 offset(2600.125f,440.25f,1750.375f);
        const auto translation=glm::translate(glm::mat4(1),offset);
        large.view=f.view*glm::translate(glm::mat4(1),-offset);
        large.viewProjection=f.viewProjection*glm::translate(glm::mat4(1),-offset);
        large.cameraPosition=offset+glm::vec3(0,0,3);
        std::vector<DrawPacket> shifted=draws;for(auto& draw:shifted)draw.model=translation*draw.model;
        renderer.render(large,shifted);auto deferred=renderer.readHDR(),world=renderer.readGBuffer(0);
        size_t precise=0;for(size_t i=0;i<world.size();i+=4)if(world[i+3]==1 && std::abs(world[i+2]-offset.z)<.001f)++precise;
        check(precise>100,"Large-world G-buffer lost sub-metre position precision");
        large.forwardShading=true;renderer.render(large,shifted);auto direct=renderer.readHDR();
        size_t divergent=0;for(size_t i=0;i<world.size();i+=4)if(world[i+3]==1 && std::abs(deferred[i]-direct[i])>.05f)++divergent;
        std::cout<<"Large-world shadow differing pixels: "<<divergent<<"\n";
        // Allow at most 1% of pixels for geometric derivatives at silhouettes.
        check(divergent<96,"Large-world deferred shadows disagree with forward shading");
        std::cout<<"Large-world float32 positions and forward/deferred shadow agreement passed\n";
    }
    // Alpha cutoff must remove the caster from both depth and RSM.
    m.parameters.factors.w=.5f;m.images[0]={1,1,{255,255,255,0}};auto hole=std::make_shared<GpuMaterial>(device,m);
    renderer.render(f,{{plane,material,glm::mat4(1)},{blocker,hole,model}});const auto noCaster=renderer.readHDR();size_t restored=0;
    for(size_t i=0;i<lit.size();i+=4)if(lit[i]-shadowed[i]>.15f && positions[i+3]==1 && std::abs(positions[i+2])<.01f && noCaster[i]>shadowed[i]+.1f)++restored;
    check(restored>8,"Alpha cutoff did not remove shadow caster");
    auto sunLights=f.lights;
    for(const auto& local:std::vector<LightData>{{{.6f,0,3,1},{3,3,3,0},{0,0,0,0}},{{0,0,3,2},{3,3,3,.95f},{0,0,-1,.75f}}}){
        f.lights={local};f.shadows=false;renderer.render(f,draws);auto noShadow=renderer.readHDR();f.shadows=true;renderer.render(f,draws);auto withShadow=renderer.readHDR();size_t blocked=0;for(size_t i=0;i<withShadow.size();i+=4)if(positions[i+3]==1 && std::abs(positions[i+2])<.01f && noShadow[i]-withShadow[i]>.15f)++blocked;check(blocked>8,"Point/spot shadow visibility did not block the receiver");
    }f.lights=sunLights;
    {
        // Cascade construction must retain near precision in a kilometre-scale
        // view, and sub-texel translation must leave the world XY lattice stable.
        ShadowRenderer stable(device,directory,32);auto wide=f;wide.farPlane=16000;
        wide.shadowSettings.distance=300;wide.shadowSettings.cascadeBlend=.1f;
        wide.viewProjection=correction*glm::perspective(glm::radians(70.f),1.f,wide.nearPlane,wide.farPlane)*wide.view;
        stable.render(wide,draws);auto before=stable.data();
        check(before.cascades.y==300 && before.splits[0].x<25,"CSM large-world distance consumed near-cascade resolution");
        check(before.rects[0].z*stable.extent()>140,"CSM did not use available atlas area");
        auto axis=glm::normalize(glm::vec3(wide.lights[0].directionOuter)),right=glm::normalize(glm::cross(axis,glm::vec3(0,1,0)));
        float texel=2.f/glm::length(glm::vec3(before.matrices[0][0][0],before.matrices[0][1][0],before.matrices[0][2][0]))/(before.rects[0].z*stable.extent());
        wide.view=wide.view*glm::translate(glm::mat4(1),-right*texel*.01f);
        wide.viewProjection=correction*glm::perspective(glm::radians(70.f),1.f,wide.nearPlane,wide.farPlane)*wide.view;
        stable.render(wide,draws);auto after=stable.data();
        for(int c=0;c<4;++c)for(int row=0;row<2;++row)check(std::abs(before.matrices[0][c][row]-after.matrices[0][c][row])<1e-5f,"CSM lattice swims under sub-texel camera motion");
        for(float depth:{before.splits[0].x*.95f,before.splits[0].x}){
            const auto inverse=glm::inverse(f.view);glm::vec3 receiver=glm::vec3(inverse*glm::vec4(0,0,-depth,1));
            for(int cascade=0;cascade<2;++cascade){auto clip=before.matrices[cascade]*glm::vec4(receiver,1);glm::vec3 p=glm::vec3(clip)/clip.w;
                check(std::abs(p.x)<=1 && std::abs(p.y)<=1 && p.z>=0 && p.z<=1,"CSM blend band is not covered by both cascades");}
        }
        std::cout<<"CSM large-world resolution, sub-texel snapping and overlapping partitions passed\n";
    }
    if(device->computeLimits().supported){
        using namespace rhi;Resources resources(device);auto path=directory+"/shadow-validation.comp";
        ComputePipelineDesc p;p.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};p.threads={64,1,1};
        p.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Compute,"ShadowTestLights",480},
            {1,BindingType::StorageRead,ShaderStage::Compute,"ShadowTestPoints",16},
            {2,BindingType::StorageWrite,ShaderStage::Compute,"ShadowTestResults",16},
            {3,BindingType::UniformBuffer,ShaderStage::Compute,"ShadowData",sizeof(ShadowParameters)}}},
            {1,{{5,BindingType::SampledTexture,ShaderStage::Compute,"shadowAtlas",0}}}};
        auto kernel=resources.computePipeline(p);
        auto texture=resources.texture({128,64,Format::RGBA32Float,TextureUsage::Sampled|TextureUsage::CopyDestination,"Synthetic PCSS blockers"});
        auto view=resources.view(texture);auto sampler=resources.sampler({Filter::Nearest,AddressMode::ClampToEdge});
        std::array<glm::vec4,30> lights{};auto lightBuffer=resources.buffer({sizeof(lights),BufferUsage::Uniform|BufferUsage::CopyDestination,"Shadow test lights"},lights.data());
        auto test=[&](const ShadowParameters& data,const std::vector<glm::vec4>& points){
            lights[0].w=float(points.size());device->writeBuffer(lightBuffer,0,sizeof(lights),lights.data());
            Resources batch(device);auto uniforms=batch.buffer({sizeof(data),BufferUsage::Uniform,"Shadow test data"},&data);
            auto input=batch.buffer({points.size()*16,BufferUsage::Storage,"Shadow query points"},points.data());
            auto output=batch.buffer({points.size()*16,BufferUsage::Storage|BufferUsage::CopySource,"Shadow results"});
            auto a=batch.bindings({p.bindings[0],{{0,lightBuffer,0,sizeof(lights),{},{}},{1,input,0,points.size()*16,{},{}},
                {2,output,0,points.size()*16,{},{}},{3,uniforms,0,sizeof(data),{},{}}}});
            auto b=batch.bindings({p.bindings[1],{{5,{},0,0,view,sampler}}});auto commands=device->createCommandList();
            commands.dispatch(kernel,{a,b},{uint32_t((points.size()+63)/64),1,1});device->submit(commands);
            std::vector<glm::vec4> result(points.size());device->readBuffer(output,0,result.size()*16,result.data());return result;
        };
        ShadowParameters data;data.lights[0]={0,1,2,0};data.matrices[0]=glm::mat4(1);data.rects[0]={0,0,.5f,1};
        data.lightDepth[0]={0,100,2,0};data.filter={1,.02f,.05f,24};data.settings.x=0;
        std::vector<float> depths(128*64*4,.9f);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)if(x>=24 && x<40)depths[(y*128+x)*4]=.4f;
        // Adjacent atlas tile has unrelated depth: filters must not leak into it.
        for(int y=0;y<64;++y)for(int x=64;x<128;++x)depths[(y*128+x)*4]=0;
        device->writeTextureFloat(texture,depths.data(),depths.size()*4);
        std::vector<glm::vec4> points;for(float z:{.45f,.8f})for(int x=0;x<64;++x)points.push_back({(x+.5f)/32-1,0,z,0});
        auto result=test(data,points);uint32_t nearSoft=0,farSoft=0;
        for(int i=0;i<128;++i){check(std::isfinite(result[i].x)&&result[i].x>=0&&result[i].x<=1,"PCSS output invalid");
            if(result[i].x>.01f && result[i].x<.99f){if(i<64)++nearSoft;else ++farSoft;}}
        check(farSoft>nearSoft+4,"PCSS penumbra did not grow with blocker separation");
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)depths[(y*128+x)*4]=.9f;
        device->writeTextureFloat(texture,depths.data(),depths.size()*4);
        auto edge=test(data,{{.9999f,0,.8f,0},{.9999f,.9999f,.8f,0}});
        check(edge[0].x==1 && edge[1].x==1,"PCSS sampled an unrelated atlas tile");
        auto perspectiveDepth=[](float z){return 100.f/99.9f*(1-.1f/z);};
        data.lightDepth[0]={.1f,100,0,1};data.filter.z=.4f;
        for(int y=0;y<64;++y)for(int x=0;x<64;++x)depths[(y*128+x)*4]=perspectiveDepth(x>=24&&x<40?1.5f:90.f);
        device->writeTextureFloat(texture,depths.data(),depths.size()*4);points.clear();
        for(float z:{1.6f,4.f})for(int x=0;x<64;++x)points.push_back({(x+.5f)/32-1,0,perspectiveDepth(z),0});
        auto local=test(data,points);uint32_t localNear=0,localFar=0;
        for(int i=0;i<128;++i)if(local[i].x>.01f && local[i].x<.99f){if(i<64)++localNear;else ++localFar;}
        check(localFar>localNear+4,"Perspective PCSS penumbra did not grow in linear light space");
        data.lightDepth[0]={.1f,10,0,1};auto linear=test(data,{{0,0,.5f,0},{0,0,.5f,.5f},{0,0,.5f,1}});
        check(std::abs(linear[0].y-.1f)<1e-6 && std::abs(linear[1].y-(1.f/5.05f))<1e-6 && std::abs(linear[2].y-10)<.0001f,"PCSS perspective depth was not linearized");
        data.filter.x=0;data.lights[0]={0,5,0,0};data.cascades={.1f,20,.2f,.1f};data.splits[0]={2,4,8,12};
        for(int tile=0;tile<5;++tile){data.matrices[tile]=glm::mat4(1);data.matrices[tile][2][2]=0;data.matrices[tile][3][2]=.5f;
            data.rects[tile]={tile? .5f:0,0,.5f,1};data.lightDepth[0]={0,1,2,0};}
        for(int y=0;y<64;++y)for(int x=0;x<128;++x)depths[(y*128+x)*4]=x<64?.4f:.9f;
        device->writeTextureFloat(texture,depths.data(),depths.size()*4);
        auto blend=test(data,{{0,0,-1.61f,0},{0,0,-1.8f,0},{0,0,-1.9999f,0},{0,0,-2.0001f,0},{0,0,1,0},{0,0,-21,0}});
        check(blend[0].x<.01f && blend[1].x>.4f && blend[1].x<.6f && std::abs(blend[2].x-blend[3].x)<.001f,"CSM split transition is discontinuous");
        check(blend[4].x==1 && blend[5].x==1,"CSM shadowed behind-camera or out-of-range receivers");
        std::cout<<"PCSS linear depth, growing penumbra and atlas isolation; CSM split visibility continuity passed (soft pixels "<<nearSoft<<" -> "<<farSoft<<")\n";
    }
    f.ssao=true;renderer.render(f,draws);const auto ao=renderer.readSSAO();size_t occluded=0;
    for(size_t i=0;i<ao.size();i+=4){check(std::isfinite(ao[i])&&ao[i]>=0&&ao[i]<=1,"SSAO range invalid");if(ao[i]<.99f)++occluded;}check(occluded>8,"SSAO did not detect blocker contact");
    f.rsm=true;renderer.render(f,draws);for(float v:renderer.readHDR())check(std::isfinite(v),"RSM lighting nonfinite");
    // Six independent point faces and one spot tile, including read-only D32 sampling.
    ShadowRenderer atlas(device,directory,32);f.lights={{{0,0,2,1},{2,2,2,0},{0,0,0,0}},{{0,0,3,2},{2,2,2,.95f},{0,0,-1,.75f}}};atlas.render(f,draws);
    check(atlas.data().lights[0].y==6 && atlas.data().lights[1].x==6 && atlas.data().lights[1].y==1,"Point/spot shadow atlas allocation invalid");
    const auto flux=atlas.readRsm(0),rsmP=atlas.readRsm(1),rsmN=atlas.readRsm(2);size_t vpl=0;
    for(size_t i=0;i<flux.size();i+=4){check(std::isfinite(flux[i])&&std::isfinite(rsmP[i])&&std::isfinite(rsmN[i]),"RSM atlas nonfinite");if(rsmP[i+3]==1){++vpl;const float norm=rsmN[i]*rsmN[i]+rsmN[i+1]*rsmN[i+1]+rsmN[i+2]*rsmN[i+2];check(std::abs(norm-1)<.005f && flux[i]>=0,"RSM position/normal/flux packing invalid");}}check(vpl>20,"Point/spot RSM capture missing");
    // A dedicated source capture must store reflected power (area and irradiance),
    // and disabling sun bounce must remove that contribution independently of direct light.
    f.lights={{{0,0,0,0},{3,3,3,0},{0,0,-1,0}}};f.rsmSettings.useSunSky=true;f.rsmSettings.sunBounce=true;atlas.render(f,draws);
    auto sourceFlux=atlas.readRsmSource(0);double sunlight=0;const uint32_t tileX=0,tileY=0;
    for(uint32_t y=tileY;y<tileY+atlas.rsmExtent();++y)for(uint32_t x=tileX;x<tileX+atlas.rsmExtent();++x)sunlight+=sourceFlux[(size_t(y)*atlas.rsmExtent()+x)*4];check(sunlight>0,"Sun RSM source capture is empty");
    f.rsmSettings.sunBounce=false;atlas.render(f,draws);sourceFlux=atlas.readRsmSource(0);double disabled=0;for(uint32_t y=tileY;y<tileY+atlas.rsmExtent();++y)for(uint32_t x=tileX;x<tileX+atlas.rsmExtent();++x)disabled+=sourceFlux[(size_t(y)*atlas.rsmExtent()+x)*4];check(disabled<sunlight*.001,"Sun bounce toggle did not remove flux");f.rsmSettings.sunBounce=true;
    if(device->supportsWireframe()){renderer.render(f,{{plane,material,glm::mat4(1),0,true}});auto wire=renderer.readGBuffer(0);size_t covered=0;for(size_t i=3;i<wire.size();i+=4)covered+=wire[i]>.5f;check(covered>10 && covered<3000,"Wireframe pipeline did not rasterize triangle edges");}
    f.shadows=f.ssao=f.rsm=false;f.lights={{{0,0,0,0},{3,3,3,0},{-.4f,.2f,-1,0}}};
    renderer.render(f,{{plane,material,glm::mat4(1)}});auto plain=renderer.readHDR();
    auto changed=[&](MaterialExtension extension,const char* message){material->updateExtension(extension);renderer.render(f,{{plane,material,glm::mat4(1)}});auto result=renderer.readHDR();size_t count=0;for(size_t i=0;i<result.size();i+=4){check(std::isfinite(result[i]),"Extended material produced nonfinite color");if(std::abs(result[i]-plain[i])>.002f)++count;}check(count>10,message);};
    f.lights={{{0,0,0,0},{3,3,3,0},{.5f,0,1,0}}};renderer.render(f,{{plane,material,glm::mat4(1)}});auto oneSided=renderer.readHDR();MaterialExtension doubleSided;doubleSided.settings.w=1;material->updateExtension(doubleSided);renderer.render(f,{{plane,material,glm::mat4(1)}});auto twoSided=renderer.readHDR();check(twoSided[(48*96+48)*4]>oneSided[(48*96+48)*4]+.2f,"Two-sided foliage did not receive back lighting");material->updateExtension({});f.lights={{{0,0,0,0},{3,3,3,0},{-.4f,.2f,-1,0}}};
    MaterialExtension extra;extra.lobes={1,.2f,0,0};changed(extra,"Clearcoat lobe did not affect HDR");extra.lobes={0,.5f,.8f,0};changed(extra,"Anisotropic lobe did not affect HDR");
    extra.lobes={0,.5f,0,1};changed(extra,"SSS lobe did not affect HDR");material->updateExtension({});
    // CullFront must select the rear surface consistently after each backend's Y conversion.
    std::vector<MeshVertex> shellVertices;for(float z:{.3f,-.3f})for(auto v:std::vector<MeshVertex>{{{-1,-1,z},{0,0,1},{0,1}},{{1,-1,z},{0,0,1},{1,1}},{{1,1,z},{0,0,1},{1,0}},{{-1,1,z},{0,0,1},{0,0}}})shellVertices.push_back(v);
    auto shell=std::make_shared<GpuMesh>(device,shellVertices,std::vector<uint32_t>{0,1,2,0,2,3,4,6,5,4,7,6});extra.lobes={0,.5f,0,1};material->updateExtension(extra);renderer.render(f,{{shell,material,glm::mat4(1)}});auto backDepth=renderer.readBackDepth();auto clip=f.viewProjection*glm::vec4(0,0,-.3f,1);check(std::abs(backDepth[(48*96+48)*4]-clip.z/clip.w)<1e-5f,"SSS back-face culling selected the front surface");material->updateExtension({});
    // Two transparent layers must sort by camera distance, preserve opaque depth,
    // overwrite reactive motion independently from alpha blending and compose in HDR.
    MaterialDesc red;red.parameters.albedoAlpha={1,0,0,.5f};red.parameters.emissiveNormal.w=0;red.extension.settings.z=1;
    auto redMaterial=std::make_shared<GpuMaterial>(device,red);red.parameters.albedoAlpha={0,1,0,.5f};auto greenMaterial=std::make_shared<GpuMaterial>(device,red);
    auto far=glm::translate(glm::mat4(1),glm::vec3(0,0,.1f)),near=glm::translate(glm::mat4(1),glm::vec3(0,0,.4f));
    renderer.render(f,{{plane,greenMaterial,near},{plane,redMaterial,far}});auto alpha=renderer.readHDR();const size_t center=(48*96+48)*4;
    check(std::abs(alpha[center]-.25f)<.003f && std::abs(alpha[center+1]-.5f)<.003f,"Transparent layers composed in wrong order");
    renderer.render(f,{{plane,redMaterial,far},{plane,greenMaterial,near}});auto reordered=renderer.readHDR();check(alpha==reordered,"Transparent output depends on input order");
    red.parameters.albedoAlpha={0,0,1,1};auto blueMaterial=std::make_shared<GpuMaterial>(device,red);auto front=glm::translate(glm::mat4(1),glm::vec3(0,0,.8f));
    renderer.render(f,{{plane,redMaterial,far},{plane,blueMaterial,front},{plane,greenMaterial,near}});auto hidden=renderer.readHDR();check(hidden[center]<.002f && hidden[center+1]<.002f && std::abs(hidden[center+2]-1)<.003f,"Transparent pass did not honor opaque depth");
    if(device->computeLimits().maxStorageImages){f.taa=true;f.historyKey=71;renderer.render(f,{{plane,greenMaterial,near},{plane,redMaterial,far}});renderer.render(f,{{plane,greenMaterial,near},{plane,redMaterial,far}});alpha=renderer.readHDR();check(std::abs(alpha[center]-.25f)<.003f && std::abs(alpha[center+1]-.5f)<.003f,"Transparent reactive motion corrupted TAA");f.taa=false;}
    std::cout<<"RHI clearcoat, anisotropy, SSS, unlit, sorted transparent HDR, depth and independent reactive blending passed\n";
    renderer.resize(64,48);renderer.render(f,draws);check(renderer.readSSAO().size()==64*48*4,"Scene effects resize failed");
    std::cout<<"RHI cascaded/point/spot shadows, depth atlas, alpha casters, SSAO, RSM MRT and resize passed; shadow pixels "<<darker<<", AO pixels "<<occluded<<"\n";
}
}
