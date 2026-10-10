# 海面远景太阳与天空反射积分

本页记录开发构建结果；验收记录保留当时的构建与源码哈希，复现命令需使用对应构建。

2026-10-10：继续修正[浮点法线与 glint 重建](ocean-float-normals-and-glints.md)中的远景亮带、细碎黑带。保留同一套 FFT、曝光、太阳强度、角半径与水体参数，改进光滑远海的反射积分。仍未完全对齐 PT。

## 分量定位

`--pt-compare-ocean` 支持 `SCENERENDERER_OCEAN_REFERENCE_AOV`，可选择 `beauty`、`body`、`reflection`、`sun`、`foam`、`border`、`downward-reflection`。前五个非 beauty 辐射分量包含实际泡沫和边界混合权重，在水面覆盖区域相加还原完整线性 HDR。每张图独立经过 RGBA16F 目标，因此验收检查相对误差小于 0.2%，不能要求任意高亮像素的绝对误差小于 10⁻⁴。

此开关只隔离实时海面，PT 始终输出完整光传输；水面以外的天空保持原输出。方向图统计的是**中心法线**的向下反射比例，不是过滤后分布的概率。TAA 的拒绝和权重依赖颜色，不要求独立累积后的五张图仍严格可加。

修正前的单帧水域平均 Y：太阳 **0.20349**，天空反射 **0.03710**，水体 **0.00868**，泡沫及边界接近零。太阳占约 82%；中心法线约 1.46% 的反射朝下。天空反射分量本身已有细碎黑带，不能把所有差异归因于太阳亮点抗锯齿。

| 修正前天空反射分量 | 修正后天空反射分量 |
|---|---|
| ![旧天空反射](../img/path-tracing/ocean-reflection-before-reflection.png) | ![积分天空反射](../img/path-tracing/ocean-reflection-after-reflection.png) |

## 远景太阳

此前把斜率协方差在线性化的中心法线处映射到反射角度，再用高斯近似太阳响应。掠射视角下，反射变换的非线性与 Fresnel 的相关性使远景出现过宽亮带。

现在在太阳方向对应的半向量处求斜率密度。令 `H = normalize(V + L)`、`s_h = H.xz / H.y`，斜率分布为 `p_s`，则反射方向的密度为：

```text
p_r(L) = p_s(s_h) / [4 (V·H) H.y³]
```

使用这个准确的反射 Jacobian、对应半向量的 Fresnel，以及 16 个均匀球面太阳帽积分点。这里的斜率分布描述每个可见像素内的法线分布，没有额外插入微表面面积投影或 Smith 项。太阳帽使用与已有水面模型一致的辐照度归一化。

新闭合用于 footprint 大于 4 的区域，在 4–8 之间与既有空间法线积分平滑混合。近景仍使用上一阶段的 box/tent glint 重建。它仍假设远景斜率是高斯分布；准确的变换 Jacobian 不意味着多峰波面分布、遮挡或海面多次反射已经准确。

## 天空反射

此前只计算 `F(mean normal) × sky(reflected mean normal)`。现在对 **Fresnel 与天空辐射的乘积**积分：解析波面使用 4×4 空间样本，未解析斜率使用 4×4 高斯求积，并使用相同分布的平均 Fresnel 给水体项分配透射比例。法线扰动造成的离散反射边缘更柔和。

向下的反射仍采样原方向；没有把它强行翻向天空。这个阶段没有求解海浪之间的反射、遮挡或完整水体多次散射。粗糙水面、近岸水面和命中不透明反射对象的像素保留原路径。复用现有纹理与参数缓冲，没有增加 sampler 或矩 mip 内存。

## 同构建验收

```sh
python3 -B tools/diagnose_ocean_components.py --render --angular-moments \
  --directory build/ocean-reflection-before
python3 -B tools/diagnose_ocean_components.py --render \
  --directory build/ocean-reflection-after
python3 -B tools/collect_ocean_reflection_validation.py \
  --before build/ocean-reflection-before --after build/ocean-reflection-after
```

640×360、time=8、513² 网格，原始浮点主波与细波法线。同一二进制与编译 shader，通过开关比较旧角度高斯/中心天空与新斜率积分/天空积分。两组原始 **512 spp PT film 逐位一致**。数值比较使用去噪前 PT；OIDN 仅用于展示。L1 包含有限采样噪声与窄高光位置差异，梯度下降也不能独立证明没有 aliasing。

| 实时路径与原始 512 spp PT 比较 | 全水域 L1 | 远景 L1 | 两侧海水 L1 | 近景 L1 |
|---|---:|---:|---:|---:|
| 旧模型单帧 | 123.16% | 171.24% | 54.27% | 82.46% |
| 新模型单帧 | 107.24% | 134.73% | 44.44% | 82.39% |
| 旧模型 TAA | 129.66% | 171.88% | 54.73% | 94.07% |
| 新模型 TAA | 113.49% | 134.01% | 43.56% | 94.04% |

近景误差基本未变；新模型主要改善远景太阳项和天空反射。整体误差仍较高，不能宣称实时海面已对齐 PT。新旧模型的太阳/反射/水体分量、同步 renderer wall 时间、原始图像哈希、构建及 shader 哈希、源码哈希、回归日志见[验收 JSON](../img/path-tracing/ocean-reflection-validation.json)。

本机这一组 20 帧、去掉前 4 帧的同步 renderer wall 中位数：旧模型单帧/TAA 为 **50.03 / 53.09 ms**，新模型为 **53.09 / 52.54 ms**。包含 FFT、矩 mip、渲染和 GPU 等待，不含 presentation/导出。单帧约增加 3 ms；TAA 的小幅时间差不能当作稳定性能提升。新积分避免在已完全解析的近景执行多余的远景求积。

Metal 和 Vulkan/MoltenVK 的 Ocean/Water 回归通过，包括时域水面累积与历史拒绝。新增数值测试用独立的双精度有限差分反射 Jacobian 验证太阳积分，覆盖视角高度参数 0.02、0.1、0.7；最大相对误差约 **0.027%**。这些测试不是 Vulkan 原生硬件 RT 验证。

| 新实时海面 TAA | 同快照 Metal 原生 RT + OIDN |
|---|---|
| ![新实时海面](../img/path-tracing/ocean-reflection-after-raster-taa.png) | ![同快照 PT](../img/path-tracing/ocean-reflection-pt-denoised.png) |

[旧模型 TAA](../img/path-tracing/ocean-reflection-before-raster-taa.png) · [新模型单帧](../img/path-tracing/ocean-reflection-after-raster.png) · [原始 PT](../img/path-tracing/ocean-reflection-pt.png)。

消融开关：`SCENERENDERER_WATER_ANGULAR_MOMENTS=1` 恢复旧角度高斯与中心天空；`SCENERENDERER_WATER_CENTER_SKY=1` 保留新太阳积分、恢复中心天空。它们不改变 PT 输入。

后续优先解决波面反射可见性与几何/光学法线的一致性，再处理未解析波面的非高斯分布和水体散射。近景 box/tent 的锐度、成本与 PT 像素误差取舍仍保留在上一阶段文档中。
