# Scene Renderer

**English** · [简体中文](README.zh-CN.md)

A **C++17 graphics renderer** for learning and experimentation, originating from a computer graphics course project at Tongji University. Classic benchmark scenes and natural environments demonstrate real-time lighting, materials, GPU compute, and CPU/GPU path tracing.

Real-time rendering uses a shared **RHI** with native **Metal and Vulkan** backends. The editor provides camera navigation and interactive effect controls. Features include PBR, CSM/PCSS, RSM, GTAO/SSAO, atmospheric scattering, volumetric clouds, FFT oceans, height/material virtual textures, vegetation, and TSAA.

[Build and run](docs/getting-started.md) · [Full gallery and settings](docs/rendering-gallery.md) · [System design](docs/system-design.md) · [Technical docs and change records](docs/README.md)

Detailed guides are currently available in Chinese.

![Sponza courtyard rendered with native Metal](img/metal/sponza.png)

## Scenes and effects

All images below were rendered by this project. Real-time images use native Metal; path-traced images identify the integrator and denoising used. Reproduction commands, sampling settings, feature comparisons, and intermediate buffers are documented in the [full gallery](docs/rendering-gallery.md).

### Classic scenes and PBR

Sponza, San Miguel, and Sibenik showcase architectural materials, shadows, and sun/sky RSM indirect lighting. Stanford scans and Damaged Helmet demonstrate metallic, nonmetallic, and textured materials. [Ambient occlusion comparisons](docs/ambient-occlusion.md) show horizon-based AO and edge-aware filtering against the original SSAO.

| San Miguel courtyard | Sibenik Cathedral |
| --- | --- |
| ![Metal San Miguel](img/metal/san-miguel.png) | ![Metal Sibenik Cathedral](img/metal/sibenik.png) |

| Stanford Bunny: three materials | Damaged Helmet: PBR textures |
| --- | --- |
| ![Metal Stanford Bunny](img/metal/bunny.png) | ![Metal Damaged Helmet](img/metal/helmet.png) |

### Atmosphere and sun

Atmospheric scattering and an analytic sun disk share lighting parameters with the scene, indirect lighting, and ocean surface. Distant cloud layers support weather distributions and wind changes.

| Daytime sky | Sunset at the horizon | Sunset clouds |
| --- | --- | --- |
| ![Metal daytime sky](img/metal/sky-day.png) | ![Metal sunset sun](img/metal/sky-sunset.png) | ![Metal sunset volumetric clouds](img/metal/clouds-sunset.png) |

### Immersive 3D voxel clouds

A 128³ XYZ density field combines conservative distance fields, GPU indirect ray marching, and cached sunlight to support close views and cameras inside the cloud. These images use full-resolution 1080p rendering with cloud temporal accumulation disabled. The storm preset includes internal volumetric flashes.

| 3D cloud volume | Storm shape and internal flashes |
| --- | --- |
| ![Metal 3D voxel clouds](img/metal/cloud-volume.png) | ![Metal 3D storm clouds](img/metal/cloud-vortex.png) |

### FFT oceans and transparent water

Primary and short-wave FFT spectra combine to produce wave crests, fine ripples, and foam. Refraction, RGB absorption, and approximate single scattering produce shallow-water transmission and translucent wave crests.

| Rough ocean | Shallow-water refraction and scattering |
| --- | --- |
| ![Metal FFT ocean](img/metal/ocean.png) | ![Metal transparent water](img/metal/ocean-clear.png) |

### Large terrain, lakes, and vegetation

Mountain Lake demonstrates an 8×8 km landscape with height/material VT paging, terrain LOD, an FFT lake surface, grass density that varies with distance, and wet-sand and beach PBR materials.

![Metal Mountain Lake landscape](img/metal/mountain-lake.png)

| Lakeside grass | Beach and wet sand |
| --- | --- |
| ![Metal lakeside vegetation](img/metal/mountain-lake-ground.png) | ![Metal lakeside beach](img/metal/mountain-lake-beach.png) |

### Virtual textures and soft shadows

