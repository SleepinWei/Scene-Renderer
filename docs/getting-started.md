# 构建与运行

[返回项目展示](../README.md) · [完整效果图集](rendering-gallery.md) · [技术文档索引](README.md)

## 快速运行

以下命令从**仓库根目录**执行。首次运行可直接使用程序生成的 `--demo`、`--classic sky`、`--classic ocean` 或 `--classic terrain`，无需下载大型场景。

### macOS：原生 Metal

需要 macOS、Xcode（含 Metal Toolchain）、CMake、Python 3.9+ 和 Homebrew：

```sh
brew install glfw assimp yaml-cpp glslang spirv-cross
cmake -S . -B build -DSCENERENDERER_RHI_BACKEND=Metal -DSCENERENDERER_LEGACY_METAL=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
./build/Scene-Renderer --demo

# 单独查看自然场景
./build/Scene-Renderer --classic sky
./build/Scene-Renderer --classic clouds
./build/Scene-Renderer --classic ocean
```

### Vulkan

Vulkan 主后端需要 Vulkan SDK、GLFW 3.4、Assimp、yaml-cpp、glslangValidator 与 spirv-cross。macOS 使用 MoltenVK；可通过 `Vulkan_INCLUDE_DIR` / `Vulkan_LIBRARY` 指定 SDK 位置。

```sh
cmake -S . -B build/vulkan -DSCENERENDERER_RHI_BACKEND=Vulkan -DCMAKE_BUILD_TYPE=Release
cmake --build build/vulkan -j 8
./build/vulkan/Scene-Renderer --demo
ctest --test-dir build/vulkan --output-on-failure
```

macOS 上也可以在 Metal 构建中同时编译 Vulkan，再通过启动参数选择：

```sh
cmake -S . -B build -DSCENERENDERER_RHI_BACKEND=Metal -DSCENERENDERER_VULKAN_PROTOTYPE=ON
cmake --build build -j 8
./build/Scene-Renderer --demo --backend Vulkan
```

原生编辑器默认主逻辑／渲染双线程，逻辑使用 60 Hz 固定步长，支持暂停及速度控制；普通帧队列满时跳过快照，主线程继续处理输入和模拟。`--single-thread` 可切换同步对照。`--asset-root DIR` 指定模型及场景资源根目录；JSON 内资源优先相对该文档解析。`--auto-quality` 显式启用配额压力下的效果品质降级。`--forward` 使用完整场景的前向光照；`--frames N` 有界运行，`--time 8` 固定海洋/草时间，`--size 800x450` 与 `--resize 640x360` 用于窗口回归，`--frames-in-flight 1` 可对照默认的 3 个在途提交。`--screenshot path.ppm` 保存包括 UI 的最后一帧。`--render-gallery directory core` 保存无 UI 的经典场景 PNG。本机 Metal 回归启用 API／Shader Validation；Vulkan／MoltenVK 通过 GPU 数值测试验证。本机没有 Khronos validation layer，不能把这些结果视为 Vulkan layer 验证；对 MoltenVK 开启 MetalTools 的已知阻塞组合由 CTest 单独关闭。

### 示例资源

`--demo` 自动生成材质、天空、海洋、地形和草；仓库未包含历史 `asset/` 资源包，配置场景缺失时也会回退到此演示。Cornell 风格场景、Bunny 和 Helmet 可直接运行，Sponza 与 San Miguel 需单独下载；高清海洋和透明浅水场景由程序生成，可直接运行。

```sh
python3 tools/fetch_gi_assets.py
./build/Scene-Renderer --classic sponza
./build/Scene-Renderer --classic san-miguel
```

下载脚本获取上游模型、校验 SHA-256，并选取运行所需的 OBJ、MTL、纹理和原始说明。Sponza 压缩包约 78 MB，San Miguel 约 536 MB（511 MiB）；后者选用上游低面数版本，导入后仍有约 **562 万个三角形**，首次加载需要较长时间和较多内存。大型模型及下载缓存不提交到 Git，截图、下载清单和代码随仓库提供。资源归属和使用条件见[场景资源说明](../samples/README.md)。

