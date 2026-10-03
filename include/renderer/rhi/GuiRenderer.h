#pragma once
#include "renderer/rhi/Resources.h"
#include <imgui/imgui.h>
#include <map>
namespace render {
class GuiRenderer {
public:
    GuiRenderer(std::shared_ptr<rhi::GraphicsDevice>,const std::string& shaderDirectory);
    ~GuiRenderer();
    void render(ImDrawData*,rhi::TextureHandle output);
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
