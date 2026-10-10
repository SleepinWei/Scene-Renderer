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

The current development capture strengthens atmospheric aerial perspective while reducing the fixed-colour ground fog and ground cloud shadows. Ground shadow strength is **30%**, aerial optical distance scale is **2**, and mountain shadow coverage is **8 km**. Mountain/cloud solar occlusion modulates scattered sunlight, including a local sky correction. The view below is **90.0° perpendicular to the sun direction**, with the sun at about **20°** elevation; side lighting produces broad air-shadow bands and softer shafts. **Metal, 960×540, fixed simulation time, RSM off.** [Controls, validation, capture provenance and limitations](docs/mountain-lake-side-light.md).

![Mountain Lake: 90-degree sun side lighting and weaker ground fog](img/diagnostics/environment/aerial-side/mountain-lake-side-combined.png)

<details>
<summary>Aerial sunlight: occlusion off / on / on with weaker ground fog</summary>

![Matched 90-degree views with atmospheric sunlight occlusion off, on, and on with weaker ground fog](img/diagnostics/environment/aerial-side/comparison.png)

The camera, sun and exposure stay fixed across all three views. The centre view adds atmospheric solar occlusion; the right view also adds the weaker height fog. Narrow shafts, off-screen terrain casters and moving views still need further work; this capture is not a 60 fps claim.

</details>

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

Refined fur and cloth examples use the captured realtime sunset sky with clouds and a 5° finite sun. Stanford Bunny now has **72,000 finer short fur strands**; the cream knit sweater has **1,280 explicit front yarn loops and 23,360 fine nap strands**, with a separate cloth-yarn near view. **Metal PT, 768×768, 1024 spp, 12 bounces, OIDN display.** [Lighting, material refinements, raw images and validation](docs/path-tracing-grooms-sunlight.md#夕阳展示). Bunny data: [Stanford University Computer Graphics Laboratory](https://graphics.stanford.edu/data/3Dscanrep/).

| Stanford Bunny fur at sunset | Knit sweater at sunset | Cloth yarn near view |
| --- | --- | --- |
| ![Bunny fur at sunset](img/path-tracing/bunny-fur-sunset.png) | ![Knit sweater at sunset](img/path-tracing/knit-sweater-sunset.png) | ![Cloth yarn at sunset](img/path-tracing/knit-yarn-sunset.png) |

The [YarnSim knitted-cloth examples](docs/path-tracing-knitted-yarn.md) import public relaxed yarn control points, connect periodic spline spans, and add **anisotropic dielectric yarn reflection** shared by CPU/Metal/Vulkan. The prototype sweater has **280 honeycomb repeats on its front**; the separate swatch has **three geometric plies and open stitch holes**. **Metal PT, 768×768, 1024 spp, 12 bounces, OIDN display, sunset lighting.** Sleeves and garment seams remain procedural. Data: [Leaf et al., 2018](https://graphics.stanford.edu/projects/yarnsim/); original assets are not redistributed.

| Honeycomb sweater front | Three-ply knitted cloth |
| --- | --- |
| ![Honeycomb sweater at sunset](img/path-tracing/knitted-sweater-sunset.png) | ![Three-ply honeycomb knit at sunset](img/path-tracing/knitted-honeycomb-sunset.png) |

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

### Ocean sea states and real-time/PT alignment

Recent development captures cover calibrated JONSWAP/TMA wind waves, independently directed swell, shared water optics, and successive glint/reflection corrections. Raster and PT share the FFT fields and detail band; a camera-focused PT grid extends water coverage to 2 km.

| Update | Result and validation |
| --- | --- |
| [Sea state and shared optics](docs/ocean-sea-state-and-pt.md) | Calibrated wave height/period, independent swell, IOR 1.333, exact Fresnel and finite-sun reflection. [Shared-surface comparisons](docs/ocean-physical-surface.md). |
| [Glint spatial filtering and TAA](docs/ocean-glint-antialiasing.md) | Subpixel solar integration, slope-moment mips and linear HDR water history. Near single-frame error improves against raster SSAA; distant/TAA errors remain. |
| [Native float FFT normals and reconstruction](docs/ocean-float-normals-and-glints.md) | Independent wind/ripple periods on CPU/GPU PT, tent glint reconstruction and direct frozen-water accumulation. Tent smooths edges at extra cost and increases pixel L1 against raw PT. |
| [Distant sun and sky reflection integration](docs/ocean-reflection-integration.md) | Exact reflection Jacobian in slope space and pixel integration of Fresnel times sky radiance; additive body/reflection/sun/foam/border diagnostics. Metal/Vulkan water regression passes. |

![Wind waves and independent swell: Metal native RT + OIDN, 512x320, 256 spp](img/path-tracing/ocean-sea-state.png)

| Current real-time TAA | Matched Metal native RT + OIDN, 512 spp |
| --- | --- |
| ![Current realtime ocean](img/path-tracing/ocean-reflection-after-raster-taa.png) | ![Matched path-traced ocean](img/path-tracing/ocean-reflection-pt-denoised.png) |

Same snapshot, camera, sea state, time, 640×360 resolution and image exporter. Numerical comparisons use raw PT before OIDN. The latest before/after ablation uses one binary and identical compiled shaders; both raw PT films are bitwise identical. These are development-build results, with capture commands, source/build hashes and validation logs in the linked records.

| Latest TAA comparison against raw 512 spp PT | Previous reflection model | Current reflection model |
| --- | ---: | ---: |
| All-water relative RGB L1 | 129.66% | 113.49% |
| Distant-water relative RGB L1 | 171.88% | 134.01% |
| Side-water relative RGB L1 | 54.73% | 43.56% |

Near glints are largely unchanged. Water self-reflection, non-Gaussian distant slopes, rough sky reflection and volume transport remain different. One synchronized 20-frame capture measures about 3 ms extra single-frame cost; it is not a stable FPS benchmark. [Full images, component statistics, costs and limits](docs/ocean-reflection-integration.md) · [Latest validation record](img/path-tracing/ocean-reflection-validation.json) · [Raw PT](img/path-tracing/ocean-reflection-pt.png).

<details>
<summary>Earlier glint reconstruction and latest sky-reflection components</summary>

| Earlier box reconstruction | Earlier tent reconstruction |
| --- | --- |
| ![Box glints, historical capture](img/path-tracing/ocean-alignment-box-raster-taa.png) | ![Tent glints, historical capture](img/path-tracing/ocean-alignment-tent-raster-taa.png) |

These historical float-normal captures use the same PT input. Tent lowers the glint ROI neighbour gradient by about 12.5%, but increases raw-PT pixel L1 and costs about 5–8 ms. Gradient reduction alone does not establish alias-free rendering. [Reconstruction ablation](docs/ocean-float-normals-and-glints.md).

| Centre-normal sky reflection | Pixel-integrated sky reflection |
| --- | --- |
| ![Previous sky reflection contribution](img/path-tracing/ocean-reflection-before-reflection.png) | ![Current sky reflection contribution](img/path-tracing/ocean-reflection-after-reflection.png) |

The sky-reflection images are weighted additive radiance components, not complete renders. Earlier 8-bit-normal PT references and raster SSAA are kept in their stage records; their errors are not compared across reference changes. [Initial comparison and spectrum-cache benchmark](docs/ocean-realtime-pt-comparison.md).

</details>

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
