#include "renderer/rhi/GpuHiZCuller.h"
#include "renderer/rhi/ShadowRenderer.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
namespace render {
namespace {
rhi::ShaderAsset asset(const std::string& directory,const std::string& name){const auto p=directory+"/"+name;return {p+".glsl",p+".metallib",p+".spv",p+".json","main0"};}
rhi::BindingLayout buildParameters(){return {0,{{0,rhi::BindingType::UniformBuffer,rhi::ShaderStage::Compute,"HiZBuild",16}}};}
rhi::BindingLayout buildImages(){return {1,{{0,rhi::BindingType::SampledTexture,rhi::ShaderStage::Compute,"hizSource",0},{1,rhi::BindingType::StorageTextureWrite,rhi::ShaderStage::Compute,"hizTarget",0,rhi::Format::R32Float}}};}
rhi::BindingLayout fusedImages(){auto layout=buildImages();for(uint32_t i=2;i<4;++i)layout.entries.push_back({i,rhi::BindingType::StorageTextureWrite,rhi::ShaderStage::Compute,i==2?"hizTarget1":"hizTarget2",0,rhi::Format::R32Float});return layout;}
rhi::BindingLayout testParameters(){return {0,{{0,rhi::BindingType::UniformBuffer,rhi::ShaderStage::Compute,"HiZParameters",16}}};}
rhi::BindingLayout testData(){return {1,{{0,rhi::BindingType::SampledTexture,rhi::ShaderStage::Compute,"hizDepth",0},{1,rhi::BindingType::StorageRead,rhi::ShaderStage::Compute,"HiZBounds",48},{2,rhi::BindingType::StorageWrite,rhi::ShaderStage::Compute,"HiZArguments",20}}};}
rhi::BindingLayout combineParameters(){return {0,{{0,rhi::BindingType::UniformBuffer,rhi::ShaderStage::Compute,"HiZCombine",16}}};}
rhi::BindingLayout combineData(){return {1,{{0,rhi::BindingType::StorageRead,rhi::ShaderStage::Compute,"TriangleArguments",20},{1,rhi::BindingType::StorageReadWrite,rhi::ShaderStage::Compute,"HiZArguments",20}}};}
bool eligible(const DrawPacket& p){
    return p.mesh && p.material && p.mesh->ready() && !p.mesh->indirectBuffer() && !p.mesh->instances() &&
        !p.wireframe && !p.material->transparent() && p.material->extension().lobes.w==0 &&
        (p.material->extension().features.x&int(MaterialFeature::Shoreline))==0 &&
        p.model[0].w==0 && p.model[1].w==0 && p.model[2].w==0 && p.model[3].w==1;
}
// Expand clip bounds for both float matrix multiplies; double CPU arithmetic
// alone does not bound cancellation in the float vertex shader in large worlds.
bool project(const DrawPacket& p,const FrameData& frame,glm::vec4& rectangle,float& depth){
    const auto lo=p.mesh->boundsMin(),hi=p.mesh->boundsMax();
    for(int c=0;c<3;++c)if(!std::isfinite(lo[c]) || !std::isfinite(hi[c]) || lo[c]>hi[c])return false;
    const auto model=glm::dmat4(p.model),vp=glm::dmat4(frame.viewProjection);
    const auto magnitude=glm::dvec4(glm::max(glm::abs(lo),glm::abs(hi)),1);
    glm::dvec4 worldMagnitude(0),error(0);
    for(int c=0;c<4;++c)worldMagnitude+=glm::abs(model[c])*magnitude[c];
    for(int c=0;c<4;++c)error+=glm::abs(vp[c])*worldMagnitude[c];
    error=error*2e-6+glm::dvec4(1e-7);
    glm::dvec2 minimum(std::numeric_limits<double>::infinity()),maximum(-std::numeric_limits<double>::infinity());
    double nearest=1;
    for(int corner=0;corner<8;++corner){
        const auto clip=vp*(model*glm::dvec4(corner&1?hi.x:lo.x,corner&2?hi.y:lo.y,corner&4?hi.z:lo.z,1));
        for(int c=0;c<4;++c)if(!std::isfinite(clip[c]) || !std::isfinite(error[c]))return false;
        // Near/eye intersections cannot be represented by this projected AABB.
        if(clip.w<=error.w+1e-6 || clip.z<=error.z+1e-6)return false;
        for(int axis=0;axis<2;++axis)for(int sign:{-1,1})for(int wSign:{-1,1}){
            const auto value=(clip[axis]+sign*error[axis])/(clip.w+wSign*error.w);
            minimum[axis]=std::min(minimum[axis],value);maximum[axis]=std::max(maximum[axis],value);
        }
        nearest=std::min(nearest,(clip.z-error.z)/(clip.w+error.w));
    }
    // Bounds outside the screen are already handled by frustum culling; fail
    // open when that stage was disabled. Pixel-space y is top-to-bottom.
    if(maximum.x < -1 || minimum.x>1 || maximum.y < -1 || minimum.y>1)return false;
    glm::dvec4 pixels((minimum.x*.5+.5)*frame.viewportWidth-2,
        (.5-maximum.y*.5)*frame.viewportHeight-2,
        (maximum.x*.5+.5)*frame.viewportWidth+2,
        (.5-minimum.y*.5)*frame.viewportHeight+2);
    pixels.x=std::clamp(pixels.x,0.,double(frame.viewportWidth-1));pixels.z=std::clamp(pixels.z,0.,double(frame.viewportWidth-1));
    pixels.y=std::clamp(pixels.y,0.,double(frame.viewportHeight-1));pixels.w=std::clamp(pixels.w,0.,double(frame.viewportHeight-1));
    rectangle=glm::vec4(pixels);depth=float(std::max(nearest-1e-6,0.));return true;
}
}
struct GpuHiZCuller::Targets {
    Resources resources;
    uint32_t width,height,sourceWidth,sourceHeight,baseMip,levels=1;size_t capacity;bool external;
    rhi::TextureViewHandle depthView,chain;
    std::array<rhi::TextureViewHandle,2> unused;
    rhi::TextureHandle pyramid;
    std::vector<rhi::TextureViewHandle> mips;
    std::vector<rhi::BindingSetHandle> sources;
    rhi::BufferHandle arguments;
    size_t bytes=0;
    Targets(std::shared_ptr<rhi::GraphicsDevice> device,uint32_t w,uint32_t h,size_t count,rhi::SamplerHandle sampler,bool externalDepth=false,uint32_t base=0):resources(device),width(std::max(w>>base,1u)),height(std::max(h>>base,1u)),sourceWidth(w),sourceHeight(h),baseMip(base),capacity(count),external(externalDepth){
        w=width;h=height;
        using namespace rhi;
        for(uint32_t extent=std::max(w,h);extent>1;extent>>=1)++levels;
        if(!external){const auto depth=resources.texture({w,h,Format::Depth32Float,TextureUsage::DepthAttachment|TextureUsage::Sampled,"HiZ current occluder depth"});depthView=resources.view(depth);}
        pyramid=resources.texture({w,h,Format::R32Float,TextureUsage::Storage|TextureUsage::Sampled|TextureUsage::CopySource,"HiZ farthest-depth pyramid",levels});chain=resources.view(pyramid,{0,levels,0,1});
        for(auto& v:unused)v=resources.view(resources.texture({1,1,Format::R32Float,TextureUsage::Storage,"HiZ unused output"}));
        auto previous=depthView;bytes=external?0:size_t(w)*h*4;
        for(uint32_t mip=0;mip<levels;++mip){
            mips.push_back(resources.view(pyramid,{mip,1,0,1}));
            sources.push_back(previous?resources.bindings({buildImages(),{{0,{},0,0,previous,sampler},{1,{},0,0,mips.back(),{}}}}):rhi::BindingSetHandle{});previous=mips.back();
            bytes+=size_t(std::max(w>>mip,1u))*std::max(h>>mip,1u)*4;
        }
        arguments=resources.buffer({count*20,BufferUsage::Storage|BufferUsage::Indirect|BufferUsage::CopySource|BufferUsage::CopyDestination,"HiZ main-camera indexed arguments",BufferInitialization::Uninitialized});bytes+=count*20;
    }
};
GpuHiZCuller::GpuHiZCuller(std::shared_ptr<rhi::GraphicsDevice> d,const std::string& directory):resources_(d){
    using namespace rhi;
    sampler_=resources_.sampler({Filter::Nearest,AddressMode::ClampToEdge});
    GraphicsPipelineDesc depth;depth.vertex=asset(directory,"forward.vert");depth.fragment=asset(directory,"shadow-depth.frag");
    depth.vertexStride=sizeof(MeshVertex);depth.attributes=GpuMesh::attributes();depth.bindings={ShadowRenderer::objectLayout(),GpuMaterial::shadowLayout()};
    depth.colorAttachment=false;depth.depthAttachment=depth.depthTest=depth.depthWrite=true;depth.label="HiZ selected occluders";depthPipeline_=resources_.pipeline(depth);
    ComputePipelineDesc build;build.shader=asset(directory,"hiz-build.comp");build.threads={8,8,1};build.bindings={buildParameters(),buildImages()};build.label="HiZ farthest-depth reduction";buildPipeline_=resources_.computePipeline(build);
    if(d->computeLimits().maxStorageImages>=3){build.shader=asset(directory,"hiz-build-fused.comp");build.bindings={buildParameters(),fusedImages()};build.label="HiZ three-level tile reduction";fusedPipeline_=resources_.computePipeline(build);}
    ComputePipelineDesc test;test.shader=asset(directory,"hiz-test.comp");test.threads={64,1,1};test.bindings={testParameters(),testData()};test.label="HiZ object tests";testPipeline_=resources_.computePipeline(test);
    ComputePipelineDesc combine;combine.shader=asset(directory,"hiz-combine.comp");combine.threads={1,1,1};combine.bindings={combineParameters(),combineData()};combine.label="HiZ triangle composition";combinePipeline_=resources_.computePipeline(combine);

}
GpuHiZCuller::~GpuHiZCuller()=default;
bool GpuHiZCuller::prepare(const FrameData& view,const std::vector<DrawPacket>& packets){
    stats_={};committed_=false;inputs_.clear();occluders_.clear();
    const auto limits=resources_.device->computeLimits();
    if(!view.hizCulling || packets.size()<2 || packets.size()>UINT32_MAX/5 || packets.size()*48>limits.maxStorageRange ||
        packets.size()*20>limits.maxStorageRange || (packets.size()+63)/64>limits.maxGroups[0])return false;
    const bool clusterMode=view.clusters.enabled&&view.clusters.batched&&view.clusters.occlusion&&!view.clusters.wireframe;
    inputs_.resize(packets.size());std::vector<size_t> candidates;
    for(size_t i=0;i<packets.size();++i){
        auto& input=inputs_[i];input={};input.counts.x=packets[i].mesh->indexCount();
        if(!eligible(packets[i]))continue;
        input.depth.z=1;++stats_.candidates;
        float depth=0;
        if(!project(packets[i],view,input.rectangle,depth)){
            // Near/eye crossings cannot be tested as objects, but their current
            // selected geometry can still safely contribute rasterized depth.
            if(clusterMode&&packets[i].clusters)candidates.push_back(i);
            continue;
        }
        input.depth={depth,1e-4f,1,1};
        const float area=(input.rectangle.z-input.rectangle.x+1)*(input.rectangle.w-input.rectangle.y+1)/float(double(view.viewportWidth)*view.viewportHeight);
        if(area>=view.hizOccluderMinArea&&(!packets[i].clusters||clusterMode))candidates.push_back(i);
    }
    std::stable_sort(candidates.begin(),candidates.end(),[&](size_t a,size_t b){return inputs_[a].depth.x<inputs_[b].depth.x;});
    for(auto i:candidates){
        const size_t triangles=packets[i].mesh->indexCount()/3;
        if(occluders_.size()>=view.hizMaxOccluders)break;
        if(triangles>view.hizOccluderTriangleBudget-stats_.prepassTriangles)continue;
        occluders_.push_back(i);inputs_[i].depth.w=0;stats_.prepassTriangles+=triangles;
    }
    // No occluder or all candidates already drawn as occluders: skip the chain.
    if(occluders_.empty() || (!clusterMode&&stats_.candidates<=occluders_.size()))return false;
    try{
        if(!targets_ || targets_->external || targets_->width!=view.viewportWidth || targets_->height!=view.viewportHeight || targets_->capacity<packets.size())
            targets_=std::make_unique<Targets>(resources_.device,view.viewportWidth,view.viewportHeight,packets.size(),sampler_);
    }catch(const rhi::ResourceBudgetExceeded&){stats_={};return false;}
    stats_.active=true;stats_.occluders=occluders_.size();stats_.pyramidLevels=targets_->levels;stats_.bufferBytes=targets_->bytes+inputs_.size()*sizeof(Input);return true;
}
std::vector<CulledMeshDraw> GpuHiZCuller::record(Resources& frame,rhi::CommandList& commands,const FrameData& view,const std::vector<DrawPacket>& packets,const std::vector<CulledMeshDraw>& triangles){
    recordPyramid(frame,commands,view,packets,triangles);
    return recordVisibility(frame,commands,view,packets,triangles);
}
void GpuHiZCuller::recordPyramid(Resources& frame,rhi::CommandList& commands,const FrameData& view,const std::vector<DrawPacket>& packets,const std::vector<CulledMeshDraw>& triangles){
    using namespace rhi;if(!stats_.active)return;
    auto camera=frame.buffer({64,BufferUsage::Uniform,"HiZ current jittered VP"},&view.viewProjection);
    RenderPassDesc pass;pass.depth=targets_->depthView;commands.setLabel("hiz/occluder-depth");commands.beginRenderPass(pass);commands.bindPipeline(depthPipeline_);
    for(auto i:occluders_){
        const std::array<glm::mat4,2> matrices={packets[i].model,glm::transpose(glm::inverse(packets[i].model))};
        auto object=frame.buffer({128,BufferUsage::Uniform,"HiZ occluder model"},matrices.data());
        commands.bindBindingSet(frame.bindings({ShadowRenderer::objectLayout(),{{0,camera,0,64,{},{}},{1,object,0,128,{},{}}}}));
        packets[i].material->bindShadow(commands);
        if(triangles[i].arguments)packets[i].mesh->draw(commands,triangles[i].indices,triangles[i].arguments,triangles[i].argumentOffset);
        else packets[i].mesh->draw(commands);
    }
    commands.endRenderPass();
    recordDepthPyramid(frame,commands,targets_->depthView,"hiz/pyramid-");
}
void GpuHiZCuller::recordDepthCopy(Resources& frame,rhi::CommandList& commands,rhi::TextureViewHandle source,rhi::TextureViewHandle target,uint32_t width,uint32_t height){
    const glm::uvec4 copy(1,0,0,0);auto uniform=frame.buffer({16,rhi::BufferUsage::Uniform,"HiZ diagnostic depth copy"},&copy);
    auto parameters=frame.bindings({buildParameters(),{{0,uniform,0,16,{},{}}}});
    auto images=frame.bindings({buildImages(),{{0,{},0,0,source,sampler_},{1,{},0,0,target,{}}}});
    commands.setLabel("diagnostic/early-depth");commands.dispatch(buildPipeline_,{parameters,images},{(width+7)/8,(height+7)/8,1});
}
void GpuHiZCuller::recordDepthPyramid(Resources& frame,rhi::CommandList& commands,rhi::TextureViewHandle source,const char* prefix){
    using namespace rhi;if(!stats_.active)return;
    uint32_t firstMip=0;
    if(targets_->baseMip){
        const glm::uvec4 options(2,0,0,0); // Conservative 4x4 base, including odd trailing texels.
        auto data=frame.buffer({16,BufferUsage::Uniform,"HiZ coarse-base options"},&options);
        auto parameters=frame.bindings({buildParameters(),{{0,data,0,16,{},{}}}});
        auto images=frame.bindings({buildImages(),{{0,{},0,0,source,sampler_},{1,{},0,0,targets_->mips[0],{}}}});
        commands.setLabel(std::string(prefix)+"0-coarse");commands.dispatch(buildPipeline_,{parameters,images},{(targets_->width+7)/8,(targets_->height+7)/8,1});firstMip=1;
    }
    if(fusedBuild_&&fusedPipeline_){
        for(uint32_t mip=firstMip;mip<targets_->levels;mip+=3){
            const glm::uvec4 options(mip==0?1u:0u,std::min(3u,targets_->levels-mip),0,0);
            auto data=frame.buffer({16,BufferUsage::Uniform,"HiZ fused options"},&options);
            auto parameters=frame.bindings({buildParameters(),{{0,data,0,16,{},{}}}});
            auto images=frame.bindings({fusedImages(),{{0,{},0,0,mip?targets_->mips[mip-1]:source,sampler_},
                {1,{},0,0,targets_->mips[mip],{}},{2,{},0,0,mip+1<targets_->levels?targets_->mips[mip+1]:targets_->unused[0],{}},
                {3,{},0,0,mip+2<targets_->levels?targets_->mips[mip+2]:targets_->unused[1],{}}}});
            commands.setLabel(std::string(prefix)+std::to_string(mip)+"-fused");
            commands.dispatch(fusedPipeline_,{parameters,images},{(std::max(targets_->width>>mip,1u)+7)/8,(std::max(targets_->height>>mip,1u)+7)/8,1});
        }return;
    }
    const glm::uvec4 copy(1,0,0,0),reduce(0);
    auto copyData=frame.buffer({16,BufferUsage::Uniform,"HiZ depth copy options"},&copy);
    auto reduceData=frame.buffer({16,BufferUsage::Uniform,"HiZ reduction options"},&reduce);
    auto copySet=frame.bindings({buildParameters(),{{0,copyData,0,16,{},{}}}});
    auto reduceSet=frame.bindings({buildParameters(),{{0,reduceData,0,16,{},{}}}});
    for(uint32_t mip=firstMip;mip<targets_->levels;++mip){
        commands.setLabel(std::string(prefix)+std::to_string(mip));
        commands.dispatch(buildPipeline_,{mip?reduceSet:copySet,mip?targets_->sources[mip]:frame.bindings({buildImages(),{{0,{},0,0,source,sampler_},{1,{},0,0,targets_->mips[0],{}}}})},
            {(std::max(targets_->width>>mip,1u)+7)/8,(std::max(targets_->height>>mip,1u)+7)/8,1});
    }
 }
std::vector<CulledMeshDraw> GpuHiZCuller::recordVisibility(Resources& frame,rhi::CommandList& commands,const FrameData& view,const std::vector<DrawPacket>& packets,const std::vector<CulledMeshDraw>& triangles){
    using namespace rhi;auto draws=triangles;if(!stats_.active)return draws;
    const glm::uvec4 parameters(uint32_t(inputs_.size()),view.viewportWidth,view.viewportHeight,targets_->levels);
    auto uniform=frame.buffer({16,BufferUsage::Uniform,"HiZ object parameters"},&parameters);
    auto bounds=frame.buffer({inputs_.size()*sizeof(Input),BufferUsage::Storage,"HiZ conservative screen bounds"},inputs_.data());
    const auto argumentsBytes=targets_->capacity*20;
    auto a=frame.bindings({testParameters(),{{0,uniform,0,16,{},{}}}});
    auto b=frame.bindings({testData(),{{0,{},0,0,targets_->chain,sampler_},{1,bounds,0,inputs_.size()*sizeof(Input),{},{}},{2,targets_->arguments,0,argumentsBytes,{},{}}}});
    commands.setLabel("hiz/object-tests");commands.dispatch(testPipeline_,{a,b},{uint32_t((inputs_.size()+63)/64),1,1});
    for(size_t i=0;i<packets.size();++i)if(inputs_[i].depth.z>0){
        if(view.clusters.enabled&&view.clusters.batched&&view.clusters.occlusion&&packets[i].clusters)continue;
        if(triangles[i].arguments){
            const glm::uvec4 offsets(uint32_t(i*5),uint32_t(triangles[i].argumentOffset/4),0,0);
            auto data=frame.buffer({16,BufferUsage::Uniform,"HiZ composition offsets"},&offsets);
            auto first=frame.bindings({combineParameters(),{{0,data,0,16,{},{}}}});
            auto second=frame.bindings({combineData(),{{0,triangles[i].arguments,0,20+triangles[i].argumentOffset,{},{}},{1,targets_->arguments,0,argumentsBytes,{},{}}}});
            commands.setLabel("hiz/triangle-composition");commands.dispatch(combinePipeline_,{first,second},{1,1,1});
        }
        draws[i]={triangles[i].indices?triangles[i].indices:packets[i].mesh->indexBuffer(),targets_->arguments,i*20};
    }
    return draws;
}
rhi::TextureViewHandle GpuHiZCuller::pyramidView()const{return stats_.active?targets_->chain:rhi::TextureViewHandle{};}
uint32_t GpuHiZCuller::pyramidBaseMip()const{return stats_.active?targets_->baseMip:0;}
uint32_t GpuHiZCuller::pyramidLevels()const{return stats_.active?targets_->levels:0;}
std::vector<HiZCullCount> GpuHiZCuller::readCounts(){
    if(!stats_.active || !committed_)return {};
    std::vector<uint32_t> arguments(inputs_.size()*5);resources_.device->readBuffer(targets_->arguments,0,arguments.size()*4,arguments.data());
    std::vector<HiZCullCount> result;
    for(size_t i=0;i<inputs_.size();++i)if(inputs_[i].depth.z>0)
        result.push_back({i,inputs_[i].counts.x/3,arguments[i*5]/3,std::find(occluders_.begin(),occluders_.end(),i)!=occluders_.end()});
    return result;
}
std::vector<float> GpuHiZCuller::readDepth(uint32_t mip){
    if(!stats_.active || !committed_)return {};
    if(mip>=targets_->levels)throw std::out_of_range("HiZ diagnostic mip outside pyramid");
    auto bytes=resources_.device->readTextureSubresource(targets_->pyramid,{mip,0});
    std::vector<float> values(bytes.size()/sizeof(float));std::memcpy(values.data(),bytes.data(),bytes.size());return values;
}
std::vector<HiZCullBounds> GpuHiZCuller::bounds() const{
    if(!stats_.active || !committed_)return {};
    std::vector<HiZCullBounds> result;
    for(size_t i=0;i<inputs_.size();++i)if(inputs_[i].depth.z>0)
        result.push_back({i,inputs_[i].rectangle,inputs_[i].depth.x,inputs_[i].depth.w>0,
            std::find(occluders_.begin(),occluders_.end(),i)!=occluders_.end()});
    return result;
}
}

namespace render {
bool GpuHiZCuller::prepareDepth(uint32_t width,uint32_t height,uint32_t baseMip){
    if(baseMip!=0&&baseMip!=2)throw std::invalid_argument("HiZ supports full or quarter-resolution base");
    stats_={};committed_=false;inputs_.clear();occluders_.clear();
    try{if(!targets_||!targets_->external||targets_->sourceWidth!=width||targets_->sourceHeight!=height||targets_->baseMip!=baseMip)targets_=std::make_unique<Targets>(resources_.device,width,height,1,sampler_,true,baseMip);}
    catch(const rhi::ResourceBudgetExceeded&){return false;}
    stats_.active=true;stats_.pyramidLevels=targets_->levels;stats_.bufferBytes=targets_->bytes;return true;
}
}
