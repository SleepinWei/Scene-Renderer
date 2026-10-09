# Scene Renderer

**English** · [简体中文](README.zh-CN.md)

A **C++17 graphics renderer** for learning and experimentation, originating from a computer graphics course project at Tongji University. Classic benchmark scenes and natural environments demonstrate real-time lighting, materials, GPU compute, and CPU/GPU path tracing.

Real-time rendering uses a shared **RHI** with native **Metal and Vulkan** backends. The editor provides camera navigation and interactive effect controls. Features include PBR, CSM/PCSS, RSM, GTAO/SSAO, atmospheric scattering, volumetric clouds, FFT oceans, height/material virtual textures, vegetation, and TSAA.

[Build and run](docs/getting-started.md) · [Full gallery and settings](docs/rendering-gallery.md) · [System design](docs/system-design.md) · [Technical docs and change records](docs/README.md)

Detailed guides are currently available in Chinese.

![Sponza courtyard rendered with native Metal](img/metal/sponza.png)

## Scenes and effects

Images below were rendered by this project unless explicitly labeled as Blender Cycles references. Real-time images use native Metal; path-traced images identify the integrator and denoising used. Reproduction commands, sampling settings, feature comparisons, and intermediate buffers are documented in the [full gallery](docs/rendering-gallery.md).

### Classic scenes and PBR

Sponza, San Miguel, and Sibenik showcase architectural materials, shadows, and sun/sky RSM indirect lighting. Stanford scans and Damaged Helmet demonstrate metallic, nonmetallic, and textured materials.

| San Miguel courtyard | Sibenik Cathedral |
| --- | --- |
| ![Metal San Miguel](img/metal/san-miguel.png) | ![Metal Sibenik Cathedral](img/metal/sibenik.png) |

| Stanford Bunny: three materials | Damaged Helmet: PBR textures |
| --- | --- |
| ![Metal Stanford Bunny](img/metal/bunny.png) | ![Metal Damaged Helmet](img/metal/helmet.png) |

### Ambient occlusion

Horizon-based AO with geometric normal reconstruction and edge-aware filtering reduces sampling bands around pillars and arches. These Sponza captures disable RSM and TSAA to isolate AO, with matching lighting and exposure. See [algorithm notes and more comparisons](docs/ambient-occlusion.md).

| Original 24-sample SSAO: AO buffer | Horizon AO + edge-aware filtering: AO buffer |
| --- | --- |
| ![Sponza original SSAO visibility](img/ao/sponza-ao-legacy-visibility.png) | ![Sponza horizon AO with edge-aware filtering](img/ao/sponza-ao-gtao-visibility.png) |

| AO disabled: final scene | AO enabled: final scene |
| --- | --- |
| ![Sponza with AO disabled](img/ao/sponza-ao-off.png) | ![Sponza with horizon AO enabled](img/ao/sponza-ao-gtao.png) |

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

Real-time water now uses a grid concentrated near the camera, an independent underwater color/position layer, and single-scattering integration along the water path. [Before/after, validation and GPU cost](docs/water-realtime-upgrade.md).

The coastal presets add optional DDA refraction with terrain fallback, local multiple scattering, shallow waves, persistent foam, and wet sand. Each feature has an editor switch; existing presets keep the new effects disabled. [Controls, comparisons, animations and limitations](docs/water-coastal-features.md). [First performance round: stage timings, submission gaps and boundary fixes](docs/water-coastal-performance.md).

![Metal coastal shallow waves and foam](img/metal/coastal-beach.png)

Underwater views add distance-dependent absorption and scattering, water-to-air refraction, and total internal reflection. Open `--classic coastal-underwater`; the Ocean panel exposes view, fog and **Short wave ripples** switches. The underwater preset uses a 0.5–2 m FFT wave band so nearby refraction and the Snell-window boundary move with the waves. [Images, optical paths and validation](docs/water-underwater-rendering.md).

