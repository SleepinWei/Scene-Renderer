# CPU Path Tracing：物体与大型场景

后续采样与 GPU 路径见 [CPU 采样优化与 Metal/Vulkan GPU Path Tracing](path-tracing-gpu.md)。当前默认 Sobol/VNDF 和自适应采样；下方首次 CPU 图像与耗时属于原 PCG/NDF 固定采样记录。

## 首次 CPU 结果（2026-10-03）

Sponza 和 San Miguel 首次已完成真实网格、纹理与灯光转换，并输出 320×240 / 8 spp 预览及 640×480 / 256 spp 静态图。积分在 CPU 上执行；天空由实时 RHI 大气烘焙成线性 HDR 环境贴图，保存后可完全在 CPU 上复用。完整生成文件位于忽略的 `build/path-tracing/`；两张最终 PNG 同步到 `img/path-tracing/`，用于 README 展示。

| 场景 | mesh | 输入三角形 | 有效三角形 | BVH 节点 | 几何/BVH 驻留估算 |
| --- | --- | --- | --- | --- | --- |
| Sponza | 25 | 262,267 | 262,266 | 87,159 | 16.63 MiB |
| San Miguel | 281 | 5,617,451 | 5,602,728 | 1,997,605 | 414.01 MiB |

差额为过滤的退化三角形，未主动简化模型。驻留估算包含变换后的顶点、索引、primitive/order 和 BVH 容量，不包含贴图、构建临时空间、导入资产、输出图及引擎的其他资源；它不是进程 RSS。

## 入口与复现

下方命令保留首次运行的参数，显式选择 PCG 和固定 spp；当前版本仍使用新的 VNDF 与深度边界修复，结果可能与首次图像不同。需要先准备 GI 模型：`python3 tools/fetch_gi_assets.py`。编译沿用项目 CMake 配置，本轮使用独立 Metal 构建目录，以避免与编辑器构建争用。

```sh
cmake -S . -B build/pt -DCMAKE_BUILD_TYPE=Release -DSCENERENDERER_RHI_BACKEND=Metal
cmake --build build/pt -j 8

# 初次烘焙实时天空，输出 8 spp 预览。
./build/pt/Scene-Renderer --path-trace sponza --pt-size 320x240 --pt-samples 8 --pt-bounces 6 --pt-threads 6 --pt-sampler pcg --pt-fixed --pt-output build/path-tracing/sponza-preview
./build/pt/Scene-Renderer --path-trace san-miguel --pt-size 320x240 --pt-samples 8 --pt-bounces 6 --pt-threads 6 --pt-sampler pcg --pt-fixed --pt-output build/path-tracing/san-miguel-preview

# 复用 HDR 天空和太阳 sidecar；此步骤不创建 GPU/context。
./build/pt/Scene-Renderer --path-trace sponza --pt-environment build/path-tracing/sponza-preview-environment.hdr --pt-size 640x480 --pt-samples 256 --pt-bounces 8 --pt-threads 8 --pt-exposure 3 --pt-sampler pcg --pt-fixed --pt-output build/path-tracing/sponza
./build/pt/Scene-Renderer --path-trace san-miguel --pt-environment build/path-tracing/san-miguel-preview-environment.hdr --pt-size 640x480 --pt-samples 256 --pt-bounces 8 --pt-threads 8 --pt-exposure 3 --pt-sampler pcg --pt-fixed --pt-output build/path-tracing/san-miguel

# 物体/灯光入口无需 GPU，便于检查小场景。
./build/pt/Scene-Renderer --path-trace cornell --pt-no-sky --pt-size 128x128 --pt-samples 16 --pt-bounces 6
ctest --test-dir build/pt -R '^pt-' --output-on-failure
```

`--path-trace` 默认 Sponza，支持经典场景工厂中的网格物体。默认 640×480、64 spp、8 次反弹、seed 1；线程数默认硬件并发减一，按 tile 数限制。`--pt-exposure` 默认场景相机曝光，本轮最终图显式设为 3，使室内阴影细节更便于查看。`--pt-seed`、`--pt-threads`、`--pt-output` 均可指定；未知参数和非法尺寸/采样/深度直接报错。