## 命令与操作

| 命令 | 用途 |
| --- | --- |
| `--demo` | 自动生成的功能演示，无需历史资产包 |
| `--classic <name>` | 选择 `cornell`、`bunny`、`dragon`、`buddha`、`armadillo`、`helmet`、`sponza`、`san-miguel`、`sibenik`、`sky`、`clouds`、`clouds-sunset`、`clouds-storm`、`cloud-volume`、`cloud-inside`、`cloud-vortex`、`shadow-test`、`ocean`、`ocean-clear`、`coastal-beach`、`coastal-water`、`coastal-underwater`、`coastal-seabed`、`underwater-dive`、`terrain`、`mountain-lake`、`mountain-lake-ground` 或 `mountain-lake-beach` |
| `--post-process-self-test` | 原生后处理 GPU 验证；编辑器 Post processing 面板提供独立开关，[说明与对照](post-processing.md) |
| `--frames <N>` | 窗口渲染 N 帧后退出 |
| `--render-gallery <目录> core` | 离屏生成三个随仓库提供的基础示例 |
| `--render-gallery <目录> gi` | 生成两个 GI 场景、RSM 开关对照及纯间接光／太阳／天空贡献图 |
| `--render-gallery <目录> benchmarks` | 生成 Dragon、Buddha、Armadillo 与 Sibenik，含教堂 RSM 对照 |
| `--render-gallery <目录> <场景名>` | 仅生成指定场景 |
| `--render-gallery <目录>` | 默认生成三个基础示例 |
| `--gpu-resource-budget-mib <N>` | 原生编辑器的 RHI buffer／texture 逻辑负载配额；默认 0 不限额；双线程编辑器超限保留成功画面并重试，无法恢复的冷启动报错；不含 driver heap 等隐式开销 |
| `--auto-quality` | 显式允许配额失败时最多降低三个效果品质等级；默认关闭，关闭后恢复请求规格 |
| `--asset-root <DIR>` | 指定模型／纹理／JSON 资源根目录；JSON 子资源优先相对文档解析 |
| `--single-thread` | 原生编辑器同步对照；默认 Metal／Vulkan 使用独立渲染线程 |
| `--rhi-self-test` | 所选 RHI 后端的 GPU 正确性自检 |
| `--path-trace <场景名>` | CPU 路径追踪，默认 Sponza；使用 `--pt-size`、`--pt-samples`、`--pt-bounces` 等设置输出 |
| `--pt-guiding` / `--pt-cache` | GPU 方向训练与可选有偏的漫反射延续缓存 |
| `--pt-denoise` / `--pt-denoise-device` / `--pt-denoise-input` | OIDN 最终图降噪、设备选择与已有 PFM 离线处理 |
| `--pt-bdpt` / `--pt-no-glass` | CPU BDPT 面积光参考与 `caustics` 无玻璃对照 |
| `--path-trace-gpu <场景名>` | Metal/Vulkan GPU 路径追踪，共用 `--pt-*` 输出参数 |
| `--pt-sampler sobol/pcg` / `--pt-fixed` | CPU 采样器对照与完整固定 spp；GPU 使用 Sobol |
| `--pt-environment <HDR>` / `--pt-no-sky` | 复用环境贴图及太阳 sidecar，或跳过实时天空烘焙 |
| `--pt-sun-radius <DEG>` | 调整烘焙天空的太阳角半径，0 < DEG < 5.73；保持辐照度，扩大太阳用于软光预览 |
| `--pt-sss-roughness <R>` / `--pt-water-roughness <R>` | 玉龙／捕获水面的介电粗糙度，范围 [0,1]，默认分别 0.22／0；小于 0.02 使用平滑 delta 边界 |
| `--pt-ocean-floor-depth <METERS>` | 设置 `dragon-jade-ocean` 海床深度，默认 20 m；2.2 m 恢复旧浅水设置 |
| `--pt-self-test` | Metal／Vulkan 天空桥和 GPU PT 求交、采样与积分自检 |

