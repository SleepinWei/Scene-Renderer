# 测试说明

当前跨平台测试由根目录 `CMakeLists.txt` 注册；构建选项与平台限制见 [构建与运行](../docs/getting-started.md#快速运行)。

## CMake／CTest

先完成对应后端的配置与构建，再运行：

```sh
ctest --test-dir build --output-on-failure
# 独立 Vulkan 构建
ctest --test-dir build/vulkan --output-on-failure
```

| 路径 | 覆盖范围 |
| --- | --- |
| `PT/` | CPU PT／BDPT、材质、采样、输出、多线程确定性、OIDN、地形／沙滩／FFT 水体冻结与 Beer/Fresnel |
| `PT/ScenePackageTests.cpp` | 离线场景包、共享几何、相机深度／宽高比、镜像绕序、PNG／线性 HDR、无效参数及截断输入 |
| `PT/PhotonMapTests.cpp` | 独立平水面照度、光子发射确定性、焦散 AOV、深度预算、HDR 直接光与混合透明阴影提前退出 |
| `engine/` | 任务系统、帧队列、并发契约及 RenderGraph 版本／子资源 pass culling（含 400 个随机参考对照） |
| `engine/EnvironmentTests.cpp` | 环境固定 tick／事件顺序、跳画面、容量／线程边界、检查点原子恢复、太阳输入、解析风积分／波谱目标、旧 schema 升级、降雨单位／实际收支／面积独立性与 PT identity |
| `tools/benchmark_environment.py` | 720p 云／草／湖面联合负载：固定中间态、重复远景动态、地面视角；保留 CPU／GPU trace、检查点与原生预览，GPU 完成间隔不是显示延迟 |
| `tools/validate_environment_editor.py` | 原生编辑器保存到 tick 120、恢复到 121、风／云量过渡中恢复与直接 240 tick 状态逐字段一致、云量精确归零、跨场景拒绝与 CLI 校验；CTest 名为 `engine-environment-editor`，需要桌面 GPU 环境 |
| `rhi/` | 设备与图形契约、图像解码、OpenGL 状态与 Vulkan 验证 |
| `test_bake_terrain_vt.py` | 地形离线分页、mip、边框与输入校验 |

其他 GPU 自检入口保留在 `src/rhi/` 和 `src/renderer/rhi/`，由渲染器命令行及 CTest 调用。测试集合随平台与构建选项变化；使用 `ctest --test-dir build -N` 查看当前注册项。

`rhi-temporal`／`--temporal-self-test` 覆盖 HDR、固定输出网格的 1/4 像素 jitter 恢复、物体运动／反应性、历史重置，以及颜色／粗糙度／NPOT mip、GPU 子资源读回和分批上传预算。可用 `--backend Vulkan` 验证另一原生后端。[AA 实现、实景对照和验证范围](../docs/realtime-anti-aliasing.md)。

`geometry-culling-native`／`--geometry-culling-self-test` 验证物体与三角形视锥裁剪、GPU 稳定索引压缩、生成网格 active count、零工作、在途复用、流式几何失效、geometry／forward／TAA 像素对照。[算法、开关与局部计时](../docs/geometry-culling.md)。

Python 地形测试需要 NumPy 与 Pillow：

```sh
python3 tests/test_bake_terrain_vt.py
```

## 历史反射实验

`legacy/test.cpp` 与 `legacy/pch.*` 保留早期 GoogleTest 反射实验源码，供历史参考；它们未注册到当前 CMake／CTest 测试集合。独立 Visual Studio 工程与 NuGet 清单已移除，当前构建与回归统一使用 CMake，无需恢复旧 NuGet 缓存。

程序化 PT 数值测试为 `pt-procedural`；`pt-native-sky` 另验证设备线程 FFT 捕获、时刻变化、异常回传及水面／水下相机的 CPU/GPU 能量一致性。覆盖范围与实际场景图见 [程序化 PT](../docs/path-tracing-procedural.md)。

2026-10-07 GPU Session 回归增加分步／离线固定样本一致性、暂停／续采／取消、在途 film reset／resize、直接 GPU 显示的色调映射／行方向／黑边与曝光不重算。`pt-native-sky` 再验证 RenderRuntime 异步准备、旧请求隔离、暂停 resize、完成／取消和错误恢复；Metal 与 Vulkan/MoltenVK 的独立原生自检及主程序 `--pt-self-test` 均通过，CPU PT 六项测试通过。真实 Cornell 编辑器 `--pt-viewport --size 640x480 --frames 340` 已捕获预览；这是功能验证，非帧率基准。使用与首版限制见 [渐进 GPU 视口](../docs/path-tracing-gpu.md#渐进-gpu-视口2026-10-07)。

同日增量缓存回归加入 CPU BLAS 复用／几何替换／预算／淘汰安全、camera ray／PDF／projection／水下介质更新与精确内容身份；GPU 检查 cold 14／hot 0／material 1／transform 2／light 1／camera 0 的 scene 上传块数及新建 session 原始图一致性、64-bit seed、预算淘汰、静态 photon map 相机复用；主程序检查导航、相机／材质 epoch 复用、旧取消包隔离和 FFT 水下 camera-only 一致性。缓存预算只约束保留条目；尚未验收完整 Barcelona 连续交互的进程峰值／延迟。

P2-E1 增加独立 GPU BLAS／TLAS 导出、BLAS root 地址范围与移动时 BLAS 字节不变、变换上传量严格等于 TLAS＋instances 的断言；native viewport 自检同步更新为 14 个缓冲。CPU／photon／package／medium／procedural 5/5，Metal／Vulkan package 原生自检均通过，Metal 编辑器 1/1（30.99 s）、Vulkan／MoltenVK 编辑器 1/1（39.49 s）通过。主程序自检使用 `MTL_DEBUG_LAYER=0 MTL_SHADER_VALIDATION=0`；本机没有 Khronos validation layer。首次 Metal 主程序请求 Preparing 超过 60 s 期限；热启动后还修正了自检旧 13 缓冲预期，最终两后端通过，未将测试期限当作帧时间保证。

完整 Barcelona revision 5 保留全部 20,622 植被粒子的 cold／3 次 hot／transform／material 准备探针通过：固定 seed raw film、refit／fresh rebuild 及 Metal／Vulkan cold／transform PFM 逐值一致；所有 prepare 调用工作量 ≤4 MiB，移动只上传 3.45 MiB TLAS＋instances。完整命令、累计 RSS 和限制见 [P2-E1 测量](../docs/path-tracing-gpu.md#独立-blastlas-与完整-barcelona-测量p2-e1)；这是准备 API 回归，未替代完整 FFT／Barcelona 编辑器延迟验收。

P2-E2 增加 CPU packed geometry／texture 共享、唯一 alpha 图像扫描、图像／几何／顺序失效、并发导出与旧导出 lease 生存期；GPU 验证 weak identity 到期后精确比较回退、新身份提升、贴图／几何更新，以及中途取消与原始 film 保持一致。最终 CPU 相关 6/6（4.51 s），Metal／Vulkan package 原生自检均通过，Metal 编辑器 1/1（32.50 s）、Vulkan／MoltenVK 编辑器 1/1（36.15 s）通过；主程序沿用 `MTL_DEBUG_LAYER=0 MTL_SHADER_VALIDATION=0`。新增原生用例初次运行暴露测试自身的空材质解引用，已修正用例构造并重跑两后端通过。

完整 Barcelona 的最终 E2 探针中，hot geometry／textures 均共享，GPU 身份命中 5 个数组、跳过 96.40 MiB 比较；不同身份仍逐字节验证。两后端 cold／transform 的原始 PFM 与 E1 及另一后端完全相同。阶段中位数、资产 hash、上传量及累计 RSS 保存在 [E2 测量](../docs/benchmarks/pt-packed-export-2026-10-07.json)。E2 当时仍待局部 patch、完整编辑器延迟、FFT 多时刻持续操作和冷管线预热；E3 的局部上传进展见下。

P2-E3 增加 4 字节对齐的 RHI buffer copy 合同与传输／patch 验证；原生 PT 回归覆盖 64 个分散材质标量、比较／clone／写入字节预算、帧内及离线的复制／patch 中途取消、活动旧会话的 raw film、密集变化与尺寸变化回退。Vulkan 首轮取消测试暴露离线异步复制未绑定退休令牌的问题，已改为离线等待复制完成、帧内沿用完成令牌，随后两后端原生自检通过。完整 Barcelona 的 transform／material 主机上传为 24／4 B，GPU clone 仍为 3.45／3.87 MiB；cold／transform 与 E2 及跨后端 PFM 完全相同，material patch／full-upload 原始 film 及跨后端 PFM 也相同，见 [E3 测量](../docs/benchmarks/pt-sparse-upload-2026-10-08.json)。CPU／RHI 相关测试最终 8/8（6.64 s），最终 Metal 编辑器 1/1（30.80 s）、Vulkan／MoltenVK 编辑器 1/1（31.21 s）通过；主程序沿用 `MTL_DEBUG_LAYER=0 MTL_SHADER_VALIDATION=0`，本机没有 Khronos validation layer。完整实景连续 UI 操作、FFT 多时刻与冷管线预热仍未验收。

本轮最终验收：Metal 构建 CPU 六项、Cornell 离线、`pt-native-sky` 和 `rhi-editor-resize` **9/9**（39.21 s）；Vulkan 主程序 `pt-native-sky` **1/1**（33.72 s）。两后端独立 package 原生自检通过；实际 Cornell 编辑器半分辨率 PT 捕获 80 帧预览并检查显示。以上为功能回归，不是吞吐基准；本机 Vulkan 没有 Khronos validation layer。

P2-E4 增加 `--pt-viewport-benchmark --scene PATH|--fixture fft --output PREFIX --cycles 3`，实际走 RenderRuntime、Raster streaming、camera-only、material／transform 或 FFT t=2／3／4、导航、部分准备取消和恢复；显式导出与 fresh capture／full upload 比较。FFT 水下相机检查初始 medium ID；每帧累计准备工作不超过请求预算。窗口实际 framebuffer 尺寸传给 Runtime，避免 Vulkan 持续重建 swapchain。原生 package 自检增加预热 descriptor 复用、idle-cache limit=0 时活动 lease 保留、实际 trace／display 无重复编译；主程序回归增加冻结 HDR／sun owner 失效、阶段计时与取消旧导出 future。测量范围与数据见 [E4](../docs/path-tracing-gpu.md#预热与完整-runtime-连续操作p2-e4)。

最终 CPU／RHI 相关 8/8（3.34 s），Metal／Vulkan package 原生自检通过，Metal 主程序 1/1（35.25 s）、Vulkan／MoltenVK 主程序 1/1（28.75 s）通过。两后端 FFT 三轮时刻／水下 fresh reference 与恢复检查通过；Barcelona 全操作跨后端 raw PFM 完全相同，FFT 跨后端 relative L1 最大 2.51×10⁻⁸。准备合并后 FFT 的小上传可一帧完成，取消探针改用 1 KiB 预算以确实观察部分上传。当前工作区构建另修复 terrain shared 变量与间接参数重名，以及 water-depth 的 208 B 参数结构／pipeline layout 不一致；没有改变其计算逻辑。

P2-E5 预览实例合批／准备调度（2026-10-08）：原生主程序先对照 individual／instanced HDR、position／normal G-buffer，覆盖镜像、非均匀缩放与 cutout。检查透明／SSS 分隔、静态 pose 复用、更新旧 lease、8,192 实例的全部 4,096 姿态块复用、空输入释放、奇异／inverse 溢出拒绝；SceneAdapter publication rollback 清除派生 cache 并保留 accepted pose。Runtime 验证部分准备期间的 clean preview、准备预算、导航、取消／epoch／降噪／线性输出。最终 CPU／RHI 8/8（4.18 s），Metal／Vulkan 主程序各 1/1（29.04／28.35 s）。实际 Cornell GUI 180 提交帧、两次 OIDN，并检查最终截图。

完整 Barcelona 两后端优化三轮、同构建关闭 batching／preview reuse 的旧策略一轮：21,129→113 Raster packets，pose 上传合计 1,346,112 B；32×18 初始 Raster PPM 与第一轮 raw PFM 策略间完全相同，全部最终 film 与 E4 及另一后端相同。FFT 512／256 谱、129 网格的 t=2／3／4 与水下切换／取消恢复全部通过，各后端对照 fresh capture／full upload。测量与局限见 [E5](../docs/path-tracing-gpu.md#pt-预览批绘制与准备调度p2-e5)；未将低分辨率探针作为高分辨率 GUI 交互达标。初版只缓存大组第一块，已改为所有稳定 member-ID 块并加入回归；原生构造补有限 inverse 检查，避免合批绕过普通绘制的矩阵验证。

P2-B 后续验收（2026-10-07）：CPU 加入 TLAS reuse／refit、拓扑变动、质量恶化重建、第 33 次更新重建、零缓存预算下的 previous-scene BLAS lease、派生表与透明／介质边界失效，以及 20,000 实例的矩阵／边界稀疏更新。CPU／Metal／Vulkan 等距离最近命中统一按 primitive ID 选择；多实例 refit 原始图对照 SAH 全量重建，GPU 命中材质／距离对照 CPU brute force。主程序另检查材质不重建 TLAS、变换只上传变化的边界与 instance 缓冲。

最终 Metal 相关回归 **9/9**（32.51 s），Vulkan 主程序 **1/1**（25.28 s），两后端独立 package 原生自检通过。首次 GPU 管线冷准备曾超过旧的 15 s 请求期限；视口自检改为 60 s 请求期限／150 s 整体期限，随后单独验收通过。这些期限用于检出停滞，不代表 UI／渲染帧预算。完整 Barcelona 的交互峰值与冷准备延迟仍待后续阶段测量。

P2-C 分帧 GPU 准备（2026-10-07）：原生自检覆盖 256／1024 字节预算、非对齐预算向下取整、管线与上传分开、精确缓存比较、film／guiding／training 初始写入、未完成 film 拒绝采样／读回／显示／reset，以及冷／热准备中途取消不污染缓存。worker payload 在取消／完成时释放；准备期间暂停状态保留。分块构造与同步构造的原始 radiance、sample count／ray、光子 caustics AOV 和训练统计完全相同。

小 fixture 材质更新：24 次准备调用，22,044 总 payload 字节、19,412 实际传输字节，12 个缓存命中、1 个 scene buffer 上传；这是正确性回归，含 film／训练初始化，不是大场景性能基准。主程序用每帧 1024 字节预算验证 **Uploading** 期间至少继续 3 个 Raster 帧且 0 spp，中途取消与 queued old epoch 隔离，替换请求完整准备后才采样。时间预算仍为协作式，未对不可分的管线编译／分配／训练作严格帧时限承诺。

P2-C 最终验收：Metal CPU 六项、Cornell 离线、主程序 `pt-native-sky`、`rhi-editor-resize` **9/9**（34.51 s）；Vulkan 主程序 `pt-native-sky` **1/1**（31.54 s），两后端独立 package 原生自检通过。本机 Vulkan 使用 MoltenVK，未安装 Khronos validation layer。另实际导出 Metal GPU Cornell 32×24／4 spp，并解析 `gpu_preparation` JSON：3 次同步准备调用、76,076 实际传输字节（含 film／训练初始化）、13 个 scene buffer；验证报告字段与传输量定义。

P2-D 视口 OIDN 与线性输出（2026-10-07）：`pt-denoiser` 增加同尺寸 context 的 device／三个 filters 复用、辅助配置切换、resize 后 device 保留／buffers 替换和跨线程拒绝。新增 `pt-viewport-postprocess` 验证单任务／待回收结果的背压、拒绝提交不转移 source、worker 持久 context、推理前／推理后取消、raw／filtered PFM 与冻结 metadata、错误 future、无 OIDN 的 raw 导出和 shutdown 完成已接受任务。原生 display 自检以独立常量 HDR 检查分块发布、tone mapping、raw 切换、无额外读回、原始 film／sample／ray 不变，以及 reset 后派生失效与 binding 退役。

主程序另验证暂停／resize／曝光／raw toggle 不重复读回，camera reset 不显示旧 epoch，关闭预览后无自动 readback，再启用后最终检查点只读一次；实际导出的 raw PFM 对照同参数 fresh GPU 图完全一致，并验证独立 filtered PFM 与 epoch／seed／samples 报告。最终 Metal 相关 **10/10**（32.28 s）；Vulkan 主程序 **1/1**（28.46 s），两后端独立 package 原生自检通过；OIDN OFF 单测 **2/2**（1.20 s）。本机 Vulkan 使用 MoltenVK，无 Khronos validation layer。

实际 Metal Cornell 编辑器：640×480 显示、320×240 film、180 提交帧；两次 CPU OIDN 处理分别约 0.237／0.110 s，首次数值含初始化，不能直接作为冷／热执行对照。截图位于 `build/path-tracing/viewport-oidn/cornell-editor.png` 并做视觉检查。原生 `--screenshot` 实际输出 P6 PPM，本次保留 `.ppm` 原件并转换 PNG；未改动像素。该预览验证实际显示流程，不替代完整 Barcelona／FFT 的交互与进程峰值基准。

`pt-scene-package` 使用小型自包含 fixture，不依赖 Blender 安装或下载资源。两个实际 Blender 场景另以固定 seed／Sobol、160×90、64 spp、深度 16 比较 CPU／Metal／Vulkan 原始 PFM；不使用 OIDN 图判断积分器差异。2026-10-05 相关回归 Metal／Vulkan 各 7/7、ASan/UBSan 4/4，图像指标与来源校验见 [Blender 验收记录](../img/path-tracing/blender-validation.json)。

Blender 材质的可选集成回归为 `PT/BlenderExportTests.py`，无需下载测试模型，但需要 Blender。按 [运行说明](../docs/blender-path-tracing.md#薄玻璃与多通道材质改进) 执行；真实烘焙后直接检查 PNG 的颜色编码、线性 alpha／ORM 和不透明图集。CPU 数值回归另覆盖 thin-sheet 的 Fresnel、能量、透射阴影、介质身份与 NEE/MIS；native sky 回归包含同模型的 Metal／Vulkan 对照。

Barcelona revision 4 增加实际 Normal Map 烘焙、PNG／UV／normal 绿通道方向、隐藏发射器保留实例、常量体积转换、封闭池体边界与体积的验证。场景包检查 RGB 系数、贴图 roughness、前后 normal 与非法参数；native sky 验证空气／水内的切线 normal 和池体 roughness。Metal／Vulkan 各 7/7、ASan/UBSan 4/4，见 [池水验收](../img/path-tracing/blender-water-validation.json)。

同参数 Cycles 基线增加显式 Lambert／相关 Smith 单次散射 GGX：CPU 以独立立体角积分验证估计器和 Lambert 能量；场景包检查模型枚举与薄片标志互不覆盖；native sky 验证两种模型的 CPU／Metal／Vulkan 图像与粗糙池体。实际 Barcelona 4096 spp 原始 PFM 对照同时记录 Cycles 独立 seed 噪声，见 [同包测量](../img/path-tracing/blender-barcelona-matched.json)；它验证共同闭包输运，不代表原始 Blender shader 图已全部还原。

共享 BLAS/TLAS 回归以 64 个镜像／非均匀实例与独立展开场景、世界空间 brute force 对比 4096 条射线，检查非归一化方向的 t、UV alpha、法线、材质、发光面世界面积及实例介质身份；GPU 另检查 1024 条射线、共享 emitter／介质输运和 checkpoint 间隔不改变固定样本。完整 Barcelona 包保留全部 20,622 植被粒子，以 160×90、64 spp 的 CPU／Metal／Vulkan 原始 HDR 验证。Metal、Vulkan 各 7/7；ASan/UBSan 四项全部通过（CPU 首轮并发负载触及 60 秒超时，单独重跑 51.11 秒通过）。详细结果见 [实例验收](../img/path-tracing/blender-instances-validation.json)。

revision 5 的 CPU 数值回归覆盖 8 组 corner tangent／镜像 UV／镜像实例／非均匀缩放／背面组合，以独立解析 frame 检查；32,768 次薄叶采样验证 R+T 白炉和两半球选择概率，背面点灯验证 NEE／空介质栈。包回归检查切线 sidecar 共享、截断、偏移、路径与薄叶非法 IOR；Blender 集成检查独立 R／T 图编码及 tangent handedness。GPU fixture 包含这些帧与透射贴图。Metal、Vulkan 各 7/7，最终 ASan/UBSan 4/4（CPU 57.36 秒；开发首轮并发负载触及 60 秒超时，单独重跑后通过）。实际图与 Cycles 对照见 [外观验收](../docs/blender-path-tracing.md#切线基准与薄叶透射)；小场景可由 `PT/BlenderAppearanceFixture.py` 复现。


2026-10-06 闭包法线回归：60 组掠射方向／倾角验证镜面反射仍位于几何正半球且正常法线不变；每个 GGX／薄叶模型使用 384×512 独立球面积分与 131,072 次 Monte Carlo 检查不同法线下的求值、PDF、几何余弦测度和能量。GPU fixture 对 Lambert／GGX／薄叶分别加入强法线图；CPU 拒绝未验收的 BDPT 对照闭包。Metal、Vulkan 各 7/7；ASan／UBSan 四项最终代码回归通过，CPU 首次在构建并发时超时，仅重跑失败项后 53.90 秒通过。复现与原始图见 [掠射法线验收](../docs/blender-path-tracing.md#掠射法线与闭包遮蔽)。

原生实时水体：`Scene-Renderer --water-self-test`（或 CTest `water-native`）验证零消光透明度、解析 Beer 透射、平面水体单次散射能量、独立水下捕获、水上遮挡、前向／延迟一致性、相机网格／TSAA、resize 和 mask；同时执行既有 FFT、大气及 TSAA 回归。详见 [实时水体升级验收](../docs/water-realtime-upgrade.md)。

同一入口新增水下观察段的 Beer／连续散射积分、体积阴影、捕获／雾开关、单次眼侧衰减、远空气侧目标、临界角内外的折射／全反射、屏幕外床面反射、干 mask 与穿越水面／TSAA／奇数重建。[水下渲染验收](../docs/water-underwater-rendering.md)。

空气／水下分层捕获回归使用解析 Snell 光线布置小目标与浸水遮挡物：直线相机不可见、折射路径可见时必须保留目标，关闭捕获时的反例必须失败；同时检查空气位置未泄漏到水下层。


太阳反射回归使用 `makeWaterSolarValidationScene`：65,536 次关闭／开启提议的固定 PCG 路径与独立太阳圆盘积分检查期望及方差；改为 generic dielectric 再验证，确保导入池水不依赖 FFT kind。GPU fixture 同时比较两类界面的 CPU／GPU continuation 密度。计数缓存的测试覆盖保留实例的 scattering／dielectric／kind 查询；大场景微基准在关闭太阳提议时要求原始值完全相同。详见 [太阳反射与统计缓存](../docs/blender-path-tracing.md#太阳反射链与实例统计缓存)。

太阳提议使用 Sobol 192／194 维，与 alpha 20–148 区间分离；新增三组 seed 的条件概率积分回归，防止接受透明覆盖后与 proposal selector 重用坐标。

本轮最终代码：Metal／Vulkan PT CTest 各 7/7，ASan／UBSan 4/4（82.31 s）。`pt-transport-benchmark` 为显式构建的 trace 微基准目标，运行通过且原始样本与计数 A/B harness 相同，默认构建／CTest 不执行性能微基准。

薄玻璃太阳回归 `makeThinSolarValidationScene` 独立积分 Lambert → 薄片反射 → 两次有色直通 → 有限太阳；固定 PCG 65,536 路径检查期望及方差，另验证重叠 cone、镜像变换后的几何法线、最多八个 catalog 方向与暗太阳白炉的 NEE／MIS。原生 GPU fixture 同时比较普通和重叠 cone 的 CPU／GPU 输出。`pt-package-render --backend Metal|Vulkan --self-test` 可运行独立原生 PT 验证，先显式构建该 EXCLUDE_FROM_ALL 目标；[使用方式与限制](../docs/blender-path-tracing.md#薄玻璃太阳反射链)。

可选近岸水体的同一 `--water-self-test` 还验证 DDA／海床回退、局部多次散射 LUT 与独立 MC seed、静水平衡／闭域质量守恒、湿干与相机 patch 滚动、上岸／退水／泡沫、活动 TSAA／奇数 resize 及开关后的旧状态拒绝；[实际结果与动画](../docs/water-coastal-features.md)。

Photon mapping 与通用 PT 优化回归：`pt-photon` 独立验证平水面 Fresnel／Beer／投影照度（九点平均与解析比约 0.98787）、面积／HDR 发射、发光表面的间接反射、并行确定性、无水体零焦散、总深度和 AOV，以及混合薄片／不透明阴影。GPU 检查共享 photon map 与 1／4／16 spp 批次；Classroom 实际场景的 closest／any-hit／batch8／batch16 PFM 完全一致。最终 Release 6/6；ASan／UBSan 原有 4/4（111.90 s）＋独立 photon 1/1（43.22 s），Metal／Vulkan 原生自检和主程序 CPU pool 命令通过。开发时合并在 `pt-cpu` 的新增光子测试曾触及 120 秒超时，拆成独立目标后全部通过。[实际图、测量与限制](../docs/path-tracing-photon-mapping.md)。

P2-E6a 主视图可见性／720p 导航（2026-10-08）：原生 PT 回归覆盖六个 homogeneous clip plane、两像素 guard、无效变换保留、generated／透明 fallback、独立 shadow depth／water 辅助集合、TAA fallback 和静态批零重传。`water-native` 用离屏发光体验证空主视图配完整辅助列表的反射 HDR 与未裁剪参考一致。最终 CPU／RHI 8/8（3.57 s），Metal／Vulkan 的 PT＋water 各 2/2（27.17／28.45 s）。

`--raster-size 1280x720 --navigation-probe` 对完整 Barcelona 做同构建开启／关闭裁剪对照：三个 X 位姿各 20 稳定帧，720p 初始与导航 PPM 完全相同，所有 raw PFM 与同后端关闭裁剪、另一后端及 E5 相同；fresh capture 参考通过。FFT 320×180 的导航、t=2／水下和取消恢复通过。范围、复现和限制见 [E6a](../docs/path-tracing-gpu.md#主视图可见性与-720p-导航p2-e6a) 与 [JSON](../docs/benchmarks/pt-viewport-visibility-2026-10-08.json)；没有将隐藏 surface／低分辨率 PT film 宣称为高分辨率 PT 或 GUI input-to-photon 验收。

M1 材质外观扩展（2026-10-08）：CPU 增加各向异性 GGX 的独立半球积分／采样估计、PDF 接受概率、正常／掠射白炉、互易、符号／90°旋转、零参数、镜像 UV frame、打包／参数校验和 transport identity 失效；面积光 BDPT 能量对照通过。两 GPU 后端增加 normal map／corner tangent／镜像非均匀变换的 PBR 和 ggx_add 对照。最终 CPU 相关 6/6，Metal／Vulkan 原生各 1/1（26.58／27.09 s）。三球实际图 CPU/GPU relative L1 7.867×10⁻⁵，两 GPU 原始图完全相同。第一次 Metal CTest 超过 150 s，随后完整直接运行通过；没有放宽标准 CTest，冷编译成本单独记录。详见 [材质文档](../docs/path-tracing-materials.md) 与 [报告](../img/path-tracing/anisotropic-ggx-validation.json)。

毛发／布料 PT（2026-10-09）：`pt-cpu` 增加球面 PDF 积分、采样一致性、白炉／吸收、极窄／宽散射与倾斜带边缘有限性、布料反射互易性、256 B ABI、身份失效及不受支持 BDPT／photon 组合拒绝；`pt-scene-package` 验证字段与非法参数。`--pt-self-test` 在 Metal／Vulkan 对照两种闭包、切线、两侧散射及窄／宽毛发边缘。完整静态薄带样例还做原始 PFM 三后端对照，见 [参数与复现](../docs/path-tracing-fibers.md) 和 [记录](../img/path-tracing/hair-cloth-validation.json)。

针织／groom 扩展新增 `pt-groom` 的独立 Python 几何检查，以及 CPU／双后端的针目周期／方向 sheen／参数与 272 B ABI 对照；四个实际模型包也做原始 HDR 三后端对照。使用 `ctest -R 'pt-cpu|pt-groom|pt-scene-package'` 和完整原生 PT 回归，详见 [毛衣、Bunny 与长发](../docs/path-tracing-grooms.md) 及 [记录](../img/path-tracing/grooms-validation.json)。

独立高密度头发 demo：`PYTHONDONTWRITEBYTECODE=1 python3 tests/PT/HairDemoTests.py` 检查 seed、导向分布、半径／控制点、头部上半部分间距，以及三个视角的相机和精确发丝三角形数量。独立实际渲染与原始 HDR 对照见 [demo 文档](../docs/path-tracing-hair-demo.md)。

公开头发基准：`python3 tests/PT/HairBenchmarkTests.py` 检查 HAIR header／属性数组、可变段数、截断／非法输入、宽度保留、坐标变换、OBJ 索引和 PBRT 线性颜色／曲线导出。可选 `tools/run_pbrt_hair_probe.py` 链接官方 PBRT v3，独立检查 61,440 组毛发散射方向／参数；完整 Yuksel 直发／波浪发另有 CPU／Metal／Vulkan 和 PBRT 原始 HDR 对照，详见 [基准文档](../docs/path-tracing-hair-benchmarks.md)。


实时共享 GGX 回归位于 `pt-native-sky`：Forward、Deferred、Scene deferred／forward 的 1,728 个线性 HDR 通道对照 CPU PT 求值，覆盖方向、法线图、UV／corner frame、镜像／非均匀缩放、普通实例、材质修改和分块切线上传；测试会生成原生实时三球 PNG 与 JSON。`pt-cpu` 另验收共享扩展参数的打包与积累失效，`rhi-gpu` 覆盖旧材质退化、属性复制／捕获和非法输入的原子拒绝。详见 [材质文档](../docs/path-tracing-materials.md#m1-r实时渲染与-pt-共用材质输入)。

`hiz-culling-native`／`hiz-culling-vulkan`／`--hiz-culling-self-test` 验证当帧 GPU 遮挡数量与图像一致性，覆盖 alpha／几何孔洞、奇数边缘、near／eye、移动／大世界、三角形组合、辅助阴影、resize 和 TAA。[Hi-Z 算法与诊断](../docs/hiz-culling.md)。

连续云量的独立 GPU 回归可用 `./build/Scene-Renderer --cloud-self-test`，Vulkan 追加 `--backend Vulkan`；完整 `--rhi-self-test` 也包含它。


环境 L3 雨湿度回归：`engine-environment` 验证米制收支、面积与视口无关、溢流／实际出流、非法恢复与事件重放；`pt-procedural` 验证冻结湿度和雨湿／岸湿的单次合并。`environment-rain-native`／`environment-rain-vulkan`（`--rain-self-test`）验证雨丝的前景深度遮挡、暂停确定性、有限 HDR 与独立显示开关。可用 `SCENERENDERER_RAIN_CAPTURE_DIR` 导出 dry／wet／spatial／rain PPM 和验证 JSON。`--pt-self-test` 另比较湿化后的四条 Raster 路径及 CPU／GPU PT。编辑器脚本验证 `--environment-rain` 和 schema 5 重放；操作、支持范围与实测见 [环境规格 L3](../docs/mountain-lake-environment-spec.md)。


空间地表水量回归增加供水限制、同时沿坡转移、内部／外排账户分离、容量溢出、湖岸出口、源遮罩面积与 V 翻转、schema 4 均匀升级及非法恢复事务。`SCENERENDERER_SPATIAL_WATER_REPORT` 可导出 CPU 验证 JSON；`tools/validate_environment_editor.py --spatial-terrain` 另用实际山湖检查 30+30 秒与直接 60 秒逐字段一致、均匀模式开关和预算残差。雨湿原生测试覆盖空间干／湿区域和独立水面捕获；完整 PT 回归覆盖空间湿化后的四条实时路径及 CPU／GPU PT 湿岸对照。图像和结果见 [L3 空间包](../docs/mountain-lake-environment-spec.md#l3-后续空间存水与沿坡转移2026-10-09)。

`--engine-self-test --backend Metal|Vulkan` 单独运行 engine 原生契约，包含地形参数编辑后保留沙滩／空间湿度、复用几何、候选参数隔离及发布回滚；可无需运行完整 PT／水体自测复现此边界。

## Hierarchical cluster geometry validation

分层 cluster 几何回归：`cluster-hierarchy` 检查确定性、128 三角形上限、混合 LOD 覆盖／共享边和预算；`cluster-rendering-native`／`cluster-rendering-vulkan` 比较实际 GPU 索引与独立 CPU 切面，并检查 alpha、镜像／剪切、正交投影、剔除和恢复；新增跨网格共享压缩区间、重复 ID／不同变换、来源更新和 11 组当帧 cluster Hi-Z HDR／显示对照。还验证两阶段历史剔除的冷启动、遮挡物移走／返回、alpha 孔洞、镜像／剪切、相机切换、LOD 改变、几何替换、重复 ID、resize／reset、功能切换和 TAA jitter；比较 HDR、显示、六张 G-buffer，并逐索引检查早期＋晚期 compact stream。Sponza 导出两套真实 GPU mip 与实际早期附件，离线逐 mip max reduction／逐 cluster 当帧遮挡证明见 [两阶段报告](../docs/cluster-two-stage-hiz.md)。可用 `--cluster-rendering-self-test --backend Metal|Vulkan` 直接运行；`SCENERENDERER_CLUSTER_CAPTURE_DIR` 导出实际 Sponza 渲染与线框。详见 [算法、性能和验证](../docs/hierarchical-cluster-geometry.md)。

## DDGI first-stage validation

`Scene-Renderer --ddgi-self-test` (Metal, hidden GLFW) and `rhi-vulkan-validation --ddgi-self-test` (offscreen Vulkan) exercise actual GPU irradiance/distance caches: analytic sky quadrature and hemisphere occlusion, alpha masks, instanced and off-camera occluders, sealed-floor coverage, colour bleeding, thin-wall visibility, incremental updates/light switches, solid-probe reactivation and forward/deferred GI increments. `SCENERENDERER_DDGI_CAPTURE_DIR` saves images, field bytes and metrics. See [DDGI implementation and evidence](../docs/ddgi.md). CTest registers `rhi-ddgi` as a GPU test with a 180 s timeout; the graphics contract covers compute-to-fragment read-only storage dependencies.