使用 Vulkan 主后端或同时编译两后端时，天空烘焙可由 Vulkan 执行；双后端构建加 `--backend Vulkan`。macOS OpenGL 4.1 不具备烘焙所需的 compute，使用保存的 HDR 或 `--pt-no-sky` 跑 CPU 路径。

每次保存以下输出：

- `<prefix>.png`：曝光映射及 gamma 2.2 后的图像。
- `<prefix>.pfm`：未曝光、未 gamma 的线性 RGB float32，PFM 从底行开始，记录本机字节序。
- `<prefix>-albedo.png`、`<prefix>-normal.png`：像素中心首个表面的诊断图，不是降噪后的 beauty。
- `<prefix>.json`：场景、分辨率、采样、反弹深度、seed、曝光、线程参数、有效几何、射线数、时间与非有限样本数。
- `<prefix>-environment.hdr`：512×256 的 equirectangular HDR；north 在第一行，经度零朝 -Z。太阳盘单独处理，不烘入这个图。
- 对实时天空同时保存 `<prefix>-environment.hdr.json`，包含太阳方向、地表辐照度与角半径。复用 HDR 时自动读取它，保持相同的太阳直射光；普通外部 HDR 没有这个 sidecar 时继续使用场景中的显式灯光。

前 4 spp、16 spp 和后续每增加 32 spp 保存独立 checkpoint；最后保存指定样本数和无后缀的最终输出。当前每次运行从零开始，没有跨进程继续累积或降噪器。

编辑器 `R` 键使用同一 CPU 核心，冻结当前快照，以窗口一半尺寸输出 `build/path-tracing/editor.*`。这是阻塞的静态渲染，未实现交互式渐进显示。双线程编辑器的天空请求进入 RenderRuntime 队列，在设备拥有线程执行，再把纯 CPU 数据交回逻辑线程；CPU worker 不访问 Camera、Material、Texture 或 GPU handle。

## 首次实现与数学边界

`CpuPathTracer` 复用 SceneSnapshotBuilder 的冻结 mesh/material 数据，保留对象变换、相机、共享贴图、材质因子及启用的灯光。移除了原 Connector 对少于三个 mesh 物体的跳过条件，以及一律白色 Lambertian 的材质替换。

加速结构采用扁平二叉 BVH、12 bin SAH 分割、最多 8 个 primitive 的普通叶节点和 60 层深度保护；退化分割回退到中位数。构建只重排整数索引，没有旧 BVH 每个递归节点复制整个三角形列表的问题。迭代遍历按近端 child 优先，使用有界栈；平面 AABB、平行射线及相邻三角形都可正确求交。world normal 使用 inverse transpose。

材质支持编码 albedo 与因子、UV repeat/双线性采样、UV 导出的法线贴图基、metallic B、roughness G、alpha cutoff、双面表面、发光及部分 alpha 的随机覆盖。albedo 的 `pow(...,2.2)` 与当前实时材质定义一致；不把材质 AO 或实时 SSAO/RSM 再乘入真实光传输，避免重复遮蔽/间接光。

BRDF 为 Lambert 漫反射加 GGX/Smith/Schlick 微表面。漫反射与 GGX NDF 采样组成显式混合 PDF；采到无效半球时贡献零，不通过重试改变 PDF。法线贴图按几何半球拒绝无效方向，未实现完整 shading-normal 能量修正、GGX VNDF 或多次散射能量补偿。

积分器使用环境、太阳和发光三角形的 next-event estimation，和 BSDF 路径通过 power heuristic MIS 合成；点/spot 采用显式直射光与遮挡射线。太阳由有限角锥采样，地表辐照度来自同一大气 transmittance，太阳盘 radiance 按角半径换算。第三次反弹后加入 Russian roulette，最大反弹次数仍是有偏的路径长度截断。

天空 LUT 使用实时 shader 的经纬度编码，转换到标准 equirectangular 图；重要性 CDF 按 luminance × texel 精确立体角构建，并加入小权重保证双线性相邻亮度的完整支持。每个 texel 内均匀采样 cos(theta)，sample/evaluate PDF 使用同一分布。天空只有烘焙视点的 RGB 辐亮度，CPU 不追踪参与介质或逐点大气变化。

