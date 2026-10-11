#pragma once
#include "engine/RuntimeTrace.h"
#include <functional>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <stdexcept>
#include <tuple>
#include <limits>
#include <utility>
namespace engine {
// Ordered resource graph. Uses are checked per mip/layer; compilation exposes
// hazards and transient lifetimes before any GPU recording callback executes.
class RenderGraph {
public:
    RenderGraph()=default;
    // Compiled plans are tied to a stable graph identity and declaration revision.
    RenderGraph(const RenderGraph&)=delete;
    RenderGraph& operator=(const RenderGraph&)=delete;
    RenderGraph(RenderGraph&&)=delete;
    RenderGraph& operator=(RenderGraph&&)=delete;
    using Resource=std::string;
    enum class Access {Read,Write,ReadWrite};
    struct Range {uint32_t firstMip=0,mipCount=0,firstLayer=0,layerCount=0;}; // 0 count: remaining range.
    struct Description {uint32_t mips=1,layers=1;bool transient=false;std::string aliasClass;};
    struct Use {Resource resource;Access access;Range range{};};
    struct Lifetime {Resource resource;size_t first=0,last=0,aliasSlot=SIZE_MAX;};
    enum class PassFlags {None, NeverCull};
    struct Plan {
        // Indices refer to the original declaration order, including culled passes.
        std::vector<std::set<size_t>> dependencies, producers;
        std::vector<Lifetime> lifetimes;
        std::vector<size_t> activePasses, culledPasses;
    private:
        friend class RenderGraph;
        const RenderGraph* owner=nullptr;
        size_t revision=0;
    };
    // Opt in only after declaring externally consumed outputs / history and side effects.
    void enableCulling(bool enabled=true) {culling_=enabled;++revision_;}
    void exportResource(Resource resource) {exportResource(std::move(resource),{});}
    void exportResource(Resource resource,Range range) {
        exports_.push_back({std::move(resource),Access::Read,range});++revision_;
    }
    void describe(Resource resource,Description desc) {
        if(resource.empty() || !desc.mips || !desc.layers || desc.mips>32 || desc.layers>2048)
            throw std::invalid_argument("Invalid graph resource description");
        if(!descriptions_.emplace(std::move(resource),std::move(desc)).second)
            throw std::logic_error("Duplicate graph resource description");
        ++revision_;
    }
    void import(Resource resource) {import(std::move(resource),{});}
    void import(Resource resource,Range range) {imports_.push_back({std::move(resource),Access::Write,range});++revision_;}
    void add(std::string name,std::vector<Use> uses,std::function<void()> record,PassFlags flags=PassFlags::None) {
        if(name.empty() || !record || (flags!=PassFlags::None && flags!=PassFlags::NeverCull))
            throw std::invalid_argument("Graph pass requires a name, recorder and valid flags");
        passes_.push_back({std::move(name),std::move(uses),std::move(record),flags});++revision_;
    }
    Plan compile() const {
        using Cell=std::tuple<Resource,uint32_t,uint32_t>;
        std::set<Cell> initialized;
        std::map<Cell,size_t> writers;
        std::map<Resource,Lifetime> lifetimes;
        auto cells=[&](const Use& use) {
            if(use.resource.empty() || (use.access!=Access::Read && use.access!=Access::Write && use.access!=Access::ReadWrite))
                throw std::invalid_argument("Invalid graph resource use");
            auto desc=description(use.resource);auto range=use.range;
            if(range.firstMip>=desc.mips || range.firstLayer>=desc.layers)
                throw std::out_of_range("Graph subresource start outside resource");
            if(!range.mipCount)range.mipCount=desc.mips-range.firstMip;
            if(!range.layerCount)range.layerCount=desc.layers-range.firstLayer;
            if(range.mipCount>desc.mips-range.firstMip || range.layerCount>desc.layers-range.firstLayer)
                throw std::out_of_range("Graph subresource range outside resource");
            std::vector<Cell> result;
            for(uint32_t mip=range.firstMip;mip<range.firstMip+range.mipCount;++mip)
                for(uint32_t layer=range.firstLayer;layer<range.firstLayer+range.layerCount;++layer)
                    result.emplace_back(use.resource,mip,layer);
            return result;
        };
        for(const auto& use:imports_) {
            if(description(use.resource).transient)throw std::logic_error("Transient graph resource cannot be imported");
            for(const auto& cell:cells(use))initialized.insert(cell);
        }
        Plan plan;plan.owner=this;plan.revision=revision_;
        plan.dependencies.resize(passes_.size());plan.producers.resize(passes_.size());
        // Build versioned data flow and validate the entire graph, even dead branches.
        // WAR / WAW order constraints must never make an overwritten version live.
        for(size_t i=0;i<passes_.size();++i) {
            const auto& pass=passes_[i];std::set<Cell> unique;
            for(const auto& use:pass.uses) {
                for(const auto& cell:cells(use)) {
                    if(!unique.insert(cell).second)throw std::logic_error("Render graph overlapping feedback: "+pass.name+" / "+use.resource);
                    if(use.access!=Access::Write && !initialized.count(cell))
                        throw std::logic_error("Render graph uninitialized read: "+pass.name+" / "+use.resource);
                    auto writer=writers.find(cell);
                    if(use.access!=Access::Write && writer!=writers.end())plan.producers[i].insert(writer->second);
                    if(use.access!=Access::Read) {
                        writers[cell]=i;initialized.insert(cell);
                    }
                }
            }
        }
        std::vector<bool> live(passes_.size(),!culling_);
        std::vector<size_t> pending;
        for(size_t i=0;i<passes_.size();++i)if(passes_[i].flags==PassFlags::NeverCull)pending.push_back(i);
        for(const auto& use:exports_)for(const auto& cell:cells(use)) {
            if(!initialized.count(cell))throw std::logic_error("Render graph uninitialized export: "+use.resource);
            auto writer=writers.find(cell);if(writer!=writers.end())pending.push_back(writer->second);
        }
        while(!pending.empty()) {
            const auto i=pending.back();pending.pop_back();
            if(live[i])continue;
            live[i]=true;
            pending.insert(pending.end(),plan.producers[i].begin(),plan.producers[i].end());
        }
        // Rebuild hazards against retained writers/readers; simply filtering the
        // old edges would miss WAW / WAR across a removed intermediate writer.
        writers.clear();std::map<Cell,std::set<size_t>> readers;
        for(size_t i=0;i<passes_.size();++i) {
            if(!live[i]){plan.culledPasses.push_back(i);continue;}
            plan.activePasses.push_back(i);
            for(const auto& use:passes_[i].uses) {
                auto found=lifetimes.find(use.resource);
                if(found==lifetimes.end())lifetimes.emplace(use.resource,Lifetime{use.resource,i,i,SIZE_MAX});
                else found->second.last=i;
                for(const auto& cell:cells(use)) {
                    auto writer=writers.find(cell);
                    if(writer!=writers.end())plan.dependencies[i].insert(writer->second);
                    if(use.access==Access::Read)readers[cell].insert(i);
                    else {
                        plan.dependencies[i].insert(readers[cell].begin(),readers[cell].end());readers[cell].clear();
                        writers[cell]=i;
                    }
                }
            }
        }
        // External consumers may use an exported texture after the last pass.
        // Its storage cannot be reused by a later, otherwise unrelated pass.
        for(const auto& use:exports_) {
            auto found=lifetimes.find(use.resource);
            if(found!=lifetimes.end())found->second.last=passes_.size();
        }
        for(const auto& item:lifetimes)plan.lifetimes.push_back(item.second);
        std::sort(plan.lifetimes.begin(),plan.lifetimes.end(),[](const auto& a,const auto& b){return std::tie(a.first,a.resource)<std::tie(b.first,b.resource);});
        struct Slot {Description desc;size_t last;};std::vector<Slot> slots;
        for(auto& lifetime:plan.lifetimes) {
            auto desc=description(lifetime.resource);
            if(!desc.transient || desc.aliasClass.empty())continue;
            size_t slot=0;
            while(slot<slots.size() && !(slots[slot].last<lifetime.first && slots[slot].desc.mips==desc.mips &&
                  slots[slot].desc.layers==desc.layers && slots[slot].desc.aliasClass==desc.aliasClass))++slot;
            if(slot==slots.size())slots.push_back({desc,lifetime.last});else slots[slot].last=lifetime.last;
            lifetime.aliasSlot=slot;
        }
        return plan;
    }
    void validate() const {compile();}
    Plan execute(const std::function<void(const std::string&)>& before={}) {
        Plan plan;
        {TraceScope trace("graph/compile","render");plan=compile();}
        execute(plan,before);return plan;
    }
    void execute(const Plan& plan,const std::function<void(const std::string&)>& before={}) {
        if(plan.owner!=this || plan.revision!=revision_)throw std::logic_error("Stale or foreign render graph plan");
        for(auto i:plan.activePasses){const auto& pass=passes_.at(i);if(before)before(pass.name);TraceScope trace(pass.name.c_str(),"graph-record");pass.record();}
    }
    std::vector<std::string> names() const {std::vector<std::string> result;for(const auto& pass:passes_)result.push_back(pass.name);return result;}
private:
    Description description(const Resource& name) const {auto found=descriptions_.find(name);return found==descriptions_.end()?Description{}:found->second;}
    struct Pass {std::string name;std::vector<Use> uses;std::function<void()> record;PassFlags flags;};
    bool culling_=false;
    size_t revision_=0;
    std::vector<Use> imports_,exports_;
    std::map<Resource,Description> descriptions_;
    std::vector<Pass> passes_;
};
}
