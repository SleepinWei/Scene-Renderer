#include "renderer/rhi/GpuVirtualTexture.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <set>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>
namespace render {
namespace {
void check(bool pass, const char *reason) {
    if (!pass)
        throw std::runtime_error(reason);
}
} // namespace
void validateVirtualTextureRhi(std::shared_ptr<rhi::GraphicsDevice> device, const std::string &directory) {
    Resources resources(device);
    {
        auto source=heightVirtualSource(128,128,std::vector<float>(128*128,.25f));
        source.rootPage=source.readPage(1,0,0);auto read=source.readPage;
        std::promise<void> release;auto gate=release.get_future().share();
        auto started=std::make_shared<std::promise<void>>();auto ready=started->get_future();
        source.readPage=[read,gate,started](uint32_t mip,uint32_t x,uint32_t y){started->set_value();gate.wait();return read(mip,x,y);};
        GpuVirtualTexture texture(device,source,2);texture.enableAsync();
        texture.update({{0,0,0}},1);
        check(texture.pendingPages()==1 && texture.residentPages()==1,"Async VT published a pending page");
        check(ready.wait_for(std::chrono::seconds(2))==std::future_status::ready,"Async VT read was not scheduled");
        texture.update({{0,0,0}},1);check(texture.pendingPages()==1 && texture.residentPages()==1,"Async VT blocked or duplicated a pending read");
        release.set_value();const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(texture.residentPages()!=2 && std::chrono::steady_clock::now()<deadline){texture.update({{0,0,0}},1);std::this_thread::yield();}
        check(texture.residentPages()==2 && texture.pendingPages()==0,"Completed VT page was not uploaded and published");
    }
    for (auto format : {rhi::Format::RGBA8UNorm, rhi::Format::RGBA32Float}) {
        auto texture = resources.texture({8, 8, format,
                                          rhi::TextureUsage::CopyDestination | rhi::TextureUsage::CopySource,
                                          "VT region regression"});
        if (format == rhi::Format::RGBA8UNorm) {
            std::vector<uint8_t> initial(8 * 8 * 4, 23), patch(3 * 2 * 4, 181);
            device->writeTexture(texture, initial.data(), initial.size());
            device->writeTextureRegion(texture, {2, 3, 3, 2}, patch.data(), patch.size());
            auto pixels = device->readTexture(texture);
            for (uint32_t y = 0; y < 8; y++)
                for (uint32_t x = 0; x < 8; x++)
                    check(pixels[(y * 8 + x) * 4] == (x >= 2 && x < 5 && y >= 3 && y < 5 ? 181 : 23),
                          "VT partial RGBA8 upload damaged untouched pixels");
        } else {
            std::vector<float> initial(8 * 8 * 4, .25f), patch(3 * 2 * 4, .75f);
            device->writeTextureFloat(texture, initial.data(), initial.size() * 4);
            device->writeTextureRegion(texture, {2, 3, 3, 2}, patch.data(), patch.size() * 4);
            auto pixels = device->readTextureFloat(texture);
            for (uint32_t y = 0; y < 8; y++)
                for (uint32_t x = 0; x < 8; x++)
                    check(std::abs(pixels[(y * 8 + x) * 4] -
                                   (x >= 2 && x < 5 && y >= 3 && y < 5 ? .75f : .25f)) < 1e-7f,
                          "VT partial float upload damaged untouched pixels");
        }
        bool rejected = false;
        uint8_t bytes[16] = {};
        try {
            device->writeTextureRegion(texture, {7, 0, 2, 1}, bytes, 8);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        check(rejected, "VT region overflow accepted");
    }
    std::vector<float> heights(512 * 257);
    for (uint32_t y = 0; y < 257; y++)
        for (uint32_t x = 0; x < 512; x++)
            heights[size_t(y) * 512 + x] = .3f + .4f * x / 511.f - .2f * y / 256.f;
    auto source = heightVirtualSource(512, 257, heights);
    GpuVirtualTexture vt(device, source, 4);
    check(vt.residentPages() == 1 && vt.maxMip() == 3, "VT root initialization incorrect");
    std::vector<GpuVirtualTexture::PageId> first, second;
    for (uint32_t y = 0; y < 3; y++)
        for (uint32_t x = 0; x < 5; x++) {
            first.push_back({0, x, y});
            second.push_back({0, x + 3, y + 3});
        }
    vt.update(first, 3);
    check(vt.residentPages() == 4, "VT frame upload budget ignored");
    vt.update(first, 15);
    check(vt.residentPages() == 16, "VT residency budget ignored");
    vt.update(second, 15);
    auto table = device->readTextureFloat(vt.tableTexture());
    std::set<uint32_t> slots;
    uint32_t valid = 0;
    for (uint32_t mip = 0, row = 0, n = 8; mip < 4; mip++, row += n, n /= 2) {
        for (uint32_t y = 0; y < n; y++)
            for (uint32_t x = 0; x < n; x++) {
                const float *e = table.data() + ((row + y) * 8 + x) * 4;
                if (e[3] < .5f)
                    continue;
                uint32_t slot = uint32_t(e[1]) * 4 + uint32_t(e[0]);
                check(slots.insert(slot).second, "VT evicted page aliases a reused physical slot");
                ++valid;
                if (mip == 3)
                    check(slot == 0, "VT pinned fallback evicted");
            }
    }
    check(valid == 16, "VT page table disagrees with resident cache");
    check(table[3] == 0, "VT stale table entry survived eviction");
    auto atlas = device->readTextureFloat(vt.atlasTexture());
    for (auto id : second) {
        const float *e = table.data() + (id.y * 8 + id.x) * 4;
        uint32_t sx = uint32_t(e[0]) * 68, sy = uint32_t(e[1]) * 68;
        for (uint32_t y = 0; y < 68; y++)
            for (uint32_t x = 0; x < 68; x++) {
                float u = glm::clamp(float(int(id.x * 64 + x) - 2) / 511.f, 0.f, 1.f),
                      v = glm::clamp(float(int(id.y * 64 + y) - 2) / 511.f, 0.f, 1.f);
                check(std::abs(atlas[((sy + y) * 272 + sx + x) * 4] - (.3f + .4f * u - .2f * v)) < 2e-6f,
                      "VT apron or non-square height resampling incorrect");
            }
    }
    {
        using namespace rhi;
        auto path = directory + "/vt-validation.comp";
        ComputePipelineDesc pipeline;
        pipeline.shader = {path + ".glsl", path + ".metallib", path + ".spv", path + ".json", "main0"};
        pipeline.threads = {16, 1, 1};
        pipeline.bindings = {{0,
                              {{0, BindingType::StorageWrite, ShaderStage::Compute, "VtResults", 16},
                               {1, BindingType::SampledTexture, ShaderStage::Compute, "atlas", 0},
                               {2, BindingType::SampledTexture, ShaderStage::Compute, "pageTable", 0},
                               {3, BindingType::StorageRead, ShaderStage::Compute, "VtSamples",16}}}};
        auto kernel = resources.computePipeline(pipeline);
        auto output = resources.buffer(
            {16 * 16, BufferUsage::Storage | BufferUsage::CopySource, "VT GPU sample reference"});
        auto sample=[&](GpuVirtualTexture& surface,const std::array<glm::vec4,16>& points){
            Resources batch(device);auto input=batch.buffer({sizeof(points),BufferUsage::Storage,"VT query coordinates"},points.data());
            auto bindings=batch.bindings({pipeline.bindings[0],
                {{0,output,0,16*16,{},{}},{1,{},0,0,surface.atlas(),surface.sampler()},
                 {2,{},0,0,surface.pageTable(),surface.sampler()},{3,input,0,sizeof(points),{},{}}}});
            auto commands=device->createCommandList();commands.dispatch(kernel,{bindings},{1,1,1});device->submit(commands);
            std::array<float,64> values;device->readBuffer(output,0,sizeof(values),values.data());return values;
        };
        std::array<glm::vec4,16> queries;for(uint32_t i=0;i<16;++i)queries[i]={i/15.f,((i*7)%16)/15.f,0,0};
        auto values=sample(vt,queries);
        for (uint32_t i = 0; i < 16; i++) {
            float u = i / 15.f, v = ((i * 7) % 16) / 15.f;
            if (std::abs(values[i * 4] - (.3f + .4f * u - .2f * v)) >= 2e-6f)
                std::cerr << "VT sample " << i << " got " << values[i * 4] << " expected "
                          << (.3f + .4f * u - .2f * v) << " error "
                          << std::abs(values[i * 4] - (.3f + .4f * u - .2f * v)) << "\n";
            check(std::abs(values[i * 4] - (.3f + .4f * u - .2f * v)) < 2e-6f,
                  "VT GPU sampler failed resident / missing page / domain edge lookup");
        }
    }
    // Deliberately different fine and root data expose a residency seam that
    // affine-plane tests cannot detect. Test both axes, diagonal corners and eviction.
    {
        using namespace rhi;auto path=directory+"/vt-validation.comp";
        ComputePipelineDesc p;p.shader={path+".glsl",path+".metallib",path+".spv",path+".json","main0"};p.threads={16,1,1};
        p.bindings={{0,{{0,BindingType::StorageWrite,ShaderStage::Compute,"VtResults",16},
            {1,BindingType::SampledTexture,ShaderStage::Compute,"atlas",0},{2,BindingType::SampledTexture,ShaderStage::Compute,"pageTable",0},
            {3,BindingType::StorageRead,ShaderStage::Compute,"VtSamples",16}}}};
        auto kernel=resources.computePipeline(p);
        for(bool height:{true,false}){
            VirtualTextureSource source;source.extent=256;source.heightField=height;source.minimum=.2f;source.maximum=.8f;
            source.formats={height?Format::RGBA32Float:Format::RGBA8UNorm};
            source.readPage=[height](uint32_t mip,uint32_t,uint32_t){
                VirtualTextureSource::Page page(1);float value=mip==0?.8f:.2f;
                if(height){std::vector<float> pixels(68*68*4,value);page[0].resize(pixels.size()*4);std::memcpy(page[0].data(),pixels.data(),page[0].size());}
                else page[0]=std::vector<uint8_t>(68*68*4,uint8_t(std::round(value*255)));return page;
            };
            GpuVirtualTexture seam(device,source,2);seam.update({{0,0,0},{0,1,0},{0,0,1}},3);
            const glm::vec2 positions[16]={{64-.001f,32},{64+.001f,32},{100,64-.001f},{100,64+.001f},
                {64-.001f,64-.001f},{64+.001f,64+.001f},{64-.001f,60},{64+.001f,60},
                {32,32},{100,100},{56,100},{64,64},{0,0},{255,255},{100,56},{100,72}};
            std::array<glm::vec4,16> queries;for(int i=0;i<16;++i)queries[i]={
                (positions[i]+glm::vec2(height?0:.5f))/(height?255.f:256.f),0,0};
            auto input=resources.buffer({sizeof(queries),BufferUsage::Storage,"VT seam queries"},queries.data());
            auto output=resources.buffer({256,BufferUsage::Storage|BufferUsage::CopySource,"VT seam results"});
            auto bindings=resources.bindings({p.bindings[0],{{0,output,0,256,{},{}},{1,{},0,0,seam.atlas(),seam.sampler()},
                {2,{},0,0,seam.pageTable(),seam.sampler()},{3,input,0,sizeof(queries),{},{}}}});
            auto sample=[&]{auto commands=device->createCommandList();commands.dispatch(kernel,{bindings},{1,1,1});device->submit(commands);
                std::array<float,64> values;device->readBuffer(output,0,sizeof(values),values.data());return values;};
            auto values=sample();for(int pair=0;pair<4;++pair)check(std::abs(values[pair*8]-values[pair*8+4])<.001f,"VT residency edge/corner is discontinuous");
            check(std::abs(values[8*4]-.8f)<.001f && std::abs(values[9*4]-.2f)<.001f,"VT seam filter erased interior detail or parent fallback");
            check(std::abs(values[14*4]-.8f)<.001f && std::abs(values[15*4]-.2f)<.001f,"VT transition did not remain inside its boundary band");
            seam.update({{0,2,2},{0,3,2},{0,2,3}},3);values=sample();
            for(int i=0;i<8;++i)check(std::abs(values[i*4]-.2f)<.001f,"VT eviction left stale seam samples");
        }
        std::cout<<"VT GPU height/material residency edges, diagonal corners, interior detail and post-eviction continuity passed\n";
    }
    // Small disk pack exercises the same source path without retaining a CPU field.
    auto base = std::filesystem::temp_directory_path() / ("scene-renderer-vt-validation-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(base);
    auto page = heightVirtualSource(2, 2, {0, 1, 0, 1}).readPage(0, 0, 0);
    {
        std::ofstream out(base / "tiles.bin", std::ios::binary);
        out.write(reinterpret_cast<const char *>(page[0].data()), page[0].size());
        std::ofstream manifest(base / "height.json");
        manifest
            << R"({"version":1,"extent":64,"tile":64,"border":2,"heightField":true,"minimum":0,"maximum":1,"formats":["rgba32f"],"data":"tiles.bin"})";
    }
    {
        std::ofstream raw(base / "height.raw", std::ios::binary);
        raw.write(reinterpret_cast<const char *>(heights.data()), heights.size() * 4);
    }
    auto raw = rawHeightVirtualSource((base / "height.raw").string(), 512, 257);
    for (auto id : {GpuVirtualTexture::PageId{0, 0, 0}, {0, 7, 7}, {3, 0, 0}})
        check(raw.readPage(id.mip, id.x, id.y) == source.readPage(id.mip, id.x, id.y),
              "Raw height tile streaming differs from retained CPU height");
    auto packed = packedVirtualSource((base / "height.json").string());
    check(packed.readPage(0, 0, 0) == page, "VT disk tile pack decode differs from memory source");
    std::filesystem::remove_all(base);
    std::cout << "VT: partial uploads, bounded residency, eviction, pinned fallback, aprons, non-square "
                 "heights raw file streaming and disk pack validated\n";
}
} // namespace render
