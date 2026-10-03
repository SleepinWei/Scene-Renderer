#pragma once
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
namespace engine {
// Ordered resource graph. Uses are checked per mip/layer; compilation exposes
// hazards and transient lifetimes before any GPU recording callback executes.
class RenderGraph {
public:
    using Resource=std::string;
    enum class Access {Read,Write,ReadWrite};
    struct Range {uint32_t firstMip=0,mipCount=0,firstLayer=0,layerCount=0;}; // 0 count: remaining range.
    struct Description {uint32_t mips=1,layers=1;bool transient=false;std::string aliasClass;};
    struct Use {Resource resource;Access access;Range range{};};
    struct Lifetime {Resource resource;size_t first=0,last=0,aliasSlot=SIZE_MAX;};
    struct Plan {std::vector<std::set<size_t>> dependencies;std::vector<Lifetime> lifetimes;};
    void describe(Resource resource,Description desc) {
        if(resource.empty() || !desc.mips || !desc.layers || desc.mips>32 || desc.layers>2048)
            throw std::invalid_argument("Invalid graph resource description");
        if(!descriptions_.emplace(std::move(resource),std::move(desc)).second)
            throw std::logic_error("Duplicate graph resource description");
    }
    void import(Resource resource) {imports_.push_back({std::move(resource),Access::Write,{}});}
    void import(Resource resource,Range range) {imports_.push_back({std::move(resource),Access::Write,range});}
    void add(std::string name,std::vector<Use> uses,std::function<void()> record) {
        if(name.empty() || !record)throw std::invalid_argument("Graph pass requires a name and recorder");
        passes_.push_back({std::move(name),std::move(uses),std::move(record)});
    }
    Plan compile() const {
        using Cell=std::tuple<Resource,uint32_t,uint32_t>;
        std::set<Cell> initialized;
        std::map<Cell,size_t> writers;
        std::map<Cell,std::set<size_t>> readers;
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
        Plan plan;plan.dependencies.resize(passes_.size());
        for(size_t i=0;i<passes_.size();++i) {
            const auto& pass=passes_[i];std::set<Cell> unique;
            for(const auto& use:pass.uses) {
                auto found=lifetimes.find(use.resource);
                if(found==lifetimes.end())lifetimes.emplace(use.resource,Lifetime{use.resource,i,i,SIZE_MAX});
                else found->second.last=i;
                for(const auto& cell:cells(use)) {
                    if(!unique.insert(cell).second)throw std::logic_error("Render graph overlapping feedback: "+pass.name+" / "+use.resource);
                    if(use.access!=Access::Write && !initialized.count(cell))
                        throw std::logic_error("Render graph uninitialized read: "+pass.name+" / "+use.resource);
                    auto writer=writers.find(cell);
                    if(writer!=writers.end())plan.dependencies[i].insert(writer->second);
                    if(use.access==Access::Read)readers[cell].insert(i);
                    else {
                        plan.dependencies[i].insert(readers[cell].begin(),readers[cell].end());readers[cell].clear();
                        writers[cell]=i;initialized.insert(cell);
                    }
                }
            }
        }
        for(const auto& item:lifetimes)plan.lifetimes.push_back(item.second);
        std::sort(plan.lifetimes.begin(),plan.lifetimes.end(),[](const auto& a,const auto& b){return a.first<b.first;});
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
    void execute(const std::function<void(const std::string&)>& before={}) {validate();for(const auto& pass:passes_){if(before)before(pass.name);pass.record();}}
    std::vector<std::string> names() const {std::vector<std::string> result;for(const auto& pass:passes_)result.push_back(pass.name);return result;}
private:
    Description description(const Resource& name) const {auto found=descriptions_.find(name);return found==descriptions_.end()?Description{}:found->second;}
    struct Pass {std::string name;std::vector<Use> uses;std::function<void()> record;};
    std::vector<Use> imports_;
    std::map<Resource,Description> descriptions_;
    std::vector<Pass> passes_;
};
}