| Underwater surface | Looking down at the seabed |
| --- | --- |
| ![Metal underwater surface](img/diagnostics/water/ripple-demo/coastal-underwater.png) | ![Metal underwater seabed](img/diagnostics/water/seabed-demo/coastal-underwater-seabed.png) |

The seabed demo adds 25 cm sand ripples, fine normal detail, and optional solar caustics projected through the live FFT surface. Open `--classic coastal-seabed`; toggle **FFT seabed caustics** and adjust **Caustic strength** in the Ocean panel. [Seabed comparisons, implementation and GPU timings](docs/water-seabed-caustics.md).

### Underwater diving, sun shafts, and refraction

The `underwater-dive` preset starts 6 m underwater, looking along a sandy channel between reef outcrops and seagrass. Distance-dependent absorption and single scattering produce blue-green visibility; drifting sediment, 16/48/128 m cascaded caustics on terrain and submerged meshes, and wave-focused sun shafts add depth and moving light. The preset uses 32 samples for shadowed volume lighting.

| Diving view: sun shafts and seabed caustics | Looking up: refracted sky, sun, and floating marker |
| --- | --- |
| ![Metal underwater diving scene with sun shafts](img/diagnostics/water/air-refraction/underwater-dive.png) | ![Underwater refraction of the sky and surface marker](img/diagnostics/water/air-refraction/underwater-dive-snell-window.png) |

```sh
./build/Scene-Renderer --classic underwater-dive --size 1280x720
```

Hold the right mouse button to look around; use **W/A/S/D** to move and **Q/E** to move vertically. Set **Camera → View pitch** to about **68°** to inspect the sky window. Near-horizontal underwater views show total internal reflection; the window boundary follows the wave normals.

The Ocean panel independently controls **Underwater distance fog**, **Underwater sun shafts**, **FFT seabed caustics**, and **Wide air refraction**, with adjustable shaft contrast, particle density, and volume samples. Wide air refraction adds an upward capture to recover surface objects outside the original camera view; a unit scattering gain preserves sky and object contrast. Metal and Vulkan water validation passes. Wide capture remains a screen-space approximation, and this preset prioritizes visual quality over a 60 fps target.

[Dive scene and controls](docs/water-underwater-diving.md) · [Caustic coverage](docs/water-caustic-cascades.md) · [Sun-shaft implementation](docs/water-sun-shafts.md) · [Refraction comparisons, validation, and performance](docs/water-air-refraction.md)

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

### Post processing

A shared native HDR-to-display module adds optional Bloom, depth of field, camera motion blur, color grading, FXAA, sharpening, vignette, chromatic aberration, and film grain. The **Post processing** panel offers independent controls, exponential / ACES fitted / Reinhard / linear tone mapping, and a soft cinematic preset; extra effects default to off. [Controls, pipeline, comparisons, and limitations](docs/post-processing.md).

| Default display | ACES and combined post effects |
| --- | --- |
| ![Cornell default display](img/diagnostics/post-processing/cornell-post-off.png) | ![Cornell combined post processing](img/diagnostics/post-processing/cornell-post-combined.png) |

| HDR Bloom | Depth of field: foreground focus |
| --- | --- |
| ![Cornell HDR Bloom](img/diagnostics/post-processing/cornell-bloom.png) | ![Cornell depth of field](img/diagnostics/post-processing/cornell-dof.png) |

```sh
./build/Scene-Renderer --classic cornell --size 1280x720
```

Open **Post processing** to enable individual effects or **Soft cinematic preset**. Metal and Vulkan validation passes; depth of field uses opaque depth, and motion blur currently covers camera motion.

### Path tracing, caustics, and subsurface scattering

CPU/Metal/Vulkan path tracing supports multiple bounces, textured materials, refraction, and random walks in homogeneous media. CPU BDPT renders smooth-glass caustics. Procedural terrain, vegetation, and FFT water can be frozen into offline scenes. Unless marked as raw photon mapping, the images below use Open Image Denoise (OIDN).