`W/A/S/D` 移动，`E/Q` 上下移动，按住 `Shift` 加速；按住鼠标右键调整视角。ImGui 用于修改渲染选项和场景参数。经典场景和离屏画廊支持 Metal/Vulkan；同时编译两后端时加 `--backend Vulkan`。`--rhi-self-test` 同时支持 OpenGL 基础路径。历史 `--metal-self-test` 仅在显式启用 `SCENERENDERER_LEGACY_METAL` 时提供。

### 复现基础图集

下载 GI 资源后，使用原生 Metal 构建生成基础图集：

```sh
python3 tools/fetch_gi_assets.py
for scene in core gi sky ocean ocean-clear terrain; do
    MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 \
        ./build/Scene-Renderer --render-gallery img/metal "$scene"
done
```

`core` 输出 Bunny、Helmet、Cornell 及 Cornell 的 RSM 关闭图；`gi` 输出 Sponza／San Miguel 的 RSM 开关和三种间接光图；`sky` 输出五种太阳高度／视角；`cloud-gallery` 输出三种体积云及无云／不透明度对照；海洋命令同时输出短波、散射或透射对照。文件统一写入 `img/metal/`。Vulkan 构建可使用相同画廊命令，另选输出目录，并省略 Metal 验证环境变量。

### 其他效果图与资源

| 画廊选择 | 输出与准备 |
| --- | --- |
| `benchmarks` | Dragon、Buddha、Armadillo、Sibenik；先执行 `python3 tools/fetch_benchmark_assets.py` |
| `cloud-gallery` | 远景云层、日落、阴天及无云／不透明度对照；无需下载 |
| `cloud-volume-gallery` | 三维体素云、云内、风暴与密度／距离／光照切片；无需下载 |
| `post-gallery` | 同一 Cornell 场景的默认显示、Bloom、景深、调色与组合后处理；[说明](post-processing.md) |
| `ocean` / `ocean-clear` | 大浪／浅水及功能开关对照；无需下载 |
| `underwater-dive` | 6 m 深海底潜水，礁石、沙沟、海草、悬浮颗粒与蓝绿色能见度；[说明与对照](water-underwater-diving.md) |
| `coastal-underwater` | 海岸水下视角，水中距离雾、双向折射与全反射；[说明与对照](water-underwater-rendering.md) |
| `coastal-beach` / `coastal-water` | 同一程序化海岸的岸侧／水侧视角，启用可选折射、局部多次散射、浅水波、泡沫与湿沙；[说明与动画](water-coastal-features.md) |
| `diagnostics` | 地形 VT、CSM／PCSS 原始诊断缓冲；[可视化步骤](render-diagnostics-gallery.md) |
| `mountain-lake` / `mountain-lake-ground` / `mountain-lake-beach` | 山湖、草地、沙滩；先按 [资源说明](mountain-lake.md) 下载并转换 |

```sh
./build/Scene-Renderer --classic cloud-volume
./build/Scene-Renderer --classic cloud-inside
./build/Scene-Renderer --classic cloud-vortex
./build/Scene-Renderer --render-gallery build/gallery cloud-volume-gallery
./build/Scene-Renderer --render-gallery build/gallery ocean
```

