#pragma once
#include "engine/RenderGraph.h"
#include "renderer/rhi/Resources.h"
#include <map>
#include <string>
namespace render {
// Physical reuse of compatible textures with disjoint graph lifetimes.
// This reuses texture objects; it is not native heap aliasing or cross-queue reuse.
class GraphTextures {
public:
    GraphTextures(std::shared_ptr<rhi::GraphicsDevice> device,const engine::RenderGraph::Plan& plan,
                  const std::map<std::string,rhi::TextureDesc>& descriptors):resources_(std::move(device)) {
        std::map<size_t,rhi::TextureDesc> physical;
        std::map<std::string,size_t> slots;
        size_t next=0;for(const auto& life:plan.lifetimes)if(life.aliasSlot!=SIZE_MAX)next=std::max(next,life.aliasSlot+1);
        for(const auto& life:plan.lifetimes) {
            auto found=descriptors.find(life.resource);if(found==descriptors.end())continue;
            const size_t slot=life.aliasSlot==SIZE_MAX?next++:life.aliasSlot;
            auto previous=physical.find(slot);
            if(previous==physical.end())physical.emplace(slot,found->second);
            else {
                auto& a=previous->second;const auto& b=found->second;
                if(a.width!=b.width || a.height!=b.height || a.format!=b.format || a.mipLevels!=b.mipLevels || a.arrayLayers!=b.arrayLayers)
                    throw std::invalid_argument("Render graph aliases incompatible texture storage");
                a.usage=a.usage|b.usage;
            }
            slots.emplace(life.resource,slot);
        }
        std::map<size_t,std::pair<rhi::TextureHandle,rhi::TextureViewHandle>> handles;
        for(const auto& allocation:physical) {
            auto texture=resources_.texture(allocation.second);handles.emplace(allocation.first,std::make_pair(texture,resources_.view(texture)));
        }
        for(const auto& binding:slots)bindings_.emplace(binding.first,handles.at(binding.second));
    }
    rhi::TextureHandle texture(const std::string& name) const{return bindings_.at(name).first;}
    rhi::TextureViewHandle view(const std::string& name) const{return bindings_.at(name).second;}
    size_t logicalTextures() const {return bindings_.size();}
private:
    Resources resources_;
    std::map<std::string,std::pair<rhi::TextureHandle,rhi::TextureViewHandle>> bindings_;
};
}
