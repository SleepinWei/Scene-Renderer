#include "rhi/ShaderAssets.h"
#include <json/json.hpp>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <stdexcept>
namespace rhi {
std::string defaultShaderDirectory() {
#ifdef SR_RHI_SHADER_DIR
    return SR_RHI_SHADER_DIR;
#else
    return "build/rhi/shaders";
#endif
}
std::string readShaderText(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("RHI: cannot read shader asset " + path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
std::vector<uint32_t> readSpirv(const std::string& path) {
    const auto bytes = readShaderText(path);
    if (bytes.size() < 20 || bytes.size() % 4) throw std::invalid_argument("RHI: invalid SPIR-V size");
    std::vector<uint32_t> words(bytes.size() / 4);
    std::memcpy(words.data(), bytes.data(), bytes.size());
    if (words[0] != 0x07230203) throw std::invalid_argument("RHI: invalid SPIR-V magic");
    return words;
}
void validateComputeShaderLayout(const ComputePipelineDesc& desc) {
    const auto reflection = nlohmann::json::parse(readShaderText(desc.shader.reflectionPath));
    const auto entry = std::find_if(reflection.at("entryPoints").begin(),reflection.at("entryPoints").end(),[&](const auto& e) { return e.at("name") == desc.shader.spirvEntryPoint && e.at("mode") == "comp"; });
    if (entry == reflection.at("entryPoints").end() || entry->at("workgroup_size").get<std::array<uint32_t,3>>() != desc.threads || (entry->contains("workgroup_size_is_spec_constant_id") && (*entry)["workgroup_size_is_spec_constant_id"] != nlohmann::json::array({false,false,false})))
        throw std::invalid_argument("RHI: compute workgroup differs from reflection");
    for (const auto* kind : {"push_constants","separate_images","separate_samplers","subpass_inputs","acceleration_structures"})
        if (!reflection.value(kind,nlohmann::json::array()).empty()) throw std::invalid_argument("RHI: unsupported compute resource category");
    size_t resources = 0, declared = 0;
    for (const auto* kind : {"ubos","ssbos","images","textures"}) for (const auto& resource : reflection.value(kind,nlohmann::json::array())) {
        ++resources;bool found = false;
        for (const auto& l : desc.bindings) for (const auto& e : l.entries) if (l.group == resource.value("set",0u) && e.binding == resource.at("binding")) {
            const auto category = std::string(kind);
            const auto expected = category == "ubos" ? BindingType::UniformBuffer : category == "textures" ? BindingType::SampledTexture : category == "images" ?
                (resource.value("readonly",false) ? BindingType::StorageTextureRead : resource.value("writeonly",false) ? BindingType::StorageTextureWrite : BindingType::StorageTextureReadWrite) :
                (resource.value("readonly",false) ? BindingType::StorageRead : resource.value("writeonly",false) ? BindingType::StorageWrite : BindingType::StorageReadWrite);
            if ((category=="images" && resource.at("type")!="image2D") || (category=="textures" && resource.at("type")!="sampler2D"))throw std::invalid_argument("RHI: non-2D shader image is not supported");
            if (category == "images") {
                const auto expectedFormat = e.imageFormat == Format::RGBA32Float ? "rgba32f" : e.imageFormat == Format::RGBA16Float ? "rgba16f" : "rgba8";
                if (resource.value("format",std::string()) != expectedFormat) throw std::invalid_argument("RHI: storage image format differs from reflection");
            }
            if (e.stage != ShaderStage::Compute || e.type != expected || e.name != resource.at("name") || e.minimumSize < resource.value("block_size",size_t(0)) || resource.contains("array")) throw std::invalid_argument("RHI: compute binding differs from reflection");found = true;
        }
        if (!found) throw std::invalid_argument("RHI: compute resource missing from layout");
    }
    for (const auto& l : desc.bindings) declared += l.entries.size();
    if (resources != declared) throw std::invalid_argument("RHI: unused compute bindings");
}
void validateShaderLayout(const GraphicsPipelineDesc& desc) {
    for (const auto stage : {ShaderStage::Vertex, ShaderStage::Fragment}) {
        const auto& asset = stage == ShaderStage::Vertex ? desc.vertex : desc.fragment;
        const auto reflection = nlohmann::json::parse(readShaderText(asset.reflectionPath));
        for (const auto* kind : {"push_constants", "images", "separate_images", "separate_samplers", "subpass_inputs", "acceleration_structures"})
            if (!reflection.value(kind, nlohmann::json::array()).empty())
                throw std::invalid_argument("RHI: unsupported shader resource category");
        size_t resources = 0, declared = 0;
        for (const auto& kind : {"ubos", "textures", "ssbos"}) for (const auto& r : reflection.value(kind, nlohmann::json::array())) {
            ++resources;bool found = false;
            for (const auto& l : desc.bindings) for (const auto& e : l.entries) {
                if (l.group != r.value("set", 0u) || e.binding != r["binding"] || e.stage != stage) continue;
                const auto type = std::string(kind) == "ubos" ? BindingType::UniformBuffer : std::string(kind)=="ssbos"?BindingType::StorageRead:BindingType::SampledTexture;
                if(std::string(kind)=="ssbos" && (stage!=ShaderStage::Vertex || !r.value("readonly",false)))throw std::invalid_argument("RHI: graphics storage must be read-only vertex data");
                if (e.type != type || e.name != r["name"] || e.minimumSize < r.value("block_size", size_t(0)) || r.contains("array"))
                    throw std::invalid_argument("RHI: binding layout disagrees with shader reflection");
                found = true;
            }
            if (!found) throw std::invalid_argument("RHI: shader resource is missing from pipeline layout");
        }
        for (const auto& l : desc.bindings) for (const auto& e : l.entries) if (e.stage == stage) ++declared;
        if (declared != resources ||
            !reflection.value("images", nlohmann::json::array()).empty())
            throw std::invalid_argument("RHI: unsupported or unused shader bindings");
        if (stage == ShaderStage::Fragment) {
            const auto outputs = reflection.value("outputs", nlohmann::json::array());
            if (outputs.size() != colorFormats(desc).size()) throw std::invalid_argument("RHI: fragment output count differs from color attachments");
            std::vector<bool> seen(outputs.size());
            for (const auto& output : outputs) {
                const auto location = output.at("location").get<size_t>();
                if (location >= seen.size() || seen[location] || output.at("type") != "vec4") throw std::invalid_argument("RHI: unsupported fragment output");
                seen[location] = true;
            }
        }
        if (stage == ShaderStage::Vertex) {
            const auto inputs = reflection.value("inputs", nlohmann::json::array());
            if (inputs.size() != desc.attributes.size()) throw std::invalid_argument("RHI: vertex layout differs from shader inputs");
            for (const auto& input : inputs) {
                bool found = false;
                for (const auto& a : desc.attributes) if (a.location == input["location"]) {
                    const auto type = a.format == VertexFormat::Float2 ? "vec2" : a.format == VertexFormat::Float3 ? "vec3" : "vec4";
                    found = input["type"] == type;
                }
                if (!found) throw std::invalid_argument("RHI: vertex attribute type differs from shader");
            }
        }
    }
}
}
