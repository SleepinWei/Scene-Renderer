#include "renderer/rhi/GuiRenderer.h"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
namespace render {
GuiRenderer::GuiRenderer(std::shared_ptr<rhi::GraphicsDevice> device,const std::string& root):resources_(device){
    using namespace rhi;auto asset=[&](const char* name){const auto p=root+"/"+name;return ShaderAsset{p+".glsl",p+".metallib",p+".spv",p+".json","main0"};};
    GraphicsPipelineDesc p;p.vertex=asset("gui.vert");p.fragment=asset("gui.frag");p.vertexStride=32;p.attributes={{0,VertexFormat::Float2,0},{1,VertexFormat::Float2,8},{2,VertexFormat::Float4,16}};imageLayout_={1,{{0,BindingType::SampledTexture,ShaderStage::Fragment,"guiTexture",0}}};p.bindings={{0,{{0,BindingType::UniformBuffer,ShaderStage::Vertex,"GuiProjection",64}}},imageLayout_};p.blend=true;pipeline_=resources_.pipeline(p);
    unsigned char* pixels;int w,h;auto& io=ImGui::GetIO();io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);auto texture=resources_.texture({uint32_t(w),uint32_t(h),Format::RGBA8UNorm,TextureUsage::Sampled|TextureUsage::CopyDestination,"ImGui font atlas"});device->writeTexture(texture,pixels,size_t(w)*h*4);fontId_=registerTexture(resources_.view(texture),resources_.sampler({Filter::Linear,AddressMode::ClampToEdge}));io.Fonts->SetTexID(fontId_);io.BackendRendererName="SceneRenderer RHI";io.BackendFlags|=ImGuiBackendFlags_RendererHasVtxOffset;
}
GuiRenderer::~GuiRenderer(){if(ImGui::GetCurrentContext()){auto& io=ImGui::GetIO();io.Fonts->SetTexID(nullptr);io.BackendRendererName=nullptr;io.BackendFlags&=~ImGuiBackendFlags_RendererHasVtxOffset;}}
ImTextureID GuiRenderer::registerTexture(rhi::TextureViewHandle view,rhi::SamplerHandle sampler){const auto id=nextId_++;textures_[id]=resources_.bindings({imageLayout_,{{0,{},0,0,view,sampler}}});return reinterpret_cast<ImTextureID>(id);}
void GuiRenderer::unregisterTexture(ImTextureID id){auto it=textures_.find(reinterpret_cast<uintptr_t>(id));if(it!=textures_.end()){resources_.device->waitIdle();resources_.device->destroyBindingSet(it->second);textures_.erase(it);}}
void GuiRenderer::render(ImDrawData* data,rhi::TextureHandle output){
    if(!data || data->TotalVtxCount==0 || data->DisplaySize.x<=0 || data->DisplaySize.y<=0)return;
    using namespace rhi;Resources frame(resources_.device);auto view=frame.view(output);const uint32_t width=uint32_t(data->DisplaySize.x*data->FramebufferScale.x),height=uint32_t(data->DisplaySize.y*data->FramebufferScale.y);if(!width || !height)return;
    const float left=data->DisplayPos.x,top=data->DisplayPos.y;glm::mat4 projection(1);projection[0][0]=2/data->DisplaySize.x;projection[1][1]=-2/data->DisplaySize.y;projection[3][0]=-1-left*projection[0][0];projection[3][1]=1-top*projection[1][1];auto uniform=frame.buffer({64,BufferUsage::Uniform,"ImGui projection"},&projection);auto camera=frame.bindings({{0,{{0,BindingType::UniformBuffer,ShaderStage::Vertex,"GuiProjection",64}}},{{0,uniform,0,64,{},{}}}});
    auto list=resources_.device->createCommandList();
    for(int n=0;n<data->CmdListsCount;++n){const auto* source=data->CmdLists[n];struct Vertex{glm::vec2 pos,uv;glm::vec4 color;};std::vector<Vertex> vertices;vertices.reserve(source->VtxBuffer.Size);
        for(const auto& v:source->VtxBuffer)vertices.push_back({{v.pos.x,v.pos.y},{v.uv.x,v.uv.y},{float((v.col>>IM_COL32_R_SHIFT)&255)/255,float((v.col>>IM_COL32_G_SHIFT)&255)/255,float((v.col>>IM_COL32_B_SHIFT)&255)/255,float((v.col>>IM_COL32_A_SHIFT)&255)/255}});
        if(vertices.empty() || source->IdxBuffer.empty())continue;auto vb=frame.buffer({vertices.size()*sizeof(Vertex),BufferUsage::Vertex,"ImGui vertices"},vertices.data()),ib=frame.buffer({size_t(source->IdxBuffer.Size)*sizeof(ImDrawIdx),BufferUsage::Index,"ImGui indices"},source->IdxBuffer.Data);
        for(const auto& command:source->CmdBuffer){if(command.UserCallback){if(command.UserCallback!=ImDrawCallback_ResetRenderState)command.UserCallback(source,&command);continue;}if(!command.ElemCount)continue;
            const auto& c=command.ClipRect;const int x=std::max(0,int(std::floor((c.x-left)*data->FramebufferScale.x))),y=std::max(0,int(std::floor((c.y-top)*data->FramebufferScale.y))),right=std::min(int(width),int(std::ceil((c.z-left)*data->FramebufferScale.x))),bottom=std::min(int(height),int(std::ceil((c.w-top)*data->FramebufferScale.y)));if(right<=x || bottom<=y)continue;
            auto texture=textures_.find(reinterpret_cast<uintptr_t>(command.TextureId));if(texture==textures_.end())throw std::invalid_argument("ImGui texture must be registered with the RHI renderer");RenderPassDesc pass;pass.color=view;pass.colorLoad=LoadOp::Load;pass.scissor={uint32_t(x),uint32_t(y),uint32_t(right-x),uint32_t(bottom-y)};list.beginRenderPass(pass);list.bindPipeline(pipeline_);list.bindBindingSet(camera);list.bindBindingSet(texture->second);list.bindVertexBuffer(vb);list.bindIndexBuffer(ib,sizeof(ImDrawIdx)==2?IndexType::UInt16:IndexType::UInt32);list.drawIndexed(command.ElemCount,command.IdxOffset,int32_t(command.VtxOffset));list.endRenderPass();
        }
    }resources_.device->submit(list);
}
}
