#pragma once
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <atomic>
#include <memory>
#include <string>
namespace engine {
class AssetLoadError:public std::runtime_error {
public:
    AssetLoadError(std::string phase,std::string path,std::string detail)
        :std::runtime_error(phase+" ["+path+"]: "+detail),phase(std::move(phase)),path(std::move(path)){}
    const std::string phase,path;
};
class CancellationScope {
public:
    explicit CancellationScope(std::shared_ptr<std::atomic<bool>> flag):previous_(current_){current_=std::move(flag);}
    ~CancellationScope(){current_=std::move(previous_);}
    static bool cancelled(){return current_ && current_->load();}
    static void check(){if(cancelled())throw std::runtime_error("Scene load cancelled");}
private:
    std::shared_ptr<std::atomic<bool>> previous_;
    inline static thread_local std::shared_ptr<std::atomic<bool>> current_;
};
class AssetPath {
public:
    static void setRoot(const std::filesystem::path& root) {
        if(!std::filesystem::is_directory(root))throw std::invalid_argument("Asset root is not a directory: "+root.string());
        std::lock_guard<std::mutex> guard(mutex_);root_=std::filesystem::weakly_canonical(root);
    }
    static std::filesystem::path root(){std::lock_guard<std::mutex> guard(mutex_);return root_;}
    static std::string resolve(const std::string& value,const std::filesystem::path& document={}) {
        std::filesystem::path path(value);
        if(path.empty())throw std::invalid_argument("Empty asset path");
        if(!path.is_absolute()) {
            auto relative=document.parent_path()/path;
            path=!document.empty() && std::filesystem::exists(relative)?relative:root()/path;
        }
        return std::filesystem::weakly_canonical(path).generic_string();
    }
private:
    inline static std::mutex mutex_;
    inline static std::filesystem::path root_=std::filesystem::current_path();
};
}
