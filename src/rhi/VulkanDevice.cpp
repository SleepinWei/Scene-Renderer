#include "rhi/GraphicsDevice.h"
#include "rhi/ShaderAssets.h"
#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <deque>
#include <stdexcept>

namespace rhi {
namespace {
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string("RHI Vulkan: ") + operation + " failed (" + std::to_string(result) + ")");
}
VkFormat format(Format value) {
    switch (value) {
    case Format::RGBA8UNorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::RGBA32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::Depth32Float: return VK_FORMAT_D32_SFLOAT;
    }throw std::invalid_argument("RHI Vulkan: invalid format");
}
VkAttachmentLoadOp loadOp(LoadOp value) {
    return value == LoadOp::Clear ? VK_ATTACHMENT_LOAD_OP_CLEAR : value == LoadOp::Load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}
VkAttachmentStoreOp storeOp(StoreOp value) { return value == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE; }
struct Context {
    bool wireframe=false;
    GLFWwindow* window = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkCommandPool commands = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory{};
    ~Context() {
        if (device) { vkDeviceWaitIdle(device);if (commands) vkDestroyCommandPool(device, commands, nullptr);vkDestroyDevice(device, nullptr); }
        if (surface) vkDestroySurfaceKHR(instance,surface,nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
    }
    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags flags) const {
        for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & flags) == flags) return i;
        throw std::runtime_error("RHI Vulkan: no compatible memory type");
    }
};
std::unique_ptr<Context> makeContext(GLFWwindow* window) {
    auto c = std::make_unique<Context>();c->window=window;
    uint32_t count = 0;check(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr), "enumerate instance extensions");
    std::vector<VkExtensionProperties> extensions(count);check(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()), "enumerate instance extensions");
    const bool portability = std::any_of(extensions.begin(), extensions.end(), [](const auto& e) { return std::strcmp(e.extensionName, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0; });
    std::vector<const char*> enabled;
    if (portability) enabled.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    if(window) {
        uint32_t count=0;const auto required=glfwGetRequiredInstanceExtensions(&count);
        if(!required || !count)throw std::runtime_error("RHI Vulkan: GLFW surface extensions unavailable");
        for(uint32_t i=0;i<count;++i)enabled.push_back(required[i]);
    }
    check(vkEnumerateInstanceLayerProperties(&count, nullptr), "enumerate layers");std::vector<VkLayerProperties> layers(count);
    check(vkEnumerateInstanceLayerProperties(&count, layers.data()), "enumerate layers");
    const bool validation = std::any_of(layers.begin(), layers.end(), [](const auto& l) { return std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0; });
    const char* validationName = "VK_LAYER_KHRONOS_validation";
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName = "SceneRenderer RHI";app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};info.pApplicationInfo = &app;
    info.enabledExtensionCount = uint32_t(enabled.size());info.ppEnabledExtensionNames = enabled.data();
    if (portability) info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    if (validation) { info.enabledLayerCount = 1;info.ppEnabledLayerNames = &validationName; }
    check(vkCreateInstance(&info, nullptr, &c->instance), "create instance");
    if(window)check(glfwCreateWindowSurface(c->instance,window,nullptr,&c->surface),"create window surface");
    check(vkEnumeratePhysicalDevices(c->instance, &count, nullptr), "enumerate devices");
    std::vector<VkPhysicalDevice> physical(count);check(vkEnumeratePhysicalDevices(c->instance, &count, physical.data()), "enumerate devices");
    uint32_t family = 0;
    for (auto candidate : physical) {
        VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(candidate, &props);
        if (props.apiVersion < VK_API_VERSION_1_1) continue;
        uint32_t families = 0;vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, nullptr);
        std::vector<VkQueueFamilyProperties> queues(families);vkGetPhysicalDeviceQueueFamilyProperties(candidate, &families, queues.data());
        for (uint32_t i = 0; i < families; ++i) if (queues[i].queueCount && ((queues[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))) { VkBool32 supported=VK_TRUE;if(c->surface)check(vkGetPhysicalDeviceSurfaceSupportKHR(candidate,i,c->surface,&supported),"query presentation queue");if(supported){c->physical = candidate;family = i;break;} }
        if (c->physical) break;
    }
    if (!c->physical) throw std::runtime_error("RHI Vulkan: no Vulkan 1.1 graphics device");
    vkGetPhysicalDeviceProperties(c->physical, &c->properties);vkGetPhysicalDeviceMemoryProperties(c->physical, &c->memory);
    check(vkEnumerateDeviceExtensionProperties(c->physical, nullptr, &count, nullptr), "enumerate device extensions");
    extensions.resize(count);check(vkEnumerateDeviceExtensionProperties(c->physical, nullptr, &count, extensions.data()), "enumerate device extensions");
    enabled.clear();if(window)enabled.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    for (const auto& e : extensions) if (std::strcmp(e.extensionName, "VK_KHR_portability_subset") == 0) enabled.push_back("VK_KHR_portability_subset");
    float priority = 1;VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queue.queueFamilyIndex = family;queue.queueCount = 1;queue.pQueuePriorities = &priority;
    VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};device.queueCreateInfoCount = 1;device.pQueueCreateInfos = &queue;
    device.enabledExtensionCount = uint32_t(enabled.size());device.ppEnabledExtensionNames = enabled.data();
    VkPhysicalDeviceFeatures supported{};vkGetPhysicalDeviceFeatures(c->physical,&supported);if(!supported.independentBlend)throw std::runtime_error("RHI Vulkan requires independent color attachment blending");VkPhysicalDeviceFeatures enabledFeatures{};enabledFeatures.independentBlend=VK_TRUE;enabledFeatures.fillModeNonSolid=supported.fillModeNonSolid;c->wireframe=supported.fillModeNonSolid;device.pEnabledFeatures=&enabledFeatures;
    check(vkCreateDevice(c->physical, &device, nullptr, &c->device), "create device");vkGetDeviceQueue(c->device, family, 0, &c->queue);
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex = family;pool.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    check(vkCreateCommandPool(c->device, &pool, nullptr, &c->commands), "create command pool");
    std::cout << "Vulkan device: " << c->properties.deviceName << "; Khronos validation " << (validation ? "enabled" : "unavailable") << '\n';
    return c;
}
BufferLimits bufferLimitsFor(const Context& c) {
    const auto& l = c.properties.limits;
    return {size_t(std::numeric_limits<uint32_t>::max()), l.maxUniformBufferRange, size_t(std::max<VkDeviceSize>(1, l.minUniformBufferOffsetAlignment)), l.maxDescriptorSetUniformBuffers};
}
struct Buffer { VkBuffer gpu = VK_NULL_HANDLE;VkDeviceMemory memory = VK_NULL_HANDLE;size_t size = 0; };
struct Image { VkImage gpu = VK_NULL_HANDLE;VkDeviceMemory memory = VK_NULL_HANDLE;VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT; };
struct View { VkImageView gpu = VK_NULL_HANDLE;uint64_t image = 0; };
struct Pipeline {
    VkPipeline gpu = VK_NULL_HANDLE;VkPipelineLayout layout = VK_NULL_HANDLE;VkRenderPass pass = VK_NULL_HANDLE;
    std::array<VkDescriptorSetLayout, 3> sets{};
};
class VulkanDevice final : public GraphicsDevice {
public:
    explicit VulkanDevice(std::unique_ptr<Context> context) : GraphicsDevice(bufferLimitsFor(*context),
        {context->properties.limits.maxImageDimension2D, context->properties.limits.maxVertexInputAttributes, 3, 8, context->properties.limits.maxColorAttachments}), c_(std::move(context)) {}
    ~VulkanDevice() override { try { close(); } catch (...) {} }
    bool supportsWireframe() const override { return c_->wireframe; }
    ComputeLimits computeLimits() const override {
        const auto& l=c_->properties.limits;
        return {true,{l.maxComputeWorkGroupCount[0],l.maxComputeWorkGroupCount[1],l.maxComputeWorkGroupCount[2]},
            {l.maxComputeWorkGroupSize[0],l.maxComputeWorkGroupSize[1],l.maxComputeWorkGroupSize[2]},l.maxComputeWorkGroupInvocations,size_t(std::max<VkDeviceSize>(1,l.minStorageBufferOffsetAlignment)),l.maxStorageBufferRange,l.maxPerStageDescriptorStorageBuffers,l.maxPerStageDescriptorUniformBuffers,l.maxPerStageDescriptorStorageImages,l.maxPerStageDescriptorSampledImages};
    }
    std::array<uint32_t,2> presentationExtent()const override{int w=0,h=0;if(c_->window)glfwGetFramebufferSize(c_->window,&w,&h);return {uint32_t(w),uint32_t(h)};}
    bool supportsPresentation() const override {return c_ && c_->window;}
    Backend backend() const override { return Backend::Vulkan; }
    bool supportsTexture(Format f, TextureUsage usage) const override {
        if (!isOpen()) return false;
        const uint32_t bits = uint32_t(usage);if (!bits || (bits & ~63u)) return false;
        if (f == Format::Depth32Float) { if (!hasUsage(usage,TextureUsage::DepthAttachment) || (bits & ~uint32_t(TextureUsage::DepthAttachment|TextureUsage::Sampled|TextureUsage::CopySource))) return false; }
        else if ((f != Format::RGBA8UNorm && f != Format::RGBA16Float && f != Format::RGBA32Float) || hasUsage(usage, TextureUsage::DepthAttachment)) return false;
        VkFormatProperties p{};vkGetPhysicalDeviceFormatProperties(c_->physical, format(f), &p);
        VkFormatFeatureFlags required = 0;
        if (hasUsage(usage, TextureUsage::Storage)) required |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
        if (hasUsage(usage, TextureUsage::Sampled)) required |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if (hasUsage(usage, TextureUsage::ColorAttachment)) required |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
        if (hasUsage(usage, TextureUsage::DepthAttachment)) required |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (hasUsage(usage, TextureUsage::CopySource)) required |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT;
        if (hasUsage(usage, TextureUsage::CopyDestination)) required |= VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        return (p.optimalTilingFeatures & required) == required;
    }
protected:
    Buffer allocateBuffer(size_t size, VkBufferUsageFlags usage) {
        Buffer b;b.size = size;
        try {
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size = size;info.usage = usage;info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            check(vkCreateBuffer(c_->device, &info, nullptr, &b.gpu), "create buffer");
            VkMemoryRequirements requirements{};vkGetBufferMemoryRequirements(c_->device, b.gpu, &requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = c_->memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            check(vkAllocateMemory(c_->device, &allocation, nullptr, &b.memory), "allocate buffer memory");
            check(vkBindBufferMemory(c_->device, b.gpu, b.memory, 0), "bind buffer memory");return b;
        } catch (...) { freeBuffer(b);throw; }
    }
    void freeBuffer(const Buffer& b) noexcept { if (b.gpu) vkDestroyBuffer(c_->device, b.gpu, nullptr);if (b.memory) vkFreeMemory(c_->device, b.memory, nullptr); }
    void transfer(const Buffer& b, size_t offset, size_t size, void* data, bool write) {
        void* mapped = nullptr;check(vkMapMemory(c_->device, b.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "map buffer");
        if (write) std::memcpy(static_cast<uint8_t*>(mapped) + offset, data, size);else std::memcpy(data, static_cast<uint8_t*>(mapped) + offset, size);
        vkUnmapMemory(c_->device, b.memory);
    }
    NativeBuffer createBufferImpl(const BufferDesc& desc, const void* data) override {
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (hasUsage(desc.usage, BufferUsage::Uniform)) usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (hasUsage(desc.usage, BufferUsage::Storage)) usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (hasUsage(desc.usage, BufferUsage::Vertex)) usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if (hasUsage(desc.usage, BufferUsage::Index)) usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (hasUsage(desc.usage, BufferUsage::Indirect)) usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        auto b = allocateBuffer(desc.size, usage);
        try {
            std::vector<uint8_t> zero;if (!data) { zero.resize(desc.size);data = zero.data(); }
            transfer(b, 0, desc.size, const_cast<void*>(data), true);
            const auto id = next_++;buffers_.emplace(id, b);return id;
        } catch (...) { freeBuffer(b);throw; }
    }
    void destroyBufferImpl(NativeBuffer id) noexcept override { auto it = buffers_.find(id);if (it != buffers_.end()) { freeBuffer(it->second);buffers_.erase(it); } }
    void writeBufferImpl(NativeBuffer id,size_t offset,size_t bytes,const void* data) override {
        if(!frameActive()){waitIdleImpl();transfer(buffers_.at(id),offset,bytes,const_cast<void*>(data),true);return;}
        auto staging=allocateBuffer(bytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);transfer(staging,0,bytes,const_cast<void*>(data),true);
        try{execute([&](VkCommandBuffer command){
            VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};before.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;before.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&before,0,nullptr,0,nullptr);
            VkBufferCopy copy{0,offset,bytes};vkCmdCopyBuffer(command,staging.gpu,buffers_.at(id).gpu,1,&copy);
            VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER};after.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;after.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&after,0,nullptr,0,nullptr);
        },true,[this,staging]{freeBuffer(staging);});}catch(...){freeBuffer(staging);throw;}
    }
    void readBufferImpl(NativeBuffer id, size_t offset, size_t bytes, void* data) override { waitIdleImpl();transfer(buffers_.at(id), offset, bytes, data, false); }
    // Compatibility binding has no implicit draw on this backend. Vulkan draws
    // always resolve the explicit pipeline and BindingSet descriptor layouts.
    void bindUniformBufferImpl(uint32_t, NativeBuffer, size_t, size_t) override {
        throw std::logic_error("RHI Vulkan: legacy uniform binding is unsupported; use BindingSet");
    }
    void beginFrameImpl() override {
        if(!c_->window)return;int width,height;glfwGetFramebufferSize(c_->window,&width,&height);
        if(width<=0 || height<=0)throw std::invalid_argument("RHI Vulkan: cannot begin minimized surface frame");
        if(!swapchain_ || extent_.width!=uint32_t(width) || extent_.height!=uint32_t(height))recreateSwapchain();
    }
    void acquireSurface() {
        if(!c_->window)return;
        if(acquired_)throw std::logic_error("RHI Vulkan: frame already acquired");
        int width,height;glfwGetFramebufferSize(c_->window,&width,&height);
        if(width<=0 || height<=0)throw std::invalid_argument("RHI Vulkan: cannot acquire minimized surface");
        if(!swapchain_ || extent_.width!=uint32_t(width) || extent_.height!=uint32_t(height))recreateSwapchain();
        VkFence fence=VK_NULL_HANDLE;VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(c_->device,&info,nullptr,&fence),"create acquire fence");
        VkResult result=vkAcquireNextImageKHR(c_->device,swapchain_,UINT64_MAX,VK_NULL_HANDLE,fence,&imageIndex_);
        if(result==VK_ERROR_OUT_OF_DATE_KHR) { vkDestroyFence(c_->device,fence,nullptr);recreateSwapchain();acquireSurface();return; }
        if(result!=VK_SUCCESS && result!=VK_SUBOPTIMAL_KHR){vkDestroyFence(c_->device,fence,nullptr);check(result,"acquire image");}
        result=vkWaitForFences(c_->device,1,&fence,VK_TRUE,UINT64_MAX);vkDestroyFence(c_->device,fence,nullptr);check(result,"wait acquire");acquired_=true;copied_=false;
    }
    void presentImpl() override {
        if(!c_->window){waitIdleImpl();return;}
        if(!acquired_ || !copied_)throw std::logic_error("RHI Vulkan: copyToBackbuffer required before present");
        VkPresentInfoKHR info{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};info.swapchainCount=1;info.pSwapchains=&swapchain_;info.pImageIndices=&imageIndex_;info.waitSemaphoreCount=1;info.pWaitSemaphores=&renderFinished_[imageIndex_];
        const auto result=vkQueuePresentKHR(c_->queue,&info);acquired_=false;copied_=false;

        if(result==VK_ERROR_OUT_OF_DATE_KHR || result==VK_SUBOPTIMAL_KHR)recreateSwapchain();else check(result,"present");
    }
    void waitIdleImpl() override { check(vkDeviceWaitIdle(c_->device), "wait idle");collectSubmissions(true); }
    uint64_t signalCompletionImpl() override {return execute([](VkCommandBuffer){},true);}
    bool completionReadyImpl(uint64_t serial) override {collectSubmissions();return serial<=completed_;}
    void waitCompletionImpl(uint64_t serial) override {
        if(serial<=completed_)return;for(const auto& p:pending_)if(p.serial==serial){check(vkWaitForFences(c_->device,1,&p.fence,VK_TRUE,UINT64_MAX),"wait frame fence");collectSubmissions();return;}throw std::logic_error("RHI Vulkan missing frame fence");
    }
    void closeImpl() override {destroySwapchain();c_.reset();}
    NativeObject createTextureImpl(const TextureDesc& desc) override {
        Image image;image.aspect = desc.format == Format::Depth32Float ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
        try {
            VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType = VK_IMAGE_TYPE_2D;info.format = format(desc.format);
            info.extent = {desc.width, desc.height, 1};info.mipLevels = info.arrayLayers = 1;info.samples = VK_SAMPLE_COUNT_1_BIT;
            info.tiling = VK_IMAGE_TILING_OPTIMAL;info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (hasUsage(desc.usage, TextureUsage::Storage)) info.usage |= VK_IMAGE_USAGE_STORAGE_BIT;
            if (hasUsage(desc.usage, TextureUsage::Sampled)) info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
            if (hasUsage(desc.usage, TextureUsage::ColorAttachment)) info.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
            if (hasUsage(desc.usage, TextureUsage::DepthAttachment)) info.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            if (hasUsage(desc.usage, TextureUsage::CopySource)) info.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            if (hasUsage(desc.usage, TextureUsage::CopyDestination)) info.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            check(vkCreateImage(c_->device, &info, nullptr, &image.gpu), "create image");
            VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(c_->device, image.gpu, &requirements);
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize = requirements.size;
            allocation.memoryTypeIndex = c_->memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            check(vkAllocateMemory(c_->device, &allocation, nullptr, &image.memory), "allocate image memory");
            check(vkBindImageMemory(c_->device, image.gpu, image.memory, 0), "bind image memory");
            const auto id = next_++;images_.emplace(id, image);return id;
        } catch (...) { if (image.gpu) vkDestroyImage(c_->device, image.gpu, nullptr);if (image.memory) vkFreeMemory(c_->device, image.memory, nullptr);throw; }
    }
    NativeObject createTextureViewImpl(NativeObject imageId, const TextureDesc& desc) override {
        const auto& image = images_.at(imageId);VkImageView view = VK_NULL_HANDLE;
        VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};info.image = image.gpu;info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = format(desc.format);info.subresourceRange = {image.aspect, 0, 1, 0, 1};
        check(vkCreateImageView(c_->device, &info, nullptr, &view), "create image view");
        try { const auto id = next_++;views_.emplace(id, View{view, imageId});return id; } catch (...) { vkDestroyImageView(c_->device, view, nullptr);throw; }
    }
    NativeObject createSamplerImpl(const SamplerDesc& desc) override {
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};info.magFilter = info.minFilter = desc.filter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;info.addressModeU = info.addressModeV = info.addressModeW = desc.address == AddressMode::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VkSampler sampler = VK_NULL_HANDLE;check(vkCreateSampler(c_->device, &info, nullptr, &sampler), "create sampler");
        try { const auto id = next_++;samplers_.emplace(id, sampler);return id; } catch (...) { vkDestroySampler(c_->device, sampler, nullptr);throw; }
    }
    VkRenderPass createPass(const std::vector<Format>& formats, bool depth, const std::vector<ColorAttachment>& colors, LoadOp depthLoad, StoreOp depthStore) {
        std::vector<VkAttachmentDescription> attachments(formats.size() + (depth ? 1 : 0));
        std::vector<VkAttachmentReference> refs;
        for (size_t i = 0; i < formats.size(); ++i) {
            auto& a = attachments[i];a.format = format(formats[i]);a.samples = VK_SAMPLE_COUNT_1_BIT;
            a.loadOp = loadOp(colors[i].load);a.storeOp = storeOp(colors[i].store);
            a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            refs.push_back({uint32_t(i), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
        }
        VkAttachmentReference depthRef{uint32_t(formats.size()), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        if (depth) {
            auto& a = attachments.back();a.samples=VK_SAMPLE_COUNT_1_BIT;a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;a.format = VK_FORMAT_D32_SFLOAT;
            a.loadOp = loadOp(depthLoad);a.storeOp = storeOp(depthStore);a.initialLayout = a.finalLayout = depthRef.layout;
        }
        VkSubpassDescription subpass{};subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = uint32_t(refs.size());subpass.pColorAttachments = refs.data();if (depth) subpass.pDepthStencilAttachment = &depthRef;
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};info.attachmentCount = uint32_t(attachments.size());info.pAttachments = attachments.data();info.subpassCount = 1;info.pSubpasses = &subpass;
        VkRenderPass pass = VK_NULL_HANDLE;check(vkCreateRenderPass(c_->device, &info, nullptr, &pass), "create render pass");return pass;
    }
    void freePipeline(const Pipeline& p) noexcept {
        if (p.gpu) vkDestroyPipeline(c_->device, p.gpu, nullptr);if (p.layout) vkDestroyPipelineLayout(c_->device, p.layout, nullptr);
        if (p.pass) vkDestroyRenderPass(c_->device, p.pass, nullptr);for (auto set : p.sets) if (set) vkDestroyDescriptorSetLayout(c_->device, set, nullptr);
    }
    NativeObject createComputePipelineImpl(const ComputePipelineDesc& desc) override {
        validateComputeShaderLayout(desc);Pipeline p;VkShaderModule module=VK_NULL_HANDLE;
        try {
            const auto words=readSpirv(desc.shader.spirvPath);VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};shader.codeSize=words.size()*4;shader.pCode=words.data();check(vkCreateShaderModule(c_->device,&shader,nullptr,&module),"create compute shader");
            for(uint32_t group=0;group<3;++group) {
                std::vector<VkDescriptorSetLayoutBinding> bindings;
                for(const auto& l:desc.bindings)if(l.group==group)for(const auto& e:l.entries)bindings.push_back({e.binding,isStorage(e.type)?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:isStorageTexture(e.type)?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:e.type==BindingType::SampledTexture?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr});
                VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};layout.bindingCount=uint32_t(bindings.size());layout.pBindings=bindings.data();check(vkCreateDescriptorSetLayout(c_->device,&layout,nullptr,&p.sets[group]),"create compute descriptor layout");
            }
            VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=3;layout.pSetLayouts=p.sets.data();check(vkCreatePipelineLayout(c_->device,&layout,nullptr,&p.layout),"create compute layout");
            VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};info.layout=p.layout;info.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;info.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;info.stage.module=module;info.stage.pName=desc.shader.spirvEntryPoint.c_str();check(vkCreateComputePipelines(c_->device,VK_NULL_HANDLE,1,&info,nullptr,&p.gpu),"create compute pipeline");
            const auto id=next_++;computePipelines_.emplace(id,p);vkDestroyShaderModule(c_->device,module,nullptr);return id;
        } catch(...) { if(module)vkDestroyShaderModule(c_->device,module,nullptr);freePipeline(p);throw; }
    }
    void destroyComputePipelineImpl(NativeObject id) noexcept override {freePipeline(computePipelines_.at(id));computePipelines_.erase(id);}
    NativeObject createPipelineImpl(const GraphicsPipelineDesc& desc) override {
        validateShaderLayout(desc);Pipeline p;VkShaderModule modules[2]{};
        try {
            const ShaderAsset* assets[] = {&desc.vertex, &desc.fragment};
            for (unsigned i = 0; i < 2; ++i) {
                const auto words = readSpirv(assets[i]->spirvPath);VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};info.codeSize = words.size() * 4;info.pCode = words.data();
                check(vkCreateShaderModule(c_->device, &info, nullptr, &modules[i]), "create shader module");
                std::vector<VkDescriptorSetLayoutBinding> bindings;
                for (const auto& l : desc.bindings) if (l.group == i) for (const auto& e : l.entries)
                    bindings.push_back({e.binding, e.type == BindingType::UniformBuffer ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : isStorage(e.type)?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                        e.stage == ShaderStage::Vertex ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
                VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};layout.bindingCount = uint32_t(bindings.size());layout.pBindings = bindings.data();
                check(vkCreateDescriptorSetLayout(c_->device, &layout, nullptr, &p.sets[i]), "create descriptor layout");
            }
            VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount = 3;
            {std::vector<VkDescriptorSetLayoutBinding> bindings;for(const auto& l:desc.bindings)if(l.group==2)for(const auto& e:l.entries)bindings.push_back({e.binding,e.type==BindingType::UniformBuffer?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:isStorage(e.type)?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,e.stage==ShaderStage::Vertex?VK_SHADER_STAGE_VERTEX_BIT:VK_SHADER_STAGE_FRAGMENT_BIT,nullptr});VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};info.bindingCount=uint32_t(bindings.size());info.pBindings=bindings.data();check(vkCreateDescriptorSetLayout(c_->device,&info,nullptr,&p.sets[2]),"create effect descriptor layout");}layout.pSetLayouts = p.sets.data();
            check(vkCreatePipelineLayout(c_->device, &layout, nullptr, &p.layout), "create pipeline layout");
            const auto formats = colorFormats(desc);p.pass = createPass(formats, desc.depthAttachment, std::vector<ColorAttachment>(formats.size()), LoadOp::Clear, StoreOp::Store);
            VkPipelineShaderStageCreateInfo stages[2]{};
            for (unsigned i = 0; i < 2; ++i) { stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;stages[i].module = modules[i];stages[i].pName = assets[i]->spirvEntryPoint.c_str(); }
            VkVertexInputBindingDescription binding{0, desc.vertexStride, VK_VERTEX_INPUT_RATE_VERTEX};std::vector<VkVertexInputAttributeDescription> attributes;
            for (const auto& a : desc.attributes) attributes.push_back({a.location, 0, a.format == VertexFormat::Float2 ? VK_FORMAT_R32G32_SFLOAT : a.format == VertexFormat::Float3 ? VK_FORMAT_R32G32B32_SFLOAT : VK_FORMAT_R32G32B32A32_SFLOAT, a.offset});
            VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};vertex.vertexBindingDescriptionCount = 1;vertex.pVertexBindingDescriptions = &binding;
            vertex.vertexAttributeDescriptionCount = uint32_t(attributes.size());vertex.pVertexAttributeDescriptions = attributes.data();
            VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport.viewportCount = viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode = desc.wireframe?VK_POLYGON_MODE_LINE:VK_POLYGON_MODE_FILL;raster.cullMode = desc.cull==CullMode::None?VK_CULL_MODE_NONE:desc.cull==CullMode::Back?VK_CULL_MODE_BACK_BIT:VK_CULL_MODE_FRONT_BIT;raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth = 1;
            VkPipelineMultisampleStateCreateInfo samples{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};depth.depthTestEnable = desc.depthTest;depth.depthWriteEnable = desc.depthWrite;depth.depthCompareOp = desc.depthCompare==DepthCompare::Less?VK_COMPARE_OP_LESS:desc.depthCompare==DepthCompare::LessEqual?VK_COMPARE_OP_LESS_OR_EQUAL:desc.depthCompare==DepthCompare::Greater?VK_COMPARE_OP_GREATER:VK_COMPARE_OP_ALWAYS;
            VkPipelineColorBlendAttachmentState attachment{};attachment.colorWriteMask = 15;attachment.blendEnable = desc.blend;
            attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;attachment.colorBlendOp = VK_BLEND_OP_ADD;
            attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;attachment.alphaBlendOp = VK_BLEND_OP_ADD;
            VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};std::vector<VkPipelineColorBlendAttachmentState> blends(formats.size(), attachment);for(size_t i=0;i<blends.size();++i)blends[i].blendEnable=desc.attachmentBlend.empty()?desc.blend:desc.attachmentBlend.at(i);blend.attachmentCount = uint32_t(blends.size());blend.pAttachments = blends.data();
            const VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount = 2;dynamic.pDynamicStates = dynamics;
            VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};info.stageCount = 2;info.pStages = stages;info.pVertexInputState = &vertex;info.pInputAssemblyState = &assembly;
            info.pViewportState = &viewport;info.pRasterizationState = &raster;info.pMultisampleState = &samples;info.pDepthStencilState = &depth;info.pColorBlendState = &blend;info.pDynamicState = &dynamic;info.layout = p.layout;info.renderPass = p.pass;
            check(vkCreateGraphicsPipelines(c_->device, VK_NULL_HANDLE, 1, &info, nullptr, &p.gpu), "create graphics pipeline");
            const auto id = next_++;pipelines_.emplace(id, p);for (auto m : modules) vkDestroyShaderModule(c_->device, m, nullptr);return id;
        } catch (...) { for (auto m : modules) if (m) vkDestroyShaderModule(c_->device, m, nullptr);freePipeline(p);throw; }
    }
    void destroyTextureImpl(NativeObject id) noexcept override { const auto& image = images_.at(id);vkDestroyImage(c_->device, image.gpu, nullptr);vkFreeMemory(c_->device, image.memory, nullptr);images_.erase(id); }
    void destroyTextureViewImpl(NativeObject id) noexcept override { vkDestroyImageView(c_->device, views_.at(id).gpu, nullptr);views_.erase(id); }
    void destroySamplerImpl(NativeObject id) noexcept override { vkDestroySampler(c_->device, samplers_.at(id), nullptr);samplers_.erase(id); }
    void destroyPipelineImpl(NativeObject id) noexcept override { freePipeline(pipelines_.at(id));pipelines_.erase(id); }
    struct Pending{uint64_t serial;VkCommandBuffer command;VkFence fence;std::function<void()> release;};
    void collectSubmissions(bool all=false){
        while(!pending_.empty()){
            auto& p=pending_.front();if(!all){auto status=vkGetFenceStatus(c_->device,p.fence);if(status==VK_NOT_READY)break;check(status,"poll submission fence");}
            if(p.release)p.release();vkDestroyFence(c_->device,p.fence,nullptr);vkFreeCommandBuffers(c_->device,c_->commands,1,&p.command);completed_=p.serial;pending_.pop_front();
        }
    }
    uint64_t execute(const std::function<void(VkCommandBuffer)>& record,bool defer=false,std::function<void()> release={},VkSemaphore signal=VK_NULL_HANDLE) {
        collectSubmissions();VkCommandBuffer command=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;
        std::unordered_map<uint64_t,VkImageLayout> previousLayouts;for(const auto& image:images_)previousLayouts.emplace(image.first,image.second.layout);bool submitted=false;
        try{
            VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocation.commandPool=c_->commands;allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocation.commandBufferCount=1;check(vkAllocateCommandBuffers(c_->device,&allocation,&command),"allocate command buffer");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(vkBeginCommandBuffer(command,&begin),"begin command buffer");record(command);check(vkEndCommandBuffer(command),"end command buffer");
            VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};check(vkCreateFence(c_->device,&info,nullptr,&fence),"create submission fence");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;if(signal){submit.signalSemaphoreCount=1;submit.pSignalSemaphores=&signal;}
            check(vkQueueSubmit(c_->queue,1,&submit,fence),"submit commands");submitted=true;
            const auto serial=++submitted_;if(defer){pending_.push_back({serial,command,fence,std::move(release)});return serial;}
            check(vkWaitForFences(c_->device,1,&fence,VK_TRUE,UINT64_MAX),"wait submission fence");collectSubmissions();vkDestroyFence(c_->device,fence,nullptr);vkFreeCommandBuffers(c_->device,c_->commands,1,&command);completed_=serial;if(release)release();return serial;
        }catch(...){vkDeviceWaitIdle(c_->device);if(!submitted)for(const auto& layout:previousLayouts)images_.at(layout.first).layout=layout.second;if(fence)vkDestroyFence(c_->device,fence,nullptr);if(command)vkFreeCommandBuffers(c_->device,c_->commands,1,&command);throw;}
    }
    void transition(VkCommandBuffer command, Image& image, VkImageLayout layout) {
        // Even the same layout needs a memory dependency between consecutive
        // attachment writes and Load passes. This prototype uses conservative
        // stages; finer resource-state scheduling belongs to the render graph.
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.oldLayout = image.layout;barrier.newLayout = layout;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;barrier.image = image.gpu;barrier.subresourceRange = {image.aspect, 0, 1, 0, 1};
        barrier.srcAccessMask = image.layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(command, image.layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);image.layout = layout;
    }
    void writeTextureRegionImpl(NativeObject id, const TextureDesc& desc, TextureRegion r, const void* pixels, size_t bytes) override {
        auto staging = allocateBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        try {
            transfer(staging, 0, bytes, const_cast<void*>(pixels), true);
            execute([&](VkCommandBuffer command) {
                auto& image = images_.at(id);transition(command, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                VkBufferImageCopy copy{};copy.imageSubresource = {image.aspect, 0, 0, 1};copy.imageOffset = {int32_t(r.x),int32_t(r.y),0};copy.imageExtent = {r.width, r.height, 1};
                vkCmdCopyBufferToImage(command, staging.gpu, image.gpu, image.layout, 1, &copy);
            },frameActive(),[this,staging]{freeBuffer(staging);});
        } catch (...) { freeBuffer(staging);throw; }
    }
    void writeTextureImpl(NativeObject id,const TextureDesc& desc,const void* pixels,size_t bytes) override {writeTextureRegionImpl(id,desc,{0,0,desc.width,desc.height},pixels,bytes);}
    std::vector<uint8_t> readPixels(NativeObject id, const TextureDesc& desc, size_t pixelBytes) {
        std::vector<uint8_t> result(size_t(desc.width) * desc.height * pixelBytes);auto staging = allocateBuffer(result.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        try {
            execute([&](VkCommandBuffer command) {
                auto& image = images_.at(id);transition(command, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                VkBufferImageCopy copy{};copy.imageSubresource = {image.aspect, 0, 0, 1};copy.imageExtent = {desc.width, desc.height, 1};
                vkCmdCopyImageToBuffer(command, image.gpu, image.layout, staging.gpu, 1, &copy);
                VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0, nullptr);
            });transfer(staging, 0, result.size(), result.data(), false);freeBuffer(staging);return result;
        } catch (...) { freeBuffer(staging);throw; }
    }
    std::function<void()> queueTextureReadbackImpl(NativeObject id,const TextureDesc& desc,std::shared_ptr<std::vector<uint8_t>> output) override {
        const size_t bytes=size_t(desc.width)*desc.height*(desc.format==Format::RGBA8UNorm || desc.format==Format::Depth32Float?4:desc.format==Format::RGBA16Float?8:16);auto staging=allocateBuffer(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        try{execute([&](VkCommandBuffer command){auto& image=images_.at(id);transition(command,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);VkBufferImageCopy copy{};copy.imageSubresource={image.aspect,0,0,1};copy.imageExtent={desc.width,desc.height,1};vkCmdCopyImageToBuffer(command,image.gpu,image.layout,staging.gpu,1,&copy);VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);},true,[this,staging,bytes,output]{output->resize(bytes);transfer(staging,0,bytes,output->data(),false);freeBuffer(staging);});}catch(...){freeBuffer(staging);throw;}return {};
    }
    std::vector<uint8_t> readTextureImpl(NativeObject id, const TextureDesc& desc) override { return readPixels(id, desc, 4); }
    void writeTextureFloatImpl(NativeObject id,const TextureDesc& desc,const float* pixels,size_t bytes) override {writeTextureImpl(id,desc,pixels,bytes);}
    void copyToBackbufferImpl(NativeObject id,const TextureDesc& desc) override {
        if(!frameActive())throw std::logic_error("RHI Vulkan: beginFrame required before copy");
        if(!acquired_)acquireSurface();
        if(desc.width!=extent_.width || desc.height!=extent_.height)throw std::invalid_argument("RHI Vulkan: output size differs from swapchain");
        execute([&](VkCommandBuffer command) {
            auto& source=images_.at(id);transition(command,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.oldLayout=presented_[imageIndex_]?VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=swapImages_[imageIndex_];barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
            VkImageBlit blit{};blit.srcSubresource=blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};blit.srcOffsets[1]=blit.dstOffsets[1]={int32_t(desc.width),int32_t(desc.height),1};
            vkCmdBlitImage(command,source.gpu,source.layout,swapImages_[imageIndex_],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
            barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=0;
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        },frameActive(),{},renderFinished_[imageIndex_]);presented_[imageIndex_]=true;copied_=true;
    }
    std::vector<float> readTextureFloatImpl(NativeObject id, const TextureDesc& desc) override {
        if(desc.format==Format::RGBA16Float)return decodeHalfPixels(readPixels(id,desc,8));
        const auto bytes=readPixels(id,desc,desc.format==Format::Depth32Float?4:16);std::vector<float> values(bytes.size()/4);std::memcpy(values.data(),bytes.data(),bytes.size());return values;
    }
    void submitGraphicsImpl(const std::vector<RecordedPass>& passes) override {
        size_t draws = 0;for (const auto& pass : passes) draws += pass.compute ? 1 : pass.draws.size();
        if (draws > UINT32_MAX / 24) throw std::invalid_argument("RHI Vulkan: command list too large");
        VkDescriptorPool pool = VK_NULL_HANDLE;auto renderPassOwner=std::make_shared<std::vector<VkRenderPass>>();auto framebufferOwner=std::make_shared<std::vector<VkFramebuffer>>();auto& renderPasses=*renderPassOwner;auto& framebuffers=*framebufferOwner;
        auto cleanup = [&] { for (auto f : framebuffers) vkDestroyFramebuffer(c_->device, f, nullptr);for (auto p : renderPasses) vkDestroyRenderPass(c_->device, p, nullptr);if (pool) vkDestroyDescriptorPool(c_->device, pool, nullptr); };
        try {
            if (draws) {
                VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uint32_t(draws * 24)}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, uint32_t(draws * 24)}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, uint32_t(draws * 24)}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, uint32_t(draws * 24)}};
                VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};info.maxSets = uint32_t(draws * 3);info.poolSizeCount = 4;info.pPoolSizes = sizes;
                check(vkCreateDescriptorPool(c_->device, &info, nullptr, &pool), "create descriptor pool");
            }
            execute([&](VkCommandBuffer command) {
                for (const auto& pass : passes) {
                    if(pass.copy) {
                        auto& source=images_.at(textureObject(pass.copySource));auto& destination=images_.at(textureObject(pass.copyDestination));const auto& desc=textureDesc(pass.copySource);
                        transition(command,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);transition(command,destination,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
                        VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.extent={desc.width,desc.height,1};vkCmdCopyImage(command,source.gpu,source.layout,destination.gpu,destination.layout,1,&copy);continue;
                    }
                    // Conservative dependency boundary covers earlier submissions as well as this list.
                    VkMemoryBarrier memory{VK_STRUCTURE_TYPE_MEMORY_BARRIER};memory.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;memory.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
                    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&memory,0,nullptr,0,nullptr);
                    if(pass.compute) {
                        const auto& p=computePipelines_.at(computePipelineObject(pass.dispatch.pipeline));vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.gpu);
                        std::array<VkDescriptorSet,3> sets{};VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocation.descriptorPool=pool;allocation.descriptorSetCount=3;allocation.pSetLayouts=p.sets.data();check(vkAllocateDescriptorSets(c_->device,&allocation,sets.data()),"allocate compute descriptors");
                        for(auto set:pass.dispatch.bindings)for(const auto& b:resolvedBindings(set)) {
                            VkDescriptorBufferInfo buffer{};VkDescriptorImageInfo image{};VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=sets[b.layout.binding/8];write.dstBinding=b.layout.binding%8;write.descriptorCount=1;
                            if(b.buffer) { buffer={buffers_.at(b.buffer).gpu,b.offset,b.size};write.descriptorType=isStorage(b.layout.type)?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;write.pBufferInfo=&buffer; }
                            else {
                                const auto layout=isStorageTexture(b.layout.type)?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                                const auto& view=views_.at(b.textureView);transition(command,images_.at(view.image),layout);
                                image={b.sampler?samplers_.at(b.sampler):VK_NULL_HANDLE,view.gpu,layout};write.descriptorType=isStorageTexture(b.layout.type)?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo=&image;
                            }
                            vkUpdateDescriptorSets(c_->device,1,&write,0,nullptr);
                        }
                        vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,p.layout,0,3,sets.data(),0,nullptr);const auto& groups=pass.dispatch.groups;if(pass.dispatch.indirect)vkCmdDispatchIndirect(command,buffers_.at(nativeBuffer(pass.dispatch.indirect,BufferUsage::Indirect)).gpu,pass.dispatch.indirectOffset);else vkCmdDispatch(command,groups[0],groups[1],groups[2]);continue;
                    }
                    const auto& target = viewTextureDesc(pass.desc.color?pass.desc.color:pass.desc.depth);const auto colors = colorAttachments(pass.desc);
                    // Sampling barriers must precede vkCmdBeginRenderPass.
                    for (const auto& draw : pass.draws) for (auto set : draw.bindings) for (const auto& b : resolvedBindings(set))
                        if (b.textureView) transition(command, images_.at(views_.at(b.textureView).image), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                    std::vector<VkImageView> attachments;std::vector<Format> formats;std::vector<VkClearValue> clears;
                    for (const auto& color : colors) {
                        const auto& view = views_.at(textureViewObject(color.view));transition(command, images_.at(view.image), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
                        attachments.push_back(view.gpu);formats.push_back(viewTextureDesc(color.view).format);
                        VkClearValue clear{};std::copy(color.clear.begin(), color.clear.end(), clear.color.float32);clears.push_back(clear);
                    }
                    if (pass.desc.depth) { const auto& depth = views_.at(textureViewObject(pass.desc.depth));transition(command, images_.at(depth.image), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);attachments.push_back(depth.gpu);VkClearValue clear{};clear.depthStencil.depth = pass.desc.clearDepth;clears.push_back(clear); }
                    auto renderPass = createPass(formats, bool(pass.desc.depth), colors, pass.desc.depthLoad, pass.desc.depthStore);renderPasses.push_back(renderPass);
                    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass = renderPass;fb.attachmentCount = uint32_t(attachments.size());fb.pAttachments = attachments.data();fb.width = target.width;fb.height = target.height;fb.layers = 1;
                    VkFramebuffer framebuffer = VK_NULL_HANDLE;check(vkCreateFramebuffer(c_->device, &fb, nullptr, &framebuffer), "create framebuffer");framebuffers.push_back(framebuffer);
                    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass = renderPass;begin.framebuffer = framebuffer;begin.renderArea.extent = {target.width, target.height};begin.clearValueCount = fb.attachmentCount;begin.pClearValues = clears.data();
                    vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
                    const auto v=pass.desc.viewport.width?pass.desc.viewport:Viewport{0,0,target.width,target.height};
                    VkViewport viewport{float(v.x),float(v.y+v.height),float(v.width),-float(v.height),0,1};const auto clip=pass.desc.scissor.width?pass.desc.scissor:v;VkRect2D scissor{{int32_t(clip.x),int32_t(clip.y)},{clip.width,clip.height}};
                    vkCmdSetViewport(command, 0, 1, &viewport);vkCmdSetScissor(command, 0, 1, &scissor);
                    for (const auto& draw : pass.draws) {
                        const auto& p = pipelines_.at(pipelineObject(draw.pipeline));vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, p.gpu);
                        std::array<VkDescriptorSet, 3> sets{};VkDescriptorSetAllocateInfo allocation{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocation.descriptorPool = pool;allocation.descriptorSetCount = 3;allocation.pSetLayouts = p.sets.data();
                        check(vkAllocateDescriptorSets(c_->device, &allocation, sets.data()), "allocate descriptor sets");
                        for (auto set : draw.bindings) for (const auto& b : resolvedBindings(set)) {
                            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet = sets[b.layout.binding / 8];write.dstBinding = b.layout.binding % 8;write.descriptorCount = 1;
                            VkDescriptorBufferInfo buffer{};VkDescriptorImageInfo image{};
                            if (b.buffer) { buffer = {buffers_.at(b.buffer).gpu, b.offset, b.size};write.descriptorType = isStorage(b.layout.type)?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;write.pBufferInfo = &buffer; }
                            else { image = {samplers_.at(b.sampler), views_.at(b.textureView).gpu, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;write.pImageInfo = &image; }
                            vkUpdateDescriptorSets(c_->device, 1, &write, 0, nullptr);
                        }
                        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout, 0, 3, sets.data(), 0, nullptr);
                        const auto vertices = buffers_.at(nativeBuffer(draw.vertices, BufferUsage::Vertex)).gpu;const VkDeviceSize offset = draw.vertexOffset;
                        vkCmdBindVertexBuffers(command, 0, 1, &vertices, &offset);
                        if (draw.indirect) {
                            const auto arguments=buffers_.at(nativeBuffer(draw.indirect,BufferUsage::Indirect)).gpu;
                            if(draw.indexed) { vkCmdBindIndexBuffer(command,buffers_.at(nativeBuffer(draw.indices,BufferUsage::Index)).gpu,draw.indexOffset,draw.indexType==IndexType::UInt16?VK_INDEX_TYPE_UINT16:VK_INDEX_TYPE_UINT32);vkCmdDrawIndexedIndirect(command,arguments,draw.indirectOffset,1,sizeof(DrawIndexedIndirectArguments)); }
                            else vkCmdDrawIndirect(command,arguments,draw.indirectOffset,1,sizeof(DrawIndirectArguments));
                        } else if (draw.indexed) {
                            vkCmdBindIndexBuffer(command, buffers_.at(nativeBuffer(draw.indices, BufferUsage::Index)).gpu, draw.indexOffset, draw.indexType == IndexType::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
                            vkCmdDrawIndexed(command, draw.count, 1, draw.first, draw.baseVertex, 0);
                        } else vkCmdDraw(command, draw.count, 1, draw.first, 0);
                    }
                    vkCmdEndRenderPass(command);
                }
                VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
                vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
            },frameActive(),[this,framebufferOwner,renderPassOwner,pool]{for(auto f:*framebufferOwner)vkDestroyFramebuffer(c_->device,f,nullptr);for(auto p:*renderPassOwner)vkDestroyRenderPass(c_->device,p,nullptr);if(pool)vkDestroyDescriptorPool(c_->device,pool,nullptr);});
        } catch (...) { cleanup();throw; }
    }
