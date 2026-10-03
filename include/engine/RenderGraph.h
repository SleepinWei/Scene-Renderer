#pragma once
#include <functional>
#include <set>
#include <string>
#include <vector>
#include <stdexcept>
namespace engine {
// An ordered render graph over the existing explicit CommandList. It validates
// initialization and feedback before recording. Backend barriers remain in RHI;
// transient aliasing and parallel command recording are separate future work.
class RenderGraph {
  public:
    using Resource = std::string;
    enum class Access { Read, Write, ReadWrite };
    struct Use {
        Resource resource;
        Access access;
    };
    void import(Resource resource) { imports_.insert(std::move(resource)); }
    void add(std::string name, std::vector<Use> uses, std::function<void()> record) {
        passes_.push_back({std::move(name), std::move(uses), std::move(record)});
    }
    void validate() const {
        auto initialized = imports_;
        for (const auto &pass : passes_) {
            std::set<Resource> unique;
            for (const auto &use : pass.uses) {
                if (!unique.insert(use.resource).second)
                    throw std::logic_error("Render graph ambiguous feedback: " + pass.name + " / " +
                                           use.resource);
                if (use.access != Access::Write && !initialized.count(use.resource))
                    throw std::logic_error("Render graph uninitialized read: " + pass.name + " / " +
                                           use.resource);
            }
            for (const auto &use : pass.uses)
                if (use.access != Access::Read)
                    initialized.insert(use.resource);
        }
    }
    void execute() {
        validate();
        for (const auto &pass : passes_)
            pass.record();
    }
    std::vector<std::string> names() const {
        std::vector<std::string> result;
        for (const auto &pass : passes_)
            result.push_back(pass.name);
        return result;
    }

  private:
    struct Pass {
        std::string name;
        std::vector<Use> uses;
        std::function<void()> record;
    };
    std::set<Resource> imports_;
    std::vector<Pass> passes_;
};
} // namespace engine
