#include "renderer/rhi/AoReference.h"
#include "renderer/rhi/FeatureScenes.h"
#include "renderer/rhi/SceneAdapter.h"
#include "PT/CpuPathTracer.h"
#include "rhi/ShaderAssets.h"
#include "system/InputManager.h"
#include "utils/Utils.h"
#include "utils/Camera.h"
#include "renderer/RenderScene.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <stb/stb_image_write.h>
#include <json/json.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <map>
#include <iostream>
#include <cstdlib>
#include <cstring>
namespace render {
namespace {
using Json=nlohmann::json;
struct Config {
    std::filesystem::path directory="build/ao-benchmark";
    std::string scene="all",label="unspecified";
    AoReferenceOptions reference;
    int slices=4,steps=4;
    uint32_t frames=4,warmup=4,timingSamples=16,timingRepeats=8,timingWidth=960,timingHeight=720;
};
std::array<uint32_t,2> dimensions(const std::string& text) {
    const auto split=text.find('x');if(split==std::string::npos)throw std::invalid_argument("AO size expects WIDTHxHEIGHT");
    auto parse=[](const std::string& value){size_t end=0;const auto n=std::stoul(value,&end);if(end!=value.size()||n<16||n>4096)throw std::invalid_argument("AO dimension outside 16..4096");return uint32_t(n);};
    return {parse(text.substr(0,split)),parse(text.substr(split+1))};
}
Config options(int argc,char** argv) {
    Config config;int i=2;if(i<argc && std::string(argv[i]).rfind("--",0)!=0)config.directory=argv[i++];
    for(;i<argc;++i){
        const std::string key=argv[i];if(i+1>=argc)throw std::invalid_argument("Missing AO benchmark option value");const std::string value=argv[++i];
        if(key=="--backend")continue;
        if(key=="--ao-scene")config.scene=value;
        else if(key=="--ao-label")config.label=value;
        else if(key=="--ao-size"){auto size=dimensions(value);config.reference.width=size[0];config.reference.height=size[1];}
        else if(key=="--ao-timing-size"){auto size=dimensions(value);config.timingWidth=size[0];config.timingHeight=size[1];}
        else if(key=="--ao-radius"||key=="--ao-bias"||key=="--ao-power"){
            size_t end=0;const float n=std::stof(value,&end);if(end!=value.size()||!std::isfinite(n))throw std::invalid_argument("Invalid AO benchmark float");
            if(key=="--ao-radius")config.reference.radius=n;else if(key=="--ao-bias")config.reference.bias=n;else config.reference.power=n;
        } else {
            size_t end=0;const auto n=std::stoul(value,&end);if(end!=value.size()||n>65536)throw std::invalid_argument("Invalid AO benchmark integer");
            if(key=="--ao-samples")config.reference.samples=uint32_t(n);
            else if(key=="--ao-threads")config.reference.threads=uint32_t(n);
            else if(key=="--ao-motion-frames")config.frames=uint32_t(n);
            else if(key=="--ao-warmup")config.warmup=uint32_t(n);
            else if(key=="--ao-timing-samples")config.timingSamples=uint32_t(n);
            else if(key=="--ao-timing-repeats")config.timingRepeats=uint32_t(n);
            else if(key=="--ao-seed")config.reference.seed=n;
            else if(key=="--ao-slices")config.slices=int(n);
            else if(key=="--ao-steps")config.steps=int(n);
            else throw std::invalid_argument("Unknown AO benchmark option: "+key);
        }
    }
    if(config.reference.radius<=0||config.reference.radius>20||config.reference.bias<0||config.reference.bias>5||config.reference.power<=0||config.reference.power>4||config.slices<2||config.slices>8||config.steps<2||config.steps>8||!config.reference.samples||config.reference.threads<1||config.reference.threads>32||config.frames<1||config.frames>16||config.warmup>64||config.timingSamples<1||config.timingSamples>256||config.timingRepeats<1||config.timingRepeats>64||
       (config.scene!="all"&&config.scene!="cornell"&&config.scene!="sponza"&&config.scene!="contact"))throw std::invalid_argument("AO benchmark options outside domain");
    return config;
}
void save(const std::filesystem::path& path,uint32_t width,uint32_t height,const std::vector<uint8_t>& pixels) {
    if(!stbi_write_png(path.string().c_str(),int(width),int(height),4,pixels.data(),int(width*4)))throw std::runtime_error("Cannot save AO benchmark image");
}
void saveScalar(const std::filesystem::path& path,uint32_t width,uint32_t height,const std::vector<float>& values,bool rgba=false) {
    static_assert(sizeof(float)==4&&std::numeric_limits<float>::is_iec559,"AO PFM requires IEEE float32");
    std::ofstream file(path,std::ios::binary);file<<"Pf\n"<<width<<' '<<height<<"\n-1.0\n";
    for(uint32_t row=height;row>0;--row)for(uint32_t x=0;x<width;++x){
        float value=values[(size_t(row-1)*width+x)*(rgba?4:1)];uint32_t bits;std::memcpy(&bits,&value,4);
        const char bytes[]={char(bits),char(bits>>8),char(bits>>16),char(bits>>24)};file.write(bytes,4);
    }
    if(!file)throw std::runtime_error("Cannot save AO benchmark float32 PFM");
}
std::vector<uint8_t> grayscale(const std::vector<float>& values,bool rgba) {
    std::vector<uint8_t> pixels((rgba?values.size()/4:values.size())*4);
    for(size_t p=0;p<pixels.size()/4;++p){float v=values[p*(rgba?4:1)];if(!std::isfinite(v))throw std::runtime_error("Nonfinite AO benchmark output");for(int c=0;c<3;++c)pixels[p*4+c]=uint8_t(glm::clamp(v,0.f,1.f)*255);pixels[p*4+3]=255;}return pixels;
}
Json statistics(const std::vector<double>& samples) {
    if(samples.empty())return nullptr;auto sorted=samples;std::sort(sorted.begin(),sorted.end());
    auto quantile=[&](double q){const double at=q*(sorted.size()-1);const auto low=size_t(at),high=std::min(low+1,sorted.size()-1);return sorted[low]+(sorted[high]-sorted[low])*(at-low);};
    return {{"samples_ms",samples},{"mean_ms",std::accumulate(samples.begin(),samples.end(),0.)/samples.size()},{"median_ms",quantile(.5)},{"p95_ms",quantile(.95)},{"p95_to_median",quantile(.95)/quantile(.5)},{"unstable_samples",quantile(.95)>1.5*quantile(.5)},{"min_ms",sorted.front()},{"max_ms",sorted.back()}};
}
Json error(const std::vector<float>& ao,const AoReferenceImage& reference,int region=0) {
    double square=0,absolute=0,signedSum=0;size_t count=0;float peak=0;
    for(size_t p=0;p<reference.visibility.size();++p)if(reference.matched[p] && (region==0 || (region==1?reference.visibility[p]<.98f:reference.visibility[p]>.99f))){
        const double delta=ao[p*4]-reference.visibility[p];square+=delta*delta;absolute+=std::abs(delta);signedSum+=delta;peak=std::max(peak,float(std::abs(delta)));++count;
    }
    if(!count)return nullptr;return {{"pixels",count},{"rmse",std::sqrt(square/count)},{"mae",absolute/count},{"mean_signed_error",signedSum/count},{"max_absolute_error",peak}};
}
RenderWorldSnapshot contactFixture(uint32_t width,uint32_t height) {
    RenderWorldSnapshot world;world.frame.cameraPosition={2.5f,1.7f,3.5f};world.frame.view=glm::lookAt(world.frame.cameraPosition,glm::vec3(0,.65f,0),glm::vec3(0,1,0));
    glm::mat4 correction(1);correction[2][2]=.5f;correction[3][2]=.5f;world.frame.viewProjection=correction*glm::perspective(glm::radians(60.f),float(width)/height,.1f,30.f)*world.frame.view;
    world.frame.nearPlane=.1f;world.frame.farPlane=30;world.frame.viewportWidth=width;world.frame.viewportHeight=height;
    uint64_t id=800000;auto quad=[&](std::array<glm::vec3,4> points){
        auto mesh=std::make_shared<MeshPayload>();mesh->id=++id;mesh->revision=1;const auto normal=glm::normalize(glm::cross(points[1]-points[0],points[2]-points[0]));
        const glm::vec2 uv[]={{0,0},{1,0},{1,1},{0,1}};for(size_t i=0;i<4;++i)mesh->vertices.push_back({points[i],normal,uv[i]});mesh->indices={0,1,2,0,2,3};
        SnapshotDraw draw;draw.objectId=id;draw.mesh=mesh;auto material=std::make_shared<MaterialPayload>();material->id=id;material->revision=1;draw.material=material;draw.parameters.factors={0,.8f,1,0};world.draws.push_back(draw);
    };
    quad({glm::vec3(-3,0,3),{3,0,3},{3,0,-3},{-3,0,-3}});
    quad({glm::vec3(-.65f,0,0),{.65f,0,0},{.65f,1.5f,0},{-.65f,1.5f,0}}); // Thin two-sided wall.
    quad({glm::vec3(-1.5f,0,-1.1f),{1.5f,0,-1.1f},{1.5f,1.5f,-1.1f},{-1.5f,1.5f,-1.1f}});
    quad({glm::vec3(-1.5f,0,1),{-1.5f,0,-1.1f},{-1.5f,1.5f,-1.1f},{-1.5f,1.5f,1}});
    return world;
}
void frameParameters(FrameData& frame,const Config& config) {
    const auto& reference=config.reference;
    frame.taa=false;frame.rsm=false;frame.shadows=false;frame.ssao=true;frame.aoRadius=reference.radius;frame.aoBias=reference.bias;frame.aoPower=reference.power;
    frame.aoSlices=config.slices;frame.aoSteps=config.steps;
}
struct Previous {glm::mat4 vp{1};std::vector<float> positions,reference;std::vector<uint8_t> mask;std::map<std::string,std::vector<float>> ao;};
Json motionError(const std::vector<float>& ao,const AoReferenceImage& ref,const std::vector<float>& positions,const FrameData& frame,const Previous& previous,const std::string& mode,uint32_t width,uint32_t height) {
    auto found=previous.ao.find(mode);if(found==previous.ao.end())return nullptr;
    double sum=0;size_t count=0;const auto projection=frame.viewProjection*glm::inverse(frame.view);
    for(size_t p=0;p<ref.matched.size();++p)if(ref.matched[p]){
        const glm::vec3 world(positions[p*4],positions[p*4+1],positions[p*4+2]);auto clip=previous.vp*glm::vec4(world,1);if(clip.w<=0)continue;
        const glm::vec2 uv=glm::vec2(clip)/clip.w*glm::vec2(.5f,-.5f)+.5f;if(glm::any(glm::lessThan(uv,glm::vec2(0)))||glm::any(glm::greaterThanEqual(uv,glm::vec2(1))))continue;
        const size_t q=size_t(uint32_t(uv.y*height))*width+uint32_t(uv.x*width);if(!previous.mask[q])continue;
        const float depth=std::abs((frame.view*glm::vec4(world,1)).z);
        const float tolerance=std::max(.002f,3*depth/(std::abs(projection[0][0])*width));
        const glm::vec3 old(previous.positions[q*4],previous.positions[q*4+1],previous.positions[q*4+2]);if(glm::length(world-old)>tolerance)continue;
        const double delta=(ao[p*4]-ref.visibility[p])-(found->second[q*4]-previous.reference[q]);sum+=delta*delta;++count;
    }
    if(!count)return nullptr;return {{"overlap_pixels",count},{"reprojected_error_delta_rmse",std::sqrt(sum/count)}};
}
Json benchmarkCase(const std::string& name,const Config& config,std::shared_ptr<rhi::GraphicsDevice> device) {
    std::shared_ptr<RenderScene> scene;RenderWorldSnapshot snapshot;
    if(name=="contact")snapshot=contactFixture(config.reference.width,config.reference.height);
    else {scene=makeClassicScene(name);scene->mainCamera()->setAspect(float(config.reference.width)/config.reference.height);SceneSnapshotBuilder builder;snapshot=*builder.capture(scene,8,config.reference.width,config.reference.height,true);}
    if(snapshot.terrain||!snapshot.frame.oceans.empty())throw std::invalid_argument("AO benchmark currently requires static triangle scenes");
    // Match the raster opaque pass; alpha-cutout triangles remain in the BVH.
    snapshot.draws.erase(std::remove_if(snapshot.draws.begin(),snapshot.draws.end(),[](const auto& draw){return draw.parameters.albedoAlpha.a<1&&draw.parameters.factors.w==0;}),snapshot.draws.end());
    frameParameters(snapshot.frame,config);pt::CpuScene rayScene(snapshot);
    SceneAdapter adapter(device);auto prepared=adapter.resolve(snapshot);auto base=prepared.frame;const auto projection=base.viewProjection*glm::inverse(base.view);
    ForwardPbrRenderer renderer(device,rhi::defaultShaderDirectory(),config.reference.width,config.reference.height,PbrPath::Scene);
    const std::vector<std::string> modes={"legacy","gtao-world","gtao-raw","gtao","gtao-hiz"};Json report;report["frames"]=Json::array();report["triangles"]=rayScene.triangles();Previous previous;
    for(uint32_t index=0;index<config.frames;++index){
        auto frame=base;const auto shift=glm::vec3(index*.025f,0,0);frame.cameraPosition=base.cameraPosition+shift;frame.view=base.view*glm::translate(glm::mat4(1),-shift);frame.viewProjection=projection*frame.view;
        frame.aoHorizon=true;frame.aoDenoise=true;renderer.render(frame,prepared.packets,prepared.exposure);
        const auto positions=renderer.readGBuffer(0);std::cout<<"Tracing AO reference "<<name<<" frame "<<index<<" ("<<config.reference.samples<<" spp)\n"<<std::flush;
        const auto reference=traceAoReference(rayScene,frame,positions,config.reference);
        if(!reference.matchedPixels||reference.saturatedRays)throw std::runtime_error("AO reference has no matches or exhausted alpha traversal");
        Json metrics={{"frame",index},{"camera",{frame.cameraPosition.x,frame.cameraPosition.y,frame.cameraPosition.z}},{"covered_pixels",reference.covered},{"matched_pixels",reference.matchedPixels},{"excluded_surface_mismatch",reference.covered-reference.matchedPixels},{"reference_rays",reference.rays},{"reference_cpu_seconds",reference.seconds},{"saturated_rays",reference.saturatedRays}};
        Previous current;current.vp=frame.viewProjection;current.positions=positions;current.reference=reference.visibility;current.mask=reference.matched;
        for(const auto& mode:modes){
            frame.aoHorizon=mode!="legacy";frame.aoDenoise=mode!="legacy"&&mode!="gtao-raw";frame.aoLinearDepth=mode!="legacy"&&mode!="gtao-world";frame.aoDepthHierarchy=mode=="gtao-hiz";renderer.render(frame,prepared.packets,prepared.exposure);auto ao=renderer.readSSAO();
            metrics["modes"][mode]={{"all",error(ao,reference)},{"occluded",error(ao,reference,1)},{"open",error(ao,reference,2)},{"motion",motionError(ao,reference,positions,frame,previous,mode,config.reference.width,config.reference.height)}};
            current.ao[mode]=ao;
            if(index==0&&mode=="gtao-hiz") {
                for(uint32_t mip=0;mip<renderer.aoDepthLevels();++mip) {
                    const auto depth=renderer.readAoDepth(mip);const uint32_t w=std::max(1u,config.reference.width>>mip),h=std::max(1u,config.reference.height>>mip);
                    saveScalar(config.directory/(name+"-depth-mip"+std::to_string(mip)+".pfm"),w,h,depth);
                    if(mip==0||mip==4) {
                        auto preview=depth;for(auto& d:preview)d=d>0?glm::clamp(1.f-std::log1p(d)/std::log1p(frame.farPlane),0.f,1.f):0;
                        save(config.directory/(name+"-depth-mip"+std::to_string(mip)+".png"),w,h,grayscale(preview,false));
                    }
                }
            }
            if(index==0){
                save(config.directory/(name+"-"+mode+".png"),config.reference.width,config.reference.height,grayscale(ao,true));
                saveScalar(config.directory/(name+"-"+mode+".pfm"),config.reference.width,config.reference.height,ao,true);
                std::vector<uint8_t> pixels(reference.visibility.size()*4,0);
                for(size_t p=0;p<reference.visibility.size();++p){if(reference.matched[p]){const float delta=ao[p*4]-reference.visibility[p];pixels[p*4]=uint8_t(glm::clamp(delta*4,0.f,1.f)*255);pixels[p*4+2]=uint8_t(glm::clamp(-delta*4,0.f,1.f)*255);}pixels[p*4+3]=255;}
                save(config.directory/(name+"-"+mode+"-error.png"),config.reference.width,config.reference.height,pixels);
            }
            std::cout<<name<<" frame "<<index<<" "<<mode<<" RMSE "<<metrics["modes"][mode]["all"]["rmse"]<<"\n"<<std::flush;
        }
        if(index==0){
            save(config.directory/(name+"-reference.png"),config.reference.width,config.reference.height,grayscale(reference.visibility,false));
            saveScalar(config.directory/(name+"-reference.pfm"),config.reference.width,config.reference.height,reference.visibility);
            std::vector<float> matched(reference.matched.begin(),reference.matched.end());
            saveScalar(config.directory/(name+"-match-mask.pfm"),config.reference.width,config.reference.height,matched);
            save(config.directory/(name+"-gtao-scene.png"),config.reference.width,config.reference.height,renderer.readOutput());
            std::vector<uint8_t> mask(reference.visibility.size()*4,0);for(size_t p=0;p<reference.matched.size();++p){for(int c=0;c<3;++c)mask[p*4+c]=reference.matched[p]?255:positions[p*4+3]>0?96:0;mask[p*4+3]=255;}
            save(config.directory/(name+"-match-mask.png"),config.reference.width,config.reference.height,mask);
        }
        previous=std::move(current);report["frames"].push_back(metrics);
        // Save completed frames progressively for long-running references.
        std::ofstream progress(config.directory/(name+"-metrics.json"));progress<<report.dump(2)<<'\n';if(!progress)throw std::runtime_error("Cannot save AO benchmark progress");
    }
    renderer.resize(config.timingWidth,config.timingHeight);auto timingFrame=base;
    glm::mat4 timingProjection=projection;timingProjection[0][0]*=(float(config.reference.width)/config.reference.height)/(float(config.timingWidth)/config.timingHeight);
    timingFrame.viewProjection=timingProjection*base.view;timingFrame.viewportWidth=config.timingWidth;timingFrame.viewportHeight=config.timingHeight;
    report["timing"]={{"width",config.timingWidth},{"height",config.timingHeight},{"warmup_submissions_per_pass",config.warmup},{"sample_submissions_per_pass",config.timingSamples},{"encoders_per_submission",config.timingRepeats}};
    for(const auto& mode:modes){
        timingFrame.aoHorizon=mode!="legacy";timingFrame.aoDenoise=mode!="legacy"&&mode!="gtao-raw";timingFrame.aoLinearDepth=mode!="legacy"&&mode!="gtao-world";timingFrame.aoDepthHierarchy=mode=="gtao-hiz";renderer.render(timingFrame,prepared.packets,prepared.exposure);
        const auto measured=renderer.measureAO(config.warmup,config.timingSamples,config.timingRepeats);
        report["timing"][mode]={{"supported",measured.supported},{"fast_reconstruction",timingFrame.aoLinearDepth&&measured.fastReconstruction},{"scope",measured.scope},{"preprocess",statistics(measured.depthMilliseconds)},{"raw",statistics(measured.rawMilliseconds)},{"filter",statistics(measured.filterMilliseconds)}};
    }
    const uint64_t pixels=uint64_t(config.timingWidth)*config.timingHeight;
    uint64_t depthPixels=0;for(uint32_t w=config.timingWidth,h=config.timingHeight;;w=std::max(1u,w/2),h=std::max(1u,h/2)){depthPixels+=uint64_t(w)*h;if(w==1&&h==1)break;}
    report["memory"]={{"position_bytes",pixels*16},{"normal_bytes",pixels*8},{"raw_ao_bytes",pixels*2},{"filtered_ao_bytes",pixels*2},{"allocated_depth_pyramid_bytes",depthPixels*4},{"nominal_point_query_bytes_per_pixel",{{"gtao_raw_max",(5+2*config.slices*config.steps)*4+8+2},{"gtao_hiz_raw_max",(5+4*config.slices*config.steps)*4+8+2},{"filter_enabled_max",390},{"filter_disabled_max",8}}},{"note","nominal point-query bound plus scalar AO stores; hierarchy adds conditional reads, preprocess reads world positions; caches/branches reduce traffic; not measured DRAM bandwidth"}};
    std::ofstream file(config.directory/(name+"-metrics.json"));file<<report.dump(2)<<'\n';if(!file)throw std::runtime_error("Cannot save AO benchmark report");
    if(scene)scene->destroy();return report;
}
}
int runAoBenchmark(int argc,char** argv) {
    const auto config=options(argc,argv);if(rhi::requestedBackend()==rhi::Backend::OpenGL)throw std::invalid_argument("AO benchmark requires native Metal or Vulkan");
#ifdef SCENERENDERER_HAS_VULKAN
    if(rhi::requestedBackend()==rhi::Backend::Vulkan)rhi::configureVulkanWindowing();
#endif
    if(!glfwInit())throw std::runtime_error("AO benchmark GLFW initialization failed");GLFWwindow* window=nullptr;
    glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER,GLFW_FALSE);
#endif
    try{
        if(createWindow(window,config.reference.width,config.reference.height)!=0||gladInit()!=0)throw std::runtime_error("AO benchmark GPU initialization failed");
        std::filesystem::create_directories(config.directory);const auto device=rhi::graphicsDevice();
        Json report={{"host_label",config.label},{"backend",device->backend()==rhi::Backend::Metal?"Metal":"Vulkan"},{"width",config.reference.width},{"height",config.reference.height},{"samples_per_pixel",config.reference.samples},{"seed",config.reference.seed},{"threads",config.reference.threads},{"motion_frames",config.frames},{"camera_step_world_x",.025},{"radius",config.reference.radius},{"bias",config.reference.bias},{"power",config.reference.power},{"slices",config.slices},{"steps_per_side",config.steps},{"taa",false},{"rsm",false},{"definition","cosine-weighted BVH visibility, XYZ distance smoothstep from 0.6R to R, geometric-normal bias, alpha cutoff, exact triangle normals; full geometry includes hidden surfaces"},{"error_colors","red: too little occlusion; blue: too much occlusion; magnitude scaled 4x; only matched pixels scored"},{"motion_definition","nearest-pixel reprojection of error relative to current ray reference; position-mismatch rejection; no AO/scene history; includes reference sampling uncertainty"}};
        for(const auto* key:{"MTL_DEBUG_LAYER","MTL_SHADER_VALIDATION"})if(const auto* value=std::getenv(key))report["validation_environment"][key]=value;
        const auto names=config.scene=="all"?std::vector<std::string>{"contact","cornell","sponza"}:std::vector<std::string>{config.scene};
        for(const auto& name:names){report["cases"][name]=benchmarkCase(name,config,device);std::ofstream file(config.directory/"summary.json");file<<report.dump(2)<<'\n';if(!file)throw std::runtime_error("Cannot save AO benchmark summary");}
        device->waitIdle();rhi::shutdown();glfwDestroyWindow(window);glfwTerminate();return 0;
    }catch(...){try{rhi::shutdown();}catch(...){}if(window)glfwDestroyWindow(window);glfwTerminate();throw;}
}
}