private:
    void destroySwapchain() noexcept {
        for(auto semaphore:renderFinished_)vkDestroySemaphore(c_->device,semaphore,nullptr);renderFinished_.clear();
        if(swapchain_)vkDestroySwapchainKHR(c_->device,swapchain_,nullptr);swapchain_=VK_NULL_HANDLE;swapImages_.clear();presented_.clear();
    }
    void recreateSwapchain() {
        waitIdleImpl();VkSurfaceCapabilitiesKHR capabilities{};check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(c_->physical,c_->surface,&capabilities),"query surface capabilities");
        uint32_t count=0;check(vkGetPhysicalDeviceSurfaceFormatsKHR(c_->physical,c_->surface,&count,nullptr),"query surface formats");std::vector<VkSurfaceFormatKHR> formats(count);check(vkGetPhysicalDeviceSurfaceFormatsKHR(c_->physical,c_->surface,&count,formats.data()),"query surface formats");
        auto selected=std::find_if(formats.begin(),formats.end(),[](const auto& f){return (f.format==VK_FORMAT_B8G8R8A8_UNORM || f.format==VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;});
        if(selected==formats.end() || !(capabilities.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT))throw std::runtime_error("RHI Vulkan: display format/transfer destination unavailable");
        VkFormatProperties properties{};vkGetPhysicalDeviceFormatProperties(c_->physical,selected->format,&properties);if(!(properties.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT))throw std::runtime_error("RHI Vulkan: display blit unsupported");
        VkExtent2D extent=capabilities.currentExtent;
        if(extent.width==UINT32_MAX) { int width,height;glfwGetFramebufferSize(c_->window,&width,&height);extent={std::clamp(uint32_t(width),capabilities.minImageExtent.width,capabilities.maxImageExtent.width),std::clamp(uint32_t(height),capabilities.minImageExtent.height,capabilities.maxImageExtent.height)}; }
        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};info.surface=c_->surface;info.minImageCount=capabilities.minImageCount+1;if(capabilities.maxImageCount)info.minImageCount=std::min(info.minImageCount,capabilities.maxImageCount);
        info.imageFormat=selected->format;info.imageColorSpace=selected->colorSpace;info.imageExtent=extent;info.imageArrayLayers=1;info.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT;info.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;info.preTransform=capabilities.currentTransform;
        info.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;if(!(capabilities.supportedCompositeAlpha&info.compositeAlpha))info.compositeAlpha=VkCompositeAlphaFlagBitsKHR(capabilities.supportedCompositeAlpha&(~capabilities.supportedCompositeAlpha+1));
        info.presentMode=VK_PRESENT_MODE_FIFO_KHR;info.clipped=VK_TRUE;info.oldSwapchain=swapchain_;VkSwapchainKHR replacement=VK_NULL_HANDLE;
        check(vkCreateSwapchainKHR(c_->device,&info,nullptr,&replacement),"create swapchain");
        std::vector<VkImage> images;
        try { check(vkGetSwapchainImagesKHR(c_->device,replacement,&count,nullptr),"query swapchain images");images.resize(count);check(vkGetSwapchainImagesKHR(c_->device,replacement,&count,images.data()),"query swapchain images"); }
        catch(...) {vkDestroySwapchainKHR(c_->device,replacement,nullptr);throw;}
        destroySwapchain();swapchain_=replacement;swapImages_=std::move(images);presented_.assign(count,false);extent_=extent;acquired_=false;copied_=false;renderFinished_.resize(count);VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};for(auto& signal:renderFinished_)check(vkCreateSemaphore(c_->device,&semaphore,nullptr,&signal),"create present semaphore");
    }
    uint64_t submitted_=0,completed_=0;std::deque<Pending> pending_;
    std::vector<VkSemaphore> renderFinished_;
    VkSwapchainKHR swapchain_=VK_NULL_HANDLE;
    std::vector<VkImage> swapImages_;
    std::vector<bool> presented_;
    VkExtent2D extent_{};
    uint32_t imageIndex_=0;
    bool acquired_=false,copied_=false;
    std::unique_ptr<Context> c_;
    uint64_t next_ = 1;
    std::unordered_map<uint64_t, Buffer> buffers_;
    std::unordered_map<uint64_t, Image> images_;
    std::unordered_map<uint64_t, View> views_;
    std::unordered_map<uint64_t, VkSampler> samplers_;
    std::unordered_map<uint64_t, Pipeline> pipelines_, computePipelines_;
};
}
void configureVulkanWindowing() { glfwInitVulkanLoader(vkGetInstanceProcAddr); }
std::shared_ptr<GraphicsDevice> makeVulkanDevice(GLFWwindow* window) { return std::make_shared<VulkanDevice>(makeContext(window)); }
} // namespace rhi