线程按 16×16 tile 动态分配；首次版本的 PCG 状态由 seed/pixel/sample 决定，逐像素累加顺序固定。不同线程数结果相同，不使用旧的共享 `rand()`，没有每个像素的全局写锁。任何非有限路径贡献使渲染报错，不把 NaN/Inf 静默写成黑色。

## 验证与图像

Apple M4/macOS Release 的开发环境记录如下；运行时还有其他开发工作，不作为严格的吞吐基准。时间统计为追踪开始后累计墙钟时间，包括前面的 checkpoint 保存，不包括 OBJ 导入、BVH 构建与首次天空烘焙。

| 场景 | 预览 320×240 / 8 spp，6 workers | 最终 640×480 / 256 spp，8 workers | 最终射线数 | 非有限样本 |
| --- | --- | --- | --- | --- |
| Sponza | 1.30 秒 | 124.55 秒 | 431,947,603 | 0 |
| San Miguel | 2.04 秒 | 197.08 秒 | 463,524,530 | 0 |

BVH 构建分别约 0.066 秒和 1.95 秒。两张最终 PNG 已目视检查：Sponza 纹理和旗帜、地面直射光、拱廊阴影；San Miguel 植被 alpha、树干、桌椅、建筑及复杂遮挡。阴影区仍有 Monte Carlo 噪声，256 spp 不是收敛参考解；线性 PFM 可供后续提高采样和曝光处理。

CPU 回归覆盖单 mesh/对象变换、平面/平行射线、10,000 条随机射线对暴力求交、空场景、albedo/UV/alpha/法线图/金属粗糙通道、截断贴图拒绝、环境 PDF 归一化及亮区采样、GGX 数值半球积分和 white furnace、环境 NEE+BSDF MIS 的独立积分对照、直接光遮挡、发光面采样、不同线程数/完整 tile/渐进累加及 PFM 格式。真实 Cornell 入口另覆盖场景工厂→快照→CPU 图像。

GPU 桥测试覆盖 RenderRuntime 设备线程上的真实 HDR 烘焙、有限值与太阳能量，再进行 CPU 环境重要性采样。最终验证结果：

| 构建 | 验证 | 结果 |
| --- | --- | --- |
| Metal Release | 完整 CTest，启用 Metal API/Shader Validation | 14/14 通过 |
| Vulkan Release / MoltenVK | 完整 CTest | 15/15 通过 |
| CPU Debug | AddressSanitizer + UndefinedBehaviorSanitizer | CPU 测试通过 |

同一 Sponza 相机、seed 和 128×96 / 4 spp，分别由 Metal 和 Vulkan 烘焙天空后运行 CPU tracing，线性 PFM 的平均 RGB 绝对差为 5.42×10⁻¹¹，最大差为 2.98×10⁻⁸。Vulkan 本机没有 Khronos validation layer，未宣称该层验证通过；Windows/Linux 尚未运行。

## 暂未支持

支持静态 mesh 物体与目标大型场景；程序化地形、草与 FFT 海面通过固定时刻捕获进入求交，包含地形材质、沙滩、泡沫、反射／折射和水下吸收，见 [程序化 PT](path-tracing-procedural.md)。未冻结的海面请求明确报错。计算细分／额外位移后的网格仍未迁移。clearcoat/anisotropy/SSS 特殊 lobe、玻璃体内吸收/介质栈、体积云、运动模糊、景深、跨进程 resume 及 BVH 实例共享仍待后续。后续已加入 [平滑玻璃与 CPU BDPT 焦散](path-tracing-convergence.md)、[Metal/Vulkan GPU PT](path-tracing-gpu.md) 和 [OIDN 降噪](path-tracing-denoising.md)。

保留旧 PT 球/矩形/介质等实验类及历史 Cornell 路径；新场景转换和大型场景入口使用新的连续内存积分核心，不把旧实验类的所有材质模型宣称为已合并。