Height/material VT uses physical page caches, page tables, ancestor fallback, and GPU depth feedback. Five-level CSM allocates shadow precision across viewing distances; PCSS estimates penumbra growth with blocker separation.

| Actual GPU VT cache | CSM shadows and cascade regions |
| --- | --- |
| ![VT height and material atlases](img/diagnostics/vt-cache.png) | ![CSM cascades and transition bands](img/diagnostics/csm-cascades.png) |

![PCF, default-sun PCSS, and larger-light PCSS comparison](img/diagnostics/pcss-comparison.png)

The comparison shows PCF, PCSS with the default sun, and PCSS with a larger light, respectively. The third column illustrates a wider penumbra. See [page tables, shadow atlases, and full diagnostics](docs/render-diagnostics-gallery.md).

### Path tracing, caustics, and subsurface scattering

CPU/Metal/Vulkan path tracing supports multiple bounces, textured materials, refraction, and random walks in homogeneous media. CPU BDPT renders smooth-glass caustics. Procedural terrain, vegetation, and FFT water can be frozen into offline scenes. The images below use Open Image Denoise (OIDN).

The architectural and natural scenes shown above also have path-traced results. Sponza and San Miguel use Metal PT at 320×240, 64 spp, and 16 bounces; terrain, lake, and ocean captures use Metal PT at 640×480 with procedural animation frozen at 8 seconds. Sampling settings and raw images are available in the [path-tracing gallery](docs/rendering-gallery.md#路径追踪).

| Sponza: Metal PT + OIDN, 64 spp | San Miguel: Metal PT + OIDN, 64 spp |
| --- | --- |
| ![Path-traced Sponza courtyard](img/path-tracing/oidn-sponza.png) | ![Path-traced San Miguel courtyard](img/path-tracing/oidn-san-miguel.png) |

| Stanford Dragon: CPU BDPT glass caustics + OIDN | Jade Dragon: Metal PT subsurface scattering + OIDN |
| --- | --- |
| ![Glass Stanford Dragon and caustics](img/path-tracing/dragon-glass.png) | ![Polished jade dragon](img/path-tracing/jade-polished-boundary.png) |

| Terrain and grass: Metal PT + OIDN, 128 spp | Mountain Lake: Metal PT + OIDN, 128 spp |
| --- | --- |
| ![Path-traced terrain and grass](img/path-tracing/pt-terrain.png) | ![Path-traced mountain lake and reflections](img/path-tracing/pt-mountain-lake.png) |

| Lakeside beach: Metal PT + OIDN, 256 spp | Shallow-water refraction: Metal PT + OIDN, 256 spp |
| --- | --- |
| ![Path-traced lakeside beach and wet sand](img/path-tracing/pt-mountain-lake-beach.png) | ![Path-traced FFT shallow water](img/path-tracing/pt-ocean-clear.png) |

![Path-traced FFT rough ocean: Metal PT + OIDN, 256 spp](img/path-tracing/pt-ocean.png)

## System overview

The main logic thread owns the world, input, and editor, publishing immutable scene snapshots to a dedicated render thread. The render thread prepares GPU resources and schedules effects; the RHI unifies resources, pipelines, commands, and presentation. Metal and Vulkan reuse shaders generated from shared GLSL. See [system design](docs/system-design.md) for the frame flow, resource management, and platform boundaries.

## Documentation

| Guide | Contents |
| --- | --- |
| [Build and run](docs/getting-started.md) | Dependencies, backend selection, scene downloads, launch options, controls, and capture reproduction |
| [Full rendering gallery](docs/rendering-gallery.md) | Effect settings, comparisons, intermediate buffers, measurements, and algorithm limitations |
| [System design](docs/system-design.md) | Logic/render separation, frame flow, RHI, shader builds, and module structure |
| [Technical documentation index](docs/README.md) | Implementation notes, change records, validation results, and follow-up plans |
| [Asset attribution and licenses](samples/README.md) | Model and texture sources, authors, and usage conditions |

This project is intended for learning and experimentation. Images, measurements, and feature coverage correspond to the configurations recorded in the documentation. See [course contributors and historical images](docs/archive/historical-gallery.md).
