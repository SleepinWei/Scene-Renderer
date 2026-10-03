#include "renderer/rhi/GpuVirtualTexture.h"
#include "engine/JobSystem.h"
#include <chrono>
#include <json/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <set>
#include <limits>
namespace render {
namespace {
uint32_t extentFor(uint32_t size) {
    if (!size || size > 16384)
        throw std::invalid_argument("VT source extent outside 1..16384");
    uint32_t n = 64;
    while (n < size)
        n *= 2;
    return n;
}
size_t pixelBytes(rhi::Format f) {
    if (f == rhi::Format::RGBA8UNorm)
        return 4;
    if (f == rhi::Format::RGBA32Float)
        return 16;
    throw std::invalid_argument("Unsupported VT plane format");
}
float heightAt(const std::vector<float> &h, uint32_t w, uint32_t n, float u, float v) {
    float px = glm::clamp(u, 0.f, 1.f) * (w - 1), py = glm::clamp(v, 0.f, 1.f) * (n - 1);
    uint32_t x = uint32_t(px), y = uint32_t(py), xx = std::min(x + 1, w - 1), yy = std::min(y + 1, n - 1);
    return glm::mix(glm::mix(h[size_t(y) * w + x], h[size_t(y) * w + xx], px - x),
                    glm::mix(h[size_t(yy) * w + x], h[size_t(yy) * w + xx], px - x), py - y);
}
} // namespace
VirtualTextureSource heightVirtualSource(uint32_t w, uint32_t h, std::vector<float> data) {
    if (w < 2 || h < 2 || data.size() != uint64_t(w) * h)
        throw std::invalid_argument("VT height dimensions invalid");
    for (float f : data)
        if (!std::isfinite(f))
            throw std::invalid_argument("Nonfinite VT height");
    VirtualTextureSource source;
    source.extent = extentFor(std::max(w, h));
    source.heightField = true;
    source.formats = {rhi::Format::RGBA32Float};
    auto limits = std::minmax_element(data.begin(), data.end());
    source.minimum = *limits.first;
    source.maximum = *limits.second;
    // Endpoint-preserving resampling keeps non-square fields and plane normals exact.
    auto owner = std::make_shared<std::vector<float>>(std::move(data));
    const uint32_t extent = source.extent;
    source.readPage = [owner, w, h, extent](uint32_t mip, uint32_t x, uint32_t y) {
        uint32_t n = extent >> mip;
        std::vector<float> rgba(size_t(GpuVirtualTexture::Pitch) * GpuVirtualTexture::Pitch * 4);
        for (uint32_t iy = 0; iy < GpuVirtualTexture::Pitch; iy++)
            for (uint32_t ix = 0; ix < GpuVirtualTexture::Pitch; ix++) {
                float u = float(int(x * 64 + ix) - 2) / float(n - 1),
                      v = float(int(y * 64 + iy) - 2) / float(n - 1);
                rgba[(size_t(iy) * GpuVirtualTexture::Pitch + ix) * 4] = heightAt(*owner, w, h, u, v);
            }
        VirtualTextureSource::Page out(1);
        out[0].resize(rgba.size() * 4);
        std::memcpy(out[0].data(), rgba.data(), out[0].size());
        return out;
    };
    return source;
}
VirtualTextureSource rawHeightVirtualSource(const std::string &path, uint32_t width, uint32_t height) {
    VirtualTextureSource source;
    source.extent = extentFor(std::max(width, height));
    source.heightField = true;
    source.formats = {rhi::Format::RGBA32Float};
    if (width < 2 || height < 2)
        throw std::invalid_argument("Raw VT height dimensions invalid");
    auto stream = std::make_shared<std::ifstream>(path, std::ios::binary | std::ios::ate);
    const size_t samples = size_t(width) * height;
    if (!*stream || stream->tellg() != std::streamoff(samples * 4))
        throw std::invalid_argument("Raw VT height file size mismatch: " + path);
    source.minimum = std::numeric_limits<float>::max();
    source.maximum = std::numeric_limits<float>::lowest();
    stream->seekg(0);
    std::vector<float> chunk(16384);
    for (size_t offset = 0; offset < samples;) {
        size_t count = std::min(chunk.size(), samples - offset);
        stream->read(reinterpret_cast<char *>(chunk.data()), count * 4);
        if (!*stream)
            throw std::runtime_error("Raw VT height scan failed");
        for (size_t i = 0; i < count; i++) {
            if (!std::isfinite(chunk[i]))
                throw std::invalid_argument("Raw VT height contains nonfinite samples");
            source.minimum = std::min(source.minimum, chunk[i]);
            source.maximum = std::max(source.maximum, chunk[i]);
        }
        offset += count;
    }
    source.readPage = [stream, width, height, extent = source.extent](uint32_t mip, uint32_t tileX,
                                                                      uint32_t tileY) {
        constexpr uint32_t Tile = GpuVirtualTexture::Tile, Border = GpuVirtualTexture::Border,
                           Pitch = GpuVirtualTexture::Pitch;
        uint32_t n = extent >> mip;
        std::array<uint32_t, GpuVirtualTexture::Pitch> x0{}, x1{};
        std::array<float, GpuVirtualTexture::Pitch> wx{};
        for (uint32_t x = 0; x < Pitch; x++) {
            float p =
                glm::clamp(float(int(tileX * Tile + x) - int(Border)) / float(n - 1), 0.f, 1.f) * (width - 1);
            x0[x] = uint32_t(p);
            x1[x] = std::min(x0[x] + 1, width - 1);
            wx[x] = p - x0[x];
        }
        uint32_t begin = x0.front(), count = x1.back() - begin + 1;
        std::map<uint32_t, std::vector<float>> rows;
        std::vector<float> values(size_t(Pitch) * Pitch * 4);
        auto readRow = [&](uint32_t row) -> const std::vector<float> & {
            auto found = rows.find(row);
            if (found != rows.end())
                return found->second;
            auto &data = rows[row];
            data.resize(count);
            stream->clear();
            stream->seekg(std::streamoff((size_t(row) * width + begin) * 4));
            stream->read(reinterpret_cast<char *>(data.data()), count * 4);
            if (!*stream)
                throw std::runtime_error("Raw VT height tile read failed");
            return data;
        };
        for (uint32_t y = 0; y < Pitch; y++) {
            float p = glm::clamp(float(int(tileY * Tile + y) - int(Border)) / float(n - 1), 0.f, 1.f) *
                      (height - 1);
            uint32_t y0 = uint32_t(p), y1 = std::min(y0 + 1, height - 1);
            float weight = p - y0;
            const auto &a = readRow(y0);
            const auto &b = readRow(y1);
            for (uint32_t x = 0; x < Pitch; x++)
                values[(size_t(y) * Pitch + x) * 4] =
                    glm::mix(glm::mix(a[x0[x] - begin], a[x1[x] - begin], wx[x]),
                             glm::mix(b[x0[x] - begin], b[x1[x] - begin], wx[x]), weight);
            for (auto it = rows.begin(); it != rows.end();)
                if (it->first != y1)
                    it = rows.erase(it);
                else
                    ++it;
        }
        VirtualTextureSource::Page page(1);
        page[0].resize(values.size() * 4);
        std::memcpy(page[0].data(), values.data(), page[0].size());
        return page;
    };
    return source;
}
VirtualTextureSource materialVirtualSource(const std::array<ImageRGBA8, 5> &images) {
    VirtualTextureSource source;
    uint32_t extent = 64;
    for (const auto &image : images) {
        if (image.pixels.empty() && !image.width && !image.height)
            continue;
        if (!image.width || !image.height || image.pixels.size() != uint64_t(image.width) * image.height * 4)
            throw std::invalid_argument("VT material dimensions invalid");
        extent = std::max(extent, extentFor(std::max(image.width, image.height)));
    }
    source.extent = extent;
    source.formats.assign(5, rhi::Format::RGBA8UNorm);
    using Chains = std::array<std::vector<ImageRGBA8>, 5>;
    auto chains = std::make_shared<Chains>();
    const uint8_t defaults[5][4] = {{255, 255, 255, 255},
                                    {128, 128, 255, 255},
                                    {255, 255, 255, 255},
                                    {255, 255, 255, 255},
                                    {255, 255, 255, 255}};
    for (uint32_t layer = 0; layer < 5; layer++) {
        auto image = images[layer];
        if (image.pixels.empty())
            image = {1, 1, std::vector<uint8_t>(defaults[layer], defaults[layer] + 4)};
        (*chains)[layer].push_back(image);
        while (image.width > 1 || image.height > 1) {
            ImageRGBA8 next{std::max(1u, (image.width + 1) / 2), std::max(1u, (image.height + 1) / 2), {}};
            next.pixels.resize(size_t(next.width) * next.height * 4);
            for (uint32_t y = 0; y < next.height; y++)
                for (uint32_t x = 0; x < next.width; x++) {
                    glm::vec4 sum(0);
                    for (uint32_t iy = 0; iy < 2; iy++)
                        for (uint32_t ix = 0; ix < 2; ix++) {
                            size_t at = (size_t(std::min(y * 2 + iy, image.height - 1)) * image.width +
                                         std::min(x * 2 + ix, image.width - 1)) *
                                        4;
                            for (uint32_t c = 0; c < 4; c++) {
                                float f = image.pixels[at + c] / 255.f;
                                sum[c] += (layer == 0 && c < 3) ? std::pow(f, 2.2f) : f;
                            }
                        }
                    sum *= .25f;
                    if (layer == 1) {
                        glm::vec3 normal = glm::vec3(sum) * 2.f - 1.f;
                        if (glm::dot(normal, normal) > 1e-8f)
                            sum = glm::vec4(glm::normalize(normal) * .5f + .5f, sum.w);
                    }
                    for (uint32_t c = 0; c < 4; c++) {
                        float f = (layer == 0 && c < 3) ? std::pow(sum[c], 1.f / 2.2f) : sum[c];
                        next.pixels[(size_t(y) * next.width + x) * 4 + c] =
                            uint8_t(std::round(glm::clamp(f, 0.f, 1.f) * 255));
                    }
                }
            (*chains)[layer].push_back(next);
            image = std::move(next);
        }
    }
    source.readPage = [chains, extent](uint32_t mip, uint32_t x, uint32_t y) {
        VirtualTextureSource::Page pages(5);
        const uint32_t n = extent >> mip;
        for (uint32_t layer = 0; layer < 5; layer++) {
            const auto &chain = (*chains)[layer];
            const uint32_t original = std::max(chain[0].width, chain[0].height);
            uint32_t level = 0;
            while (level + 1 < chain.size() && (original >> (level + 1)) >= n)
                ++level;
            const auto &image = chain[level];
            auto &page = pages[layer];
            page.resize(size_t(GpuVirtualTexture::Pitch) * GpuVirtualTexture::Pitch * 4);
            for (uint32_t iy = 0; iy < GpuVirtualTexture::Pitch; iy++)
                for (uint32_t ix = 0; ix < GpuVirtualTexture::Pitch; ix++) {
                    float u = (float(int(x * 64 + ix) - 2) + .5f) / n,
                          v = (float(int(y * 64 + iy) - 2) + .5f) / n;
                    float px = glm::clamp(u * image.width - .5f, 0.f, float(image.width - 1)),
                          py = glm::clamp(v * image.height - .5f, 0.f, float(image.height - 1));
                    uint32_t xx = uint32_t(px), yy = uint32_t(py);
                    for (uint32_t c = 0; c < 4; c++) {
                        auto at = [&](uint32_t a, uint32_t b) {
                            return float(image.pixels[(size_t(b) * image.width + a) * 4 + c]);
                        };
                        float f =
                            glm::mix(glm::mix(at(xx, yy), at(std::min(xx + 1, image.width - 1), yy), px - xx),
                                     glm::mix(at(xx, std::min(yy + 1, image.height - 1)),
                                              at(std::min(xx + 1, image.width - 1),
                                                 std::min(yy + 1, image.height - 1)),
                                              px - xx),
                                     py - yy);
                        page[(size_t(iy) * GpuVirtualTexture::Pitch + ix) * 4 + c] = uint8_t(std::round(f));
                    }
                }
        }
        return pages;
    };
    return source;
}
VirtualTextureSource packedVirtualSource(const std::string &manifest) {
    std::ifstream input(manifest);
    if (!input)
        throw std::invalid_argument("Cannot open VT manifest: " + manifest);
    nlohmann::json j;
    input >> j;
    VirtualTextureSource source;
    source.extent = j.at("extent");
    source.heightField = j.at("heightField");
    source.minimum = j.value("minimum", 0.f);
    source.maximum = j.value("maximum", 0.f);
    if (j.at("version") != 1 || j.at("tile") != 64 || j.at("border") != 2 ||
        extentFor(source.extent) != source.extent || !std::isfinite(source.minimum) ||
        !std::isfinite(source.maximum) || source.minimum > source.maximum)
        throw std::invalid_argument("Invalid VT pack metadata");
    auto formats = j.at("formats").get<std::vector<std::string>>();
    for (const auto &f : formats)
        source.formats.push_back(f == "rgba8"     ? rhi::Format::RGBA8UNorm
                                 : f == "rgba32f" ? rhi::Format::RGBA32Float
                                                  : throw std::invalid_argument("VT pack plane format"));
    if ((source.heightField && source.formats != std::vector<rhi::Format>{rhi::Format::RGBA32Float}) ||
        (!source.heightField && source.formats != std::vector<rhi::Format>(5, rhi::Format::RGBA8UNorm)))
        throw std::invalid_argument("VT pack plane count mismatch");
    const auto file =
        (std::filesystem::path(manifest).parent_path() / j.at("data").get<std::string>()).string();
    auto stream = std::make_shared<std::ifstream>(file, std::ios::binary);
    if (!*stream)
        throw std::invalid_argument("Cannot open VT tiles: " + file);
    size_t pageBytes = 0, totalPages = 0;
    for (auto f : source.formats)
        pageBytes += size_t(GpuVirtualTexture::Pitch) * GpuVirtualTexture::Pitch * pixelBytes(f);
    for (uint32_t n = source.extent / 64; n; n /= 2)
        totalPages += size_t(n) * n;
    stream->seekg(0, std::ios::end);
    if (stream->tellg() != std::streamoff(totalPages * pageBytes))
        throw std::invalid_argument("Truncated or oversized VT pack");
    source.readPage = [stream, extent = source.extent, formats = source.formats,
                       pageBytes](uint32_t mip, uint32_t x, uint32_t y) {
        uint32_t n = extent / 64;
        size_t before = 0;
        for (uint32_t l = 0; l < mip; l++) {
            before += size_t(n) * n;
            n /= 2;
        }
        if (!n || x >= n || y >= n)
            throw std::invalid_argument("VT pack page outside source");
        stream->clear();
        stream->seekg(std::streamoff((before + size_t(y) * n + x) * pageBytes));
        VirtualTextureSource::Page pages;
        for (auto f : formats) {
            pages.emplace_back(size_t(GpuVirtualTexture::Pitch) * GpuVirtualTexture::Pitch * pixelBytes(f));
            stream->read(reinterpret_cast<char *>(pages.back().data()), pages.back().size());
            if (!*stream)
                throw std::runtime_error("VT tile read failed");
        }
        return pages;
    };
    return source;
}
GpuVirtualTexture::GpuVirtualTexture(std::shared_ptr<rhi::GraphicsDevice> d, VirtualTextureSource source,
                                     uint32_t columns)
    : source_(std::move(source)), resources_(d), columns_(columns) {
    if (extentFor(source_.extent) != source_.extent || columns < 2 || columns > 16 || !source_.readPage || !source_.ioMutex ||
        source_.formats.empty() || source_.formats.size() > 5 || !std::isfinite(source_.minimum) ||
        !std::isfinite(source_.maximum) || source_.minimum > source_.maximum)
        throw std::invalid_argument("Invalid VT configuration");
    tableWidth_ = source_.extent / Tile;
    maxMip_ = 0;
    for (uint32_t n = tableWidth_; n > 1; n /= 2)
        ++maxMip_;
    tableRows_ = row(maxMip_) + 1;
    tableData_.resize(size_t(tableWidth_) * (tableRows_ + 2) * 4);
    for (auto f : source_.formats) {
        pixelBytes(f);
        auto t = resources_.texture(
            {columns * Pitch, columns * Pitch, f,
             rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDestination | rhi::TextureUsage::CopySource,
             "VT physical tile atlas"});
        atlases_.push_back(t);
        atlasViews_.push_back(resources_.view(t));
    }
    table_ = resources_.texture(
        {tableWidth_, tableRows_ + 2, rhi::Format::RGBA32Float,
         rhi::TextureUsage::Sampled | rhi::TextureUsage::CopyDestination | rhi::TextureUsage::CopySource,
         "VT mip page table"});
    tableView_ = resources_.view(table_);
    sampler_ = resources_.sampler({rhi::Filter::Linear, rhi::AddressMode::ClampToEdge});
    float *meta = tableData_.data() + size_t(tableRows_) * tableWidth_ * 4;
    meta[0] = float(source_.extent);
    meta[1] = Tile;
    meta[2] = float(columns);
    meta[3] = Border;
    meta += tableWidth_ * 4;
    meta[0] = float(maxMip_);
    meta[1] = float(source_.heightField);
    install({maxMip_, 0, 0}, 0, std::move(source_.rootPage));
    resident_[{maxMip_, 0, 0}] = {0, ++clock_};
    uploadTable();
}
uint32_t GpuVirtualTexture::row(uint32_t mip) const {
    uint32_t offset = 0;
    for (uint32_t l = 0; l < mip; l++)
        offset += tableWidth_ >> l;
    return offset;
}
size_t GpuVirtualTexture::physicalBytes() const {
    size_t bytes = size_t(tableWidth_) * (tableRows_ + 2) * 16;
    for (auto f : source_.formats)
        bytes += size_t(columns_ * Pitch) * (columns_ * Pitch) * pixelBytes(f);
    return bytes;
}
void GpuVirtualTexture::install(PageId id, uint32_t slot, VirtualTextureSource::Page pages) {
    if (pages.empty()) {
        std::lock_guard<std::mutex> lock(*source_.ioMutex);
        pages = source_.readPage(id.mip, id.x, id.y);
    }
    if (pages.size() != atlases_.size())
        throw std::invalid_argument("VT source returned wrong plane count");
    for (size_t l = 0; l < pages.size(); l++) {
        if (pages[l].size() != size_t(Pitch) * Pitch * pixelBytes(source_.formats[l]))
            throw std::invalid_argument("VT source returned wrong tile bytes");
        if (source_.formats[l] == rhi::Format::RGBA32Float)
            for (size_t i = 0; i < pages[l].size(); i += 4) {
                float value;
                std::memcpy(&value, pages[l].data() + i, 4);
                if (!std::isfinite(value))
                    throw std::invalid_argument("VT source returned nonfinite tile");
            }
    }
    for (size_t l = 0; l < pages.size(); l++)
        resources_.device->writeTextureRegion(
            atlases_[l], {(slot % columns_) * Pitch, (slot / columns_) * Pitch, Pitch, Pitch},
            pages[l].data(), pages[l].size());
    float *entry = tableData_.data() + (size_t(row(id.mip) + id.y) * tableWidth_ + id.x) * 4;
    entry[0] = float(slot % columns_);
    entry[1] = float(slot / columns_);
    entry[2] = float(id.mip);
    entry[3] = 1;
    version_++;
}
void GpuVirtualTexture::uploadTable() {
    resources_.device->writeTextureFloat(table_, tableData_.data(), tableData_.size() * 4);
}
void GpuVirtualTexture::update(const std::vector<PageId> &requests, uint32_t uploads) {
    std::set<PageId> requested;
    for (auto id : requests) {
        if (id.mip > maxMip_ || id.x >= (tableWidth_ >> id.mip) || id.y >= (tableWidth_ >> id.mip))
            throw std::invalid_argument("VT request outside page table");
        requested.insert(id);
    }
    if (requested.size() - requested.count({maxMip_, 0, 0}) > capacity() - 1)
        throw std::invalid_argument("VT request set exceeds physical budget");
    ++clock_;
    for (auto id : requested) {
        auto it = resident_.find(id);
        if (it != resident_.end())
            it->second.touched = clock_;
    }
    // Each instance owns its futures; replacement destroys the receiving map,
    // so old jobs can never publish into a new page table. Callbacks capture only
    // CPU source state and serialize shared file streams through ioMutex.
    for(auto it=pending_.begin();it!=pending_.end();) {
        if(!requested.count(it->first) && it->second.wait_for(std::chrono::seconds(0))==std::future_status::ready)it=pending_.erase(it);else ++it;
    }
    uint32_t issued=0;
    for (auto id : requests) {
        if(resident_.count(id))continue;
        if(!uploads)break;
        VirtualTextureSource::Page pages;
        if(asynchronous_) {
            auto found=pending_.find(id);
            if(found==pending_.end()) {
                if(pending_.size()>=16 || issued>=8)continue;
                auto source=source_;source.rootPage.clear();
                auto task=std::make_shared<std::packaged_task<VirtualTextureSource::Page()>>([source=std::move(source),id]{
                    std::lock_guard<std::mutex> lock(*source.ioMutex);return source.readPage(id.mip,id.x,id.y);
                });
                auto future=task->get_future();
                if(!engine::JobSystem::io().tryEnqueue([task]{(*task)();}))continue;
                pending_.emplace(id,std::move(future));++issued;continue;
            }
            if(found->second.wait_for(std::chrono::seconds(0))!=std::future_status::ready)continue;
            try{pages=found->second.get();}catch(...){pending_.erase(found);throw;}
            pending_.erase(found);
        }else {std::lock_guard<std::mutex> lock(*source_.ioMutex);pages=source_.readPage(id.mip,id.x,id.y);}
        uint32_t slot=0;auto victim=resident_.end();
        if(resident_.size()<capacity()){
            std::set<uint32_t> used;for(const auto& page:resident_)used.insert(page.second.slot);
            while(used.count(slot))++slot;
        }else{
            for(auto it=resident_.begin();it!=resident_.end();++it)
                if(it->first.mip!=maxMip_ && !requested.count(it->first) && (victim==resident_.end() || it->second.touched<victim->second.touched))victim=it;
            if(victim==resident_.end())break;slot=victim->second.slot;
        }
        // Validate and upload every plane before publishing the complete mapping.
        install(id,slot,std::move(pages));
        if(victim!=resident_.end()){
            tableData_[(size_t(row(victim->first.mip)+victim->first.y)*tableWidth_+victim->first.x)*4+3]=0;
            resident_.erase(victim);
        }
        resident_[id]={slot,clock_};uploadTable();--uploads;
    }
}
void GpuVirtualTexture::prepare(const glm::mat4 &vp, const glm::mat4 &model, uint32_t width, uint32_t height,
                                bool flipV, uint32_t uploads) {
    if (!width || !height)
        throw std::invalid_argument("VT viewport empty");
    glm::mat4 transform = vp * model;
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 4; r++)
            if (!std::isfinite(transform[c][r]))
                throw std::invalid_argument("VT nonfinite transform");
    std::vector<PageId> requests;
    for (uint32_t bias = 0; bias <= maxMip_; bias++) {
        requests.clear();
        std::function<void(PageId)> visit = [&](PageId page) {
            if (requests.size() >= capacity())
                return; // This bias already exceeds the leaf budget.
            float n = float(tableWidth_ >> page.mip), u0 = page.x / n, u1 = (page.x + 1) / n, v0 = page.y / n,
                  v1 = (page.y + 1) / n;
            if (flipV) {
                v0 = 1 - v0;
                v1 = 1 - v1;
            }
            glm::vec4 clip[8];
            for (int i = 0; i < 8; i++)
                clip[i] = transform * glm::vec4((i & 1 ? u1 : u0) * 2 - 1,
                                                i & 2 ? source_.maximum : source_.minimum,
                                                (i & 4 ? v1 : v0) * 2 - 1, 1);
            for (int plane = 0; plane < 6; plane++) {
                bool outside = true;
                for (auto p : clip) {
                    float distance = plane == 0   ? p.x + p.w
                                     : plane == 1 ? p.w - p.x
                                     : plane == 2 ? p.y + p.w
                                     : plane == 3 ? p.w - p.y
                                     : plane == 4 ? p.z
                                                  : p.w - p.z;
                    if (distance >= 0)
                        outside = false;
                }
                if (outside)
                    return;
            }
            glm::vec2 lo(1e30f), hi(-1e30f);
            bool eyeCross = false;
            for (auto p : clip) {
                if (p.w <= 1e-5f) {
                    eyeCross = true;
                    continue;
                }
                glm::vec2 q = glm::vec2(p) / p.w;
                lo = glm::min(lo, q);
                hi = glm::max(hi, q);
            }
            float pixels =
                eyeCross ? 1e30f : std::max((hi.x - lo.x) * width * .5f, (hi.y - lo.y) * height * .5f);
            if (page.mip > bias && pixels > Tile * 1.5f) {
                for (uint32_t y = 0; y < 2; y++)
                    for (uint32_t x = 0; x < 2; x++)
                        visit({page.mip - 1, page.x * 2 + x, page.y * 2 + y});
            } else if (page.mip != maxMip_)
                requests.push_back(page);
        };
        visit({maxMip_, 0, 0});
        std::set<PageId> withAncestors(requests.begin(), requests.end());
        for (auto page : requests)
            while (page.mip + 1 < maxMip_) {
                page = {page.mip + 1, page.x / 2, page.y / 2};
                withAncestors.insert(page);
            }
        requests.assign(withAncestors.begin(), withAncestors.end());
        if (requests.size() <= capacity() - 1)
            break;
    }
    std::stable_sort(requests.begin(), requests.end(), [](PageId a, PageId b) { return a.mip > b.mip; });
    update(requests, uploads);
}
} // namespace render
