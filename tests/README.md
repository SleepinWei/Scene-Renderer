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
| `engine/` | 任务系统、帧队列与并发契约 |
| `rhi/` | 设备与图形契约、图像解码、OpenGL 状态与 Vulkan 验证 |
| `test_bake_terrain_vt.py` | 地形离线分页、mip、边框与输入校验 |

其他 GPU 自检入口保留在 `src/rhi/` 和 `src/renderer/rhi/`，由渲染器命令行及 CTest 调用。测试集合随平台与构建选项变化；使用 `ctest --test-dir build -N` 查看当前注册项。

Python 地形测试需要 NumPy 与 Pillow：

```sh
python3 tests/test_bake_terrain_vt.py
```

## 历史反射实验

`legacy/test.cpp` 与 `legacy/pch.*` 保留早期 GoogleTest 反射实验源码，供历史参考；它们未注册到当前 CMake／CTest 测试集合。独立 Visual Studio 工程与 NuGet 清单已移除，当前构建与回归统一使用 CMake，无需恢复旧 NuGet 缓存。

程序化 PT 数值测试为 `pt-procedural`；`pt-native-sky` 另验证设备线程 FFT 捕获、时刻变化、异常回传及水面／水下相机的 CPU/GPU 能量一致性。覆盖范围与实际场景图见 [程序化 PT](../docs/path-tracing-procedural.md)。

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
