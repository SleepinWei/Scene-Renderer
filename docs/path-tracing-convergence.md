# GPU Guiding、Radiance Cache 与 BDPT 焦散

本轮在已有 Metal/Vulkan compute PT 上加入冻结的空间方向学习表、可选的漫反射延续缓存，并增加 CPU BDPT 数学参考和玻璃焦散场景。普通 PT 仍为默认；算法存在不等于所有场景都会更快收敛，训练时间和局部误差均需计入验收。

## 运行

沿用 [构建说明](getting-started.md#快速运行)。本机验证目录为 `build/pt`（Metal）、`build/pt-vulkan`（Vulkan/MoltenVK）。

```sh
# 训练 64 spp 后冻结方向表，再运行 GPU PT。
./build/pt/Scene-Renderer --path-trace-gpu sponza --pt-guiding --pt-fixed --pt-size 320x240 --pt-samples 256 --pt-bounces 16 --pt-exposure 3 --pt-output build/path-tracing/convergence/sponza-guiding

# 可选有偏的粗糙漫反射缓存；二者可以组合。
./build/pt/Scene-Renderer --path-trace-gpu san-miguel --pt-guiding --pt-cache --pt-fixed --pt-size 320x240 --pt-samples 256 --pt-bounces 16 --pt-exposure 3 --pt-output build/path-tracing/convergence/san-miguel-cache

# CPU BDPT：有限面积光源、闭合玻璃球和漫反射接收面。
./build/pt/Scene-Renderer --path-trace caustics --pt-bdpt --pt-size 640x480 --pt-samples 512 --pt-bounces 8 --pt-threads 8 --pt-exposure 2 --pt-output build/path-tracing/convergence/caustics-bdpt

# 无玻璃对照；保持相机和面积光完全相同。
./build/pt/Scene-Renderer --path-trace caustics --pt-bdpt --pt-no-glass --pt-size 320x240 --pt-samples 256 --pt-bounces 8 --pt-output build/path-tracing/convergence/caustics-no-glass
```

`--pt-bdpt` 自动选择固定 spp；之后显式加 `--pt-adaptive` 会被拒绝。GPU + BDPT 或 CPU + guiding/cache 也会明确报错。`--pt-no-glass` 控制专用 `caustics` 场景的玻璃几何。

| 参数 | 默认值 | 含义 |
| --- | --- | --- |
| `--pt-guiding` | 关闭 | BSDF 延续方向与可见天空方向的学习提议 |
| `--pt-cache` | 关闭 | 有偏的粗糙漫反射延续复用 |
| `--pt-training-samples N` | 64 | 独立训练阶段的每像素 spp，1–512 |
| `--pt-guide-cell S` | 场景最大轴范围 / 48 | 世界空间分区尺寸，较大分区共享更多样本，也增加空间近似 |
| `--pt-cache-min N` | 64 | 缓存记录数下限，1–8192 |
| `--pt-cache-depth N` | 2 | 从第 N 次散射开始允许缓存终止，1–128 |
| `--pt-bdpt` | 关闭 | CPU BDPT，最大散射次数上限 32 |

输出沿用 PNG/PFM、材质/法线 AOV、采样图和 JSON。BDPT 另外输出 `*-caustics.png` / `*-caustics.pfm`：光源到相机的完整路径含有玻璃 delta 事件，随后在非 delta 表面散射的贡献；它是完整图的子集，不是光照贴图或加亮后处理。JSON 记录训练耗时/射线、有效分区、引导/缓存命中、玻璃网格数、焦散能量比例。`trace_and_training_seconds` 包含训练，模型导入、CPU BVH 和设备准备另计。

## Guiding 与缓存的实现

`pt-learning.glsl` / `GpuPathTracer.cpp` 使用 16384 个哈希槽，键包含世界空间分区和法线主轴/符号；不同键碰撞时放弃该训练记录，查询不匹配就回退原积分器。每槽最多保留 8192 个记录，每个方向表使用 8×8 等立体角世界方向分区。

训练使用独立的像素 seed scramble，循环选择前八次散射中的一个顶点。一个表学习 BSDF 延续贡献的漫反射 product 分布；另一个表学习带可见性的天空 NEE 贡献。整数原子累积、读回和冻结均通过 RHI 完成，不依赖浮点 atomic。CDF 只在训练后生成；渲染期间读取不可变表。

有至少 128 个训练记录、非金属且 roughness ≥ 0.5 的表面才启用方向引导。延续方向提议为 `0.65 × BSDF + 0.35 × learned`，天空提议为 `0.5 × HDR + 0.5 × learnedVisibleSky`。采样返回完整混合 PDF；NEE、环境命中和 continuation 的 MIS 同步更新，并保留 BSDF/HDR 分量的路径支持。训练直方图的定点量化和截断只改变提议分布，不截断最终引导贡献。全程关闭自适应和缓存时，guiding 目标仍是原有的有限深度积分。

缓存复用训练得到的 **BSDF 延续贡献除以 albedo 的 RGB 均值**，当前表面再乘自身 albedo。它不是完整方向 radiance field，也不是严格的 irradiance 插值：不同视角、材质和同一分区的表面会造成近似误差。只对 metallic < 0.05、roughness ≥ 0.5、深度和记录数满足门槛的表面终止；当前顶点的直接 NEE 保留原提议及 MIS，避免与训练的延续分量重复计算。玻璃不参与缓存终止。

缓存的定点贡献上限约为 16，空间/法线合并、截断和提前终止都有偏差，可能平滑细节、漏光或遗漏稀有光路。它仅作为显式的预览模式，不能代替固定 spp 参考。Guiding 同样需要足够训练和渲染预算来抵消开销，当前表还没有自适应空间/方向细分。

GPU ABI 已扩展：材质 128 字节（含 optics、absorption 与 scattering）、参数 288 字节，学习槽 544 字节；其余基础几何和像素布局沿用 [GPU PT 说明](path-tracing-gpu.md)。

## BDPT 与平滑玻璃

`BidirectionalPathTracer.cpp` 独立生成相机/光源子路径，枚举深度范围内的连接策略，包括 `s=0` 相机路径命中光源、`s=1` 面积光连接、内部连接，以及 `t=1` 光路径连接针孔相机。连接进行可见性测试，以几何项、BSDF 和子路径 throughput 求值。针孔相机有方向 PDF、投影及独立的 film splat，每次迭代为每个像素生成一条 light path：总数为像素数 × spp，而目标像素响应带像素数因子，两者抵消后 film splat sum 除以 spp。不能按落入某像素的路径数量除。

MIS 使用非 delta 边的面积密度和 power heuristic，在 log 域计算以避免深路径乘积下溢。delta 边使用共同的离散占位，排除连接端点为 delta 的无效策略，策略权重形成 partition of unity。固定深度内不使用 roulette，以避免漏掉生存概率。普通 PBR 的光源子路径应用 shading-normal adjoint 修正。

平滑玻璃通过 `CpuScene(snapshot, dielectricOverrides)` 的 PT 专用对象 ID / IOR 覆盖声明，CPU 和 GPU 单向 PT 均支持。反射/折射使用精确介电 Fresnel、Snell 和 TIR；radiance 模式包含 eta²，importance 模式不包含该因子。颜色在每次透射边界作为 tint，尚未实现体内 Beer 吸收、介质栈、色散或粗糙玻璃。实时透明材质没有自动转换为这种闭合玻璃。

BDPT 首版只验收有限面积光源、针孔相机、基础 PBR 和平滑玻璃；HDR、太阳、点/spot、blended coverage 和指导/缓存组合明确拒绝。alpha mask 可沿用求交，但这轮焦散验收使用不透明接收面和闭合玻璃。CPU film/splat 工作内存超过 1 GiB 会拒绝请求，需降低分辨率或 worker 数。GPU BDPT、硬件 RT、焦散专用 manifold sampling 尚未实现。

## 验证与实测

CPU 测试覆盖 Fresnel 4% 正入射、TIR、delta 概率、radiance/importance 能量、相机投影、面积光 BDPT/PT 能量对照、真实 specular-to-diffuse 焦散 AOV 和无玻璃零焦散对照。GPU 测试覆盖混合方向提议的能量一致性、缓存实际命中、空训练表回退和玻璃与 CPU 的积分对照；原有 BVH、材质、光源和采样测试保留。

本机 Apple M4，Metal，320×240、16 次散射、固定 spp、64 spp 独立训练。优化模式渲染 256 spp，与同一次普通 PT 的最近耗时 checkpoint 对照；参考为独立 seed=7 的 1024 spp PFM。时间包含训练、冻结表和 checkpoint 输出，模型导入/BVH 不在表内。以下是近似同耗时的开发机测量，不是无噪声 ground truth。

| 场景 / 模式 | 训练 + 追踪（秒） | 普通 PT 对照 spp / 秒 | 全图 RGB MSE 比 | 阴影 ROI Y MSE 比 |
| --- | ---: | --- | ---: | ---: |
| Sponza guiding | 7.353 | 336 / 7.622 | 1.0046 | 1.2285 |
| Sponza cache | 7.096 | 304 / 6.913 | 1.0307 | 1.1302 |
| Sponza guiding + cache | 7.193 | 304 / 6.913 | 0.9693 | 1.1271 |
| San Miguel guiding | 13.712 | 368 / 14.077 | 1.0081 | 1.0096 |
| San Miguel cache | 12.289 | 336 / 12.863 | 0.9989 | 0.9964 |
| San Miguel guiding + cache | 12.248 | 304 / 11.653 | 0.9924 | 0.9993 |

比值低于 1 表示该指标更好。Sponza 阴影 ROI 为 `(164,150)–(216,220)`，San Miguel 为 `(175,155)–(290,225)`；两者均为左上原点。Guiding 命中分别约 1121 万和 122 万次；单独 Cache 命中约 250 万和 28 万次。训练约 1.4 / 2.4 秒，不能忽略。当前优化没有稳定改善两场景的阴影同耗时误差，因此继续保持显式启用；不能宣称已解决所有暗部噪声或提高了正确解的平均亮度。

复测工具：`tools/benchmark_pt_convergence.py`。原始报告位于 `build/path-tracing/convergence-verified/*-benchmark.json`，记录命令、wall time、训练/追踪、命中及线性误差；参考图路径也写在报告中。

玻璃焦散已渲染 640×480、512 spp、8 次散射、8 workers 的真实 BDPT 图，耗时 69.425 秒，原始 caustics AOV 占整图能量约 5.438%。面积光基础测试中 BDPT/PT 能量比为 0.99805；无玻璃场景的焦散分类 AOV 为零。

![BDPT 焦散](../img/path-tracing/bdpt-caustics.png)
![独立焦散路径贡献](../img/path-tracing/bdpt-caustics-only.png)

最终输出另接入 [Open Image Denoise](path-tracing-denoising.md)，但上述所有收敛指标均来自未经降噪的 PFM。

Metal 全套 15/15、Vulkan/MoltenVK 全套 16/16 通过，Metal 开启 API/Shader Validation。CPU ASan/UBSan 通过；OIDN 的测试包含关闭依赖的构建。


参考：[Practical Path Guiding](https://cgl.ethz.ch/publications/papers/paperMue17a.php)、[BDPT 推导](https://www.pbr-book.org/3ed-2018/Light_Transport_III_Bidirectional_Methods/Bidirectional_Path_Tracing)、[Radiance Caching](https://gpuopen.com/download/publications/GPUOpen2022_GI1_0.pdf)。当前实现是独立的简化版本，不是上述论文完整实现或厂商 SDK。

新增 Stanford Dragon 玻璃 BDPT 及不透明对照，保留真实焦散 AOV；资源与复现见 [Dragon／程序化 PT](path-tracing-procedural.md)。开放 FFT 水面已支持 CPU/Metal/Vulkan 单向 PT，但 BDPT 的介质连接权重尚未实现，含水面的请求会明确拒绝。

水体／玉石的 RGB 体积输运、HG 和介质栈现已接入同一 CPU／Metal／Vulkan 内核，见 [随机游走 BSSRDF](path-tracing-subsurface.md)。体积场景暂禁用 GPU guiding/cache；surface BDPT 尚未覆盖体积策略和 MIS。
