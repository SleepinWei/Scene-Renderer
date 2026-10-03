#pragma once
#include "renderer/rhi/Resources.h"
#include <imgui/imgui.h>
#include <map>
namespace render {
struct GuiDrawList {std::vector<ImDrawVert> vertices;std::vector<ImDrawIdx> indices;std::vector<ImDrawCmd> commands;};
struct GuiFrame {
    ImVec2 displayPos{},displaySize{},framebufferScale{1,1};
    std::vector<GuiDrawList> lists;
    static GuiFrame capture(const ImDrawData*); // Deep-copy on the UI thread.
};
class GuiRenderer {
public:
    GuiRenderer(std::shared_ptr<rhi::GraphicsDevice>,const std::string& shaderDirectory);
    ~GuiRenderer();
    void render(ImDrawData*,rhi::TextureHandle output);
    void render(const GuiFrame&,rhi::TextureHandle output);
    ImTextureID registerTexture(rhi::TextureViewHandle,rhi::SamplerHandle);
    void unregisterTexture(ImTextureID);
private:
    Resources resources_;
    rhi::PipelineHandle pipeline_;
    rhi::BindingLayout imageLayout_;
    std::map<uintptr_t,rhi::BindingSetHandle> textures_;
    uintptr_t nextId_=1;ImTextureID fontId_{};
};
}