This hair showcase uses circular-fiber scattering under a clear sky captured from the realtime atmosphere pipeline. The low-angle sky and finite sun are rotated together, their source illumination is scaled by 10, and narrower hair highlights reveal the warm solar reflections. Each asset retains **50,000 strands: Metal PT, 512×512, 1024 spp, 12 bounces, OIDN display**. The head remains neutral diffuse. Hair geometry: [Cem Yuksel](https://www.cemyuksel.com/research/hairmodels/); head courtesy of **Murat Afshar**. See [lighting, material settings, raw images and validation](docs/path-tracing-hair-sunlight.md).

| Straight hair in stronger sunlight | Wavy hair in stronger sunlight |
|---|---|
| ![Sunlit straight hair](img/path-tracing/yuksel-straight-sunlit.png) | ![Sunlit wavy hair](img/path-tracing/yuksel-wavy-sunlit.png) |

The architectural and natural scenes shown above also have path-traced results. Sponza and San Miguel use Metal PT at 320×240, 64 spp, and 16 bounces; terrain, lake, and ocean captures use Metal PT at 640×480 with procedural animation frozen at 8 seconds. Sampling settings and raw images are available in the [path-tracing gallery](docs/rendering-gallery.md#路径追踪).

| Sponza: Metal PT + OIDN, 64 spp | San Miguel: Metal PT + OIDN, 64 spp |
| --- | --- |
| ![Path-traced Sponza courtyard](img/path-tracing/oidn-sponza.png) | ![Path-traced San Miguel courtyard](img/path-tracing/oidn-san-miguel.png) |

| Stanford Dragon: CPU BDPT glass caustics + OIDN | Jade Dragon: Metal PT subsurface scattering + OIDN |
| --- | --- |
| ![Glass Stanford Dragon and caustics](img/path-tracing/dragon-glass.png) | ![Polished jade dragon](img/path-tracing/jade-polished-boundary.png) |

| Sunlit pool floor: Metal photon mapping + OIDN | Swimming pool above water: Metal photon mapping + OIDN |
| --- | --- |
| ![Sunlit pool floor caustics](img/path-tracing/pool-sunlit-underwater.png) | ![Sunlit swimming pool caustics](img/path-tracing/pool-sunlit.png) |

Short ripples and a 0.266° sun radius produce clear caustic lines on pale blue tiles: 4M light paths, 64 spp, radius 0.025. The water uses a frozen analytic ripple mesh. [Raw closeup](img/path-tracing/pool-sunlit-underwater-raw.png) · [Flat-water control](img/path-tracing/pool-sunlit-flat.png) · [Reproduction and validation](docs/path-tracing-photon-mapping.md#晴天泳池更明显的网状亮纹).

| Pool: Metal photon mapping, 1M light paths + 32 spp, raw | Refractive caustic contribution, raw |
| --- | --- |
| ![Pool floor photon caustics](img/path-tracing/pool-photon.png) | ![Pool refractive caustics AOV](img/path-tracing/pool-photon-caustics.png) |

The pool uses a frozen analytic wave mesh. The fixed-radius photon map is biased; CPU/Metal/Vulkan share the same photon data. [Reproduction, validation, and PT acceleration](docs/path-tracing-photon-mapping.md).

| Terrain and grass: Metal PT + OIDN, 128 spp | Mountain Lake: Metal PT + OIDN, 128 spp |
| --- | --- |
| ![Path-traced terrain and grass](img/path-tracing/pt-terrain.png) | ![Path-traced mountain lake and reflections](img/path-tracing/pt-mountain-lake.png) |

| Lakeside beach: Metal PT + OIDN, 1024 spp | Shallow-water refraction: Metal PT + OIDN, 4096 spp |
| --- | --- |
| ![Path-traced lakeside beach and wet sand](img/path-tracing/pt-mountain-lake-beach.png) | ![Path-traced FFT shallow water](img/path-tracing/pt-ocean-clear.png) |

Beach textures are sampled at world scale; underwater solar paths use a BSDF/phase mixture proposal without changing the sun or exposure. [Raw beach](img/path-tracing/pt-mountain-lake-beach-raw.png) · [Raw shallow water](img/path-tracing/pt-ocean-clear-raw.png) · [Fix and validation](docs/path-tracing-procedural.md#水下太阳路径采样).

![Path-traced FFT rough ocean: Metal PT + OIDN, 256 spp](img/path-tracing/pt-ocean.png)

### Blender test scenes

Blender's official Classroom and Barcelona Pavilion scenes are imported offline, rendered with this project's Metal path tracer, and denoised with OIDN. See [assets, material conversion, and validation](docs/blender-path-tracing.md).

| Classroom | Barcelona Pavilion |
| --- | --- |
| ![Classroom Metal PT + OIDN](img/path-tracing/blender-classroom-materials.png) | ![Barcelona Pavilion pool: Metal PT + OIDN](img/path-tracing/blender-barcelona-water.png) |

Barcelona now imports Bump/Normal Map atlases and dielectric roughness textures, with bounded pool reflection, refraction, and RGB absorption. Hidden particle emitters and reflected texture UVs are fixed. The pool uses an explicit artistic profile (IOR 1.333, depth 0.5 world units); the original water parameters remain available. [Raw preview](img/path-tracing/blender-barcelona-water-raw.png) · [Reproduction and limits](docs/blender-path-tracing.md#barcelona-材质与池水) · [Validation](img/path-tracing/blender-water-validation.json).

CPU, Metal and Vulkan use **shared geometry BLAS/TLAS**. The latest Barcelona export retains all **20,622 vegetation particles**, representing roughly **55 million instance triangles**. Revision 5 adds Blender corner MikkTSpace frames and separately baked thin-leaf diffuse reflection/transmission. Its CPU geometry/acceleration payload is **54.4 MiB**. This 640×360, 256 spp Metal preview uses OIDN. [Raw image](img/path-tracing/blender-barcelona-thin-raw.png) · [Material validation and reproduction](docs/blender-path-tracing.md#薄玻璃太阳反射链).

![Barcelona full vegetation and thin-leaf transmission: Metal PT + OIDN](img/path-tracing/blender-barcelona-thin.png)

Blender Cycles source-scene GT uses 1024 spp, the original materials and full vegetation, with denoising, intensity clamping, and glossy filtering disabled. The older engine previews use a reduced particle budget; the full-instance preview above still uses a different pool/material profile; see [the comparison and linear reference](docs/blender-path-tracing.md#blender-cycles-gt).

![Barcelona source-scene GT: Blender Cycles, 1024 spp](img/path-tracing/blender-barcelona-cycles-gt.png)

A separate **matched Cycles comparison** reconstructs the same frozen geometry, 512 particles, camera, textures, lights, GGX closures, thin glass and bounded pool. At 320×180, 4096 spp and depth 24, raw Metal/Cycles total RGB energy differs by **0.067%**; pixel differences still contain reflection noise. Original Blender shader graphs and normal maps are outside this controlled baseline. [Measurements, independent-seed noise and reproduction](docs/blender-path-tracing.md#同参数-cycles-线性对照).

Per-lobe normals and bump shadowing reduce small-scene raw Metal/Cycles RGB L1 from **0.838% to 0.290%** for Lambert/leaves and **2.662% to 0.582%** for GGX/leaves (192×96, 4096 spp). Solar proposals reduce full-instance Barcelona L1 from **4.67% → 3.53% → 2.98%** (water, then thin-sheet reflection; 160×90 / 512 spp, seed 1). The thin-glass outlier drops from **29.06 to 0.0656** (Cycles **0.0462**), with RGB RMSE **0.156 → 0.049**. Seed 2 L1 rises slightly from **3.07% to 3.19%**; pool/leaf rare paths and original layered materials remain. Cached scene counts give **1.58×** in a separate single-thread trace microbenchmark. [Latest validation and limits](img/path-tracing/blender-thin-validation.json).

![Full Barcelona with normal maps: Metal raw, Cycles raw, linear absolute error](img/path-tracing/blender-barcelona-thin-matched.png)

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