路径追踪、BDPT、OIDN 和水体／玉石的启动参数见 [完整效果图集](rendering-gallery.md#路径追踪) 与 [专题索引](README.md#渲染与效果)。大型资源的下载条件和使用许可见 [场景资源说明](../samples/README.md)。

## 构建、验证与限制

基础 CTest 无需大型 GI 模型。下载模型后可另外运行新 RHI 画廊：

```sh
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ctest --test-dir build --output-on-failure
ctest --test-dir build/vulkan --output-on-failure
./build/Scene-Renderer --render-gallery build/rhi/gallery-metal gi
./build/vulkan/Scene-Renderer --render-gallery build/rhi/gallery-vulkan gi
./build/Scene-Renderer --demo --frames 3
```

GPU 验证覆盖上传/异步读回、延迟释放、MRT、前向/延迟 PBR、SSS 深度、透明排序、三类阴影、SSAO、太阳/天空 RSM、大气 LUT、GPU 体积云间接队列／球壳／历史／遮挡、完整海洋 IFFT、地形/草的 VT 区域上传、页淘汰与回退、流式高度、网格预算和闭合接缝、计算细分及 TSAA。CPU 数值参考和限定的像素比较用于检查结果；编辑器测试同时覆盖真实 resize、UI 与窗口呈现。画廊提供实际模型和贴图的视觉回归，不以历史截图作为物理参考图像。

2026-10-05 体积云最终完整回归在 Apple M4/macOS 验收：Metal **17/17**、Vulkan/MoltenVK **18/18**；云间接队列、零工作清理、深度遮挡、球壳观察位置、风速／投影历史及 resize 均进入实际 GPU 验证。带云编辑器在 Metal API／Shader Validation 下通过双线程与单线程各 8 帧运行，详见 [体积云验收](gpu-driven-clouds.md)。

2026-10-03 CPU Path Tracing 在 Apple M4/macOS 验收：包含新增 PT 回归的完整 CTest 为 Metal **14/14**、Vulkan/MoltenVK **15/15**；CPU 测试通过 AddressSanitizer 和 UndefinedBehaviorSanitizer。覆盖 BVH 与暴力求交对照、材质／alpha／法线贴图、环境 PDF、GGX 数值积分、MIS、遮挡与发光面、确定性多线程、渐进累加和输出格式；另验证 Cornell 场景入口及设备线程上的天空烘焙。两个大型场景均输出 256 spp 图像，非有限样本为 0；完整记录见 [CPU Path Tracing 说明](path-tracing-cpu.md)。

2026-10-04 Engine 后续回归在 Apple M4/macOS 验收：Metal **11/11**、Vulkan/MoltenVK **12/12**、OpenGL **8/8**。覆盖线程归属／封存移交、CPU/GPU 联合加载与失败回退、增量 mesh／图片上传、磁盘管线缓存、纹理子资源隔离、transient 复用、真实深度 VT feedback、高度 morph／草附着，以及冷启动和已有画面下的自动降级。CPU 并发与 RHI graphics 契约分别通过 ThreadSanitizer；隔离本轮提交的全量 Metal 构建及完整 CTest **14/14**（含既有 PT 测试）通过。完整应用未在 TSan 下验收。经典大型场景持续运行、命令、预算及统计口径见 [本轮实施与验收](engine-runtime-completion.md)。

2026-10-03 天空修复在 Apple M4/macOS 验收：Metal **8/8**、Vulkan/MoltenVK **9/9**，包含太阳角半径／能量、地平线及几何遮挡、控制同步、观察高度与极限参数。OpenGL 4.1 的历史 RHI 验收为 7/7，本轮未重复运行。Metal 开启 API/Shader Validation；本机没有 Khronos validation layer，Windows/Linux 与 OpenGL 4.3+ 尚未实机验收。Metal/Vulkan 使用单队列、最多三帧并行提交；单次吞吐测量和算法边界见 [RHI 重构计划](rhi-refactor-plan.md)。大规模 OBJ 导入仍需较多 CPU 内存与启动时间。

`Cloud` 已通过新 RHI 实现 GPU Driven 体积云；地面云阴影与环境光照调制尚未实现。自动曝光尚未实现；CPU/GPU 路径追踪支持冻结的静态／程序化几何、基础 PBR 与均匀介质；历史资产缺失也限制了原场景的视觉回归。Sponza 和 San Miguel 的实时图采用 RSM 一次反弹近似，CPU 路径追踪图采用有最大深度限制的多次反弹；两条路径的近似与尚未支持的效果见各自说明。

旧 OpenGL 后端可使用独立目录构建：

```sh
cmake -S . -B build/opengl -DSCENERENDERER_RHI_BACKEND=OpenGL -DCMAKE_BUILD_TYPE=Release
cmake --build build/opengl -j 8
```

依赖包括 GLFW、Assimp、yaml-cpp，以及随仓库提供的 GLM、ImGui、stb、tinygltf 和 glad 等。Metal 额外需要 Xcode Metal Toolchain、glslang、SPIRV-Cross；OpenGL 构建需要平台 OpenGL 库。
