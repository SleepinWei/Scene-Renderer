#include "renderer/rhi/ForwardPbrRenderer.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace render {
namespace {
void check(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
glm::vec3 reference(glm::vec3 position, glm::vec3 base, float metallic, float roughness, float radiance, float ambient, glm::vec3 N = {0,0,1}) {
    const glm::vec3 L(0,0,1), V = glm::normalize(glm::vec3(0,0,3)-position), H = glm::normalize(V+L);
    const float dotL = glm::dot(N,L), dotV = glm::dot(N,V), dotH = glm::dot(N,H), a = roughness*roughness, a2 = a*a;
    const float denominator = dotH*dotH*(a2-1)+1, distribution = a2/(3.14159265359f*denominator*denominator);
    const float k = (roughness+1)*(roughness+1)/8, G = dotV/(dotV*(1-k)+k)*dotL/(dotL*(1-k)+k);
    const glm::vec3 albedo = glm::pow(base,glm::vec3(2.2f)), F0 = glm::mix(glm::vec3(.04f),albedo,metallic);
    const glm::vec3 F = F0+(1.f-F0)*std::pow(1-glm::dot(H,V),5.f);
    return ambient*albedo+((1.f-F)*(1-metallic)*albedo/3.14159265359f + distribution*G*F/(4*dotV*dotL+.0001f))*radiance*dotL;
}
}
void validateForwardRendering(std::shared_ptr<rhi::GraphicsDevice> device, const std::string& directory) {
    const std::vector<MeshVertex> vertices{{{-.8f,-.8f,.4f},{0,0,1},{.1f,.9f}},{{.8f,-.8f,.4f},{0,0,1},{.9f,.9f}},
        {{.8f,.8f,.4f},{0,0,1},{.9f,.1f}},{{-.8f,.8f,.4f},{0,0,1},{.1f,.1f}}};
    auto mesh = std::make_shared<GpuMesh>(device,vertices,std::vector<uint32_t>{0,1,2,0,2,3});
    try { GpuMesh invalid(device,vertices,{0,1,99});throw std::runtime_error("Renderer accepted invalid indices"); } catch (const std::invalid_argument&) {}
    MaterialDesc desc;desc.parameters.factors = {0,.5f,1,.5f};desc.parameters.emissiveNormal.w = 0;
    desc.images[0] = {2,2,{255,0,0,255, 0,255,0,0, 0,0,255,255, 255,255,255,255}};desc.filter = rhi::Filter::Nearest;
    const std::string checkerPath = directory+"/../forward-checker.ppm";
    {
        std::ofstream source(checkerPath,std::ios::binary);source << "P6\n2 2\n255\n";
        for (size_t i = 0; i < desc.images[0].pixels.size(); i += 4) source.write(reinterpret_cast<const char*>(desc.images[0].pixels.data()+i),3);
        check(bool(source),"Cannot save decoder test input");
    }
    auto decoded = ImageRGBA8::load(checkerPath);check(decoded.width == 2 && decoded.height == 2 && decoded.pixels[0] == 255 && decoded.pixels[10] == 255,"Image decode channels/orientation failed");
    auto material = std::make_shared<GpuMaterial>(device,desc);
    ForwardPbrRenderer renderer(device,directory,64,64);
    FrameData frame;frame.lights.push_back({{0,0,0,0},{8,8,8,0},{0,0,-1,0}});
    std::vector<DrawPacket> packets{{mesh,material,glm::mat4(1)}};
    // A second, farther object uses its own parameter block, not the last
    // object uniform written by the CPU while building a command list.
    auto rearDesc = desc;rearDesc.parameters.emissiveNormal = {0,4,0,0};rearDesc.parameters.factors.w = 0;
    rearDesc.images[0] = {1,1,{0,0,0,255}};auto rear = std::make_shared<GpuMaterial>(device,rearDesc);
    glm::mat4 farther(1);farther[3].z = .2f;packets.push_back({mesh,rear,farther});
    renderer.render(frame,packets);
    const auto hdr = renderer.readHDR();const auto output = renderer.readOutput();
    check(hdr.size() == 64*64*4 && output.size() == 64*64*4,"Forward readback size mismatch");
    for (auto v : hdr) check(std::isfinite(v),"Forward HDR contains NaN/Inf");
    check(*std::max_element(hdr.begin(),hdr.end()) > 1.5f,"Forward HDR was clamped before tone mapping");
    // Check three opaque texture quadrants against the retained BRDF and tone
    // curve; checking world coordinates also detects matrix layout mistakes.
    for (auto sample : {std::array<int,2>{16,16},{16,47},{47,47}}) {
        const int x = sample[0], y = sample[1];const size_t at = size_t(y*64+x)*4;
        glm::vec3 base = x < 32 ? (y < 32 ? glm::vec3(1,0,0) : glm::vec3(0,0,1)) : glm::vec3(1);
        const glm::vec3 expected = reference({(x+.5f)/32-1,1-(y+.5f)/32,.4f},base,0,.5f,8,.03f);
        for (int c = 0; c < 3; ++c) {
            check(std::abs(hdr[at+c]-expected[c]) < .012f,"Forward PBR/depth/object block differs from CPU reference");
            const int mapped = int(std::lround(std::pow(1-std::exp(-hdr[at+c]),1/2.2f)*255));
            check(std::abs(int(output[at+c])-mapped) <= 2,"Forward tone map differs from CPU reference");
        }
    }
    // The alpha hole reveals the farther object's green emissive surface.
    const size_t hole = (16*64+47)*4;
    const auto rearExpected = reference({47.5f/32-1,1-16.5f/32,.6f},glm::vec3(0),0,.5f,8,.03f)+glm::vec3(0,4,0);
    for (int c = 0; c < 3; ++c) check(std::abs(hdr[hole+c]-rearExpected[c]) < .02f,"Alpha cutoff or per-object depth failed");
    check(output[0] == 0 && output[1] == 0 && output[2] == 0,"Forward background was corrupted");
    ForwardPbrRenderer deferred(device,directory,64,64,PbrPath::Deferred);
    auto compareDeferred = [&](const std::vector<DrawPacket>& draws, float exposure = 1.f) {
        renderer.render(frame,draws,exposure);deferred.render(frame,draws,exposure);
        const auto a = renderer.readHDR(), b = deferred.readHDR();const auto mappedA = renderer.readOutput(), mappedB = deferred.readOutput();
        check(a.size() == b.size(),"Deferred HDR size mismatch");
        for (size_t i=0;i<a.size();++i) {
            check(std::isfinite(b[i]) && std::abs(a[i]-b[i]) <= .025f + .005f*std::abs(a[i]),"Deferred HDR differs from forward PBR");
            check(std::abs(int(mappedA[i])-int(mappedB[i])) <= 2,"Deferred tone mapped output differs from forward");
        }
    };
    compareDeferred(packets);
    const auto position = deferred.readGBuffer(0), normal = deferred.readGBuffer(1), albedo = deferred.readGBuffer(2), emission = deferred.readGBuffer(3);
    const size_t surface = (16*64+16)*4;
    check(position[3] == 0 && position[surface+3] == 1,"G-buffer background/validity mask failed");
    check(std::abs(position[surface] - (16.5f/32-1)) < .001f && std::abs(position[surface+2]-.4f) < .001f && std::abs(position[hole+2]-.6f) < .001f,"G-buffer position/depth/alpha cutoff failed");
    check(normal[surface+2] == 1 && normal[surface+3] == .5f && albedo[surface] == 1 && albedo[surface+3] == 0,"G-buffer normal/roughness/albedo/metallic packing failed");
    check(emission[hole+1] == 4 && emission[surface+3] == 1,"G-buffer emissive/AO packing failed");
    try { deferred.readGBuffer(4);throw std::runtime_error("invalid G-buffer attachment accepted"); } catch (const std::invalid_argument&) {}
    compareDeferred(packets,.25f);
    renderer.render(frame,packets,.25f);auto lowExposure = renderer.readOutput();
    check(lowExposure[(16*64+16)*4] < output[(16*64+16)*4],"Exposure update was ignored");
    desc.parameters.albedoAlpha = {.5f,.5f,.5f,1};material->update(desc.parameters);renderer.render(frame,packets);
    check(renderer.readHDR()[(16*64+16)*4] < hdr[(16*64+16)*4],"Material parameter update was ignored");
    auto mappedDesc = desc;mappedDesc.parameters.albedoAlpha = {1,1,1,1};mappedDesc.parameters.emissiveNormal.w = 1;
    mappedDesc.images[1] = {1,1,{204,128,230,255}};auto mapped = std::make_shared<GpuMaterial>(device,mappedDesc);
    renderer.render(frame,{{mesh,mapped,glm::mat4(1)}});const auto mappedHDR = renderer.readHDR();
    const auto mappedNormal = glm::normalize(glm::vec3(204.f/255*2-1,-(128.f/255*2-1),230.f/255*2-1));
    const auto mappedExpected = reference({16.5f/32-1,1-16.5f/32,.4f},{1,0,0},0,.5f,8,.03f,mappedNormal);
    for (int c = 0; c < 3; ++c) check(std::abs(mappedHDR[(16*64+16)*4+c]-mappedExpected[c]) < .015f,"Normal map tangent orientation differs from CPU reference");
    compareDeferred({{mesh,mapped,glm::mat4(1)}});
    renderer.resize(32,48);deferred.resize(32,48);compareDeferred(packets);renderer.render(frame,packets);check(renderer.readOutput().size() == 32*48*4,"Forward resize failed");
    try { renderer.resize(0,48);throw std::runtime_error("Renderer accepted invalid resize"); } catch (const std::invalid_argument&) {}
    renderer.render(frame,packets);check(renderer.readOutput().size() == 32*48*4,"Failed resize lost prior attachments");
    try { deferred.resize(0,48);throw std::runtime_error("Deferred accepted invalid resize"); } catch (const std::invalid_argument&) {}
    compareDeferred(packets);
    deferred.resize(64,64);deferred.render(frame,packets);const auto deferredOutput = deferred.readOutput();
    const char* backend = device->backend() == rhi::Backend::Metal ? "metal" : device->backend() == rhi::Backend::OpenGL ? "opengl" : "vulkan";
    std::ofstream image(directory+"/../"+backend+"-forward.ppm",std::ios::binary);image << "P6\n64 64\n255\n";
    for (size_t i = 0; i < output.size(); i += 4) image.write(reinterpret_cast<const char*>(output.data()+i),3);
    check(bool(image),"Cannot save forward validation image");
    std::ofstream deferredImage(directory+"/../"+backend+"-deferred.ppm",std::ios::binary);deferredImage << "P6\n64 64\n255\n";
    for (size_t i=0;i<deferredOutput.size();i+=4) deferredImage.write(reinterpret_cast<const char*>(deferredOutput.data()+i),3);
    check(bool(deferredImage),"Cannot save deferred validation image");
    std::cout << "RHI " << backend << " MRT/deferred G-buffer, lighting, normal mapping, forward comparison and resize passed\n";
    std::cout << "RHI " << backend << " forward PBR/HDR, material/object blocks, alpha cutoff, depth, exposure and resize passed\n";
}
}
