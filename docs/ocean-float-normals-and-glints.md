# 海面浮点法线与 glint 重建

本页记录开发构建结果；验收记录保留当时的构建与源码哈希，复现命令需使用对应构建。

2026-10-10：延续[海面 glint 抗锯齿](ocean-glint-antialiasing.md)与[实时/PT 对照](ocean-realtime-pt-comparison.md)。本阶段统一海面的光学输入，并处理近景亮点跨像素边界及冻结海面的时域累积。远景斜率矩闭合、海面自身反射和水体散射仍未完全对齐。

本页保留这一阶段的构建、图像与数据。后续[远景太阳和天空反射积分](ocean-reflection-integration.md)已加入分量诊断和反射积分修正，当前默认模型与新图像以该页为准。

## 原始 FFT 法线

`freezeOcean` 不再把主波与细波合成为 RGBA8 光学法线贴图。`OceanNormalFields` 保留两张原始 RGBA32F 场和独立 UV 周期，CPU/GPU PT 都先对各自场做周期双线性采样、归一化，再相加斜率。海况样例保留 **1024² / 256 m 主波、256² / 32 m 细波**，不受 `CaptureOptions.textureExtent` 的材质烘焙尺寸限制；该选项仍控制泡沫和其他材质贴图。

UV 由冻结网格的未位移材质坐标恢复，并处理 V 方向与 FFT 网格点对应的半 texel 偏移，匹配实时 `waterWaveUV`。新字段仅用于冻结海面；已有普通材质、包内法线贴图和旧水体法线输入继续使用原路径。

GPU 共用原来的 image descriptor 与 uint texel 缓冲：`images.w=0` 为打包 UNORM8，`images.w=1` 为每 texel 四个 float bit word。`PackedMaterial` 尾部增加独立海面字段，CPU/GLSL std430 ABI 为 **288 bytes**。不可变纹理缓存同时保留两种源的所有权并比较身份。

导出 JSON 的 `frozen_oceans` 记录 `normal_encoding`、`normal_grid`、`normal_uv_scales` 与 `normal_sampling`。这会改变 PT 参考本身，旧阶段的 8 位法线结果继续保留，不能把新参考与旧参考的误差直接当作渲染算法提升。

## glint 与静态累积

近景/中景解析太阳反射在**线性 HDR** 下做单位面积的 separable tent 重建：光滑水面 8×8 样本，粗糙水面 4×4 样本。窄 glint 的响应连续跨过像素边界，再进行曝光映射；没有改太阳强度、角半径、曝光或水面粗糙度。它改变了像素重建滤波器，不能把相对 box 参考的所有差异都视为误差或改善。

此前冻结场景的水面 TAA 每轮都重采样 current/history，反复混合高光与邻近像素。现在在时间与视图不变、符合物理水面历史资格的区域，抖动样本直接累积到同一输出像素。仍保留原运动出屏、深度和 reactive 拒绝；动态水面与移动相机保留原有重投影路径。固定海面使用这一路径不等同于验证了所有动态拖影情形。

消融开关：

```sh
# 旧 4×4 box 太阳重建
SCENERENDERER_WATER_GLINT_BOX=1 build/pt-native-rt/runtime/Scene-Renderer \
  --classic ocean-sea-state --backend Metal

# 保留前一阶段固定水面 current/history 重采样
SCENERENDERER_WATER_TAA_RESAMPLE=1 build/pt-native-rt/runtime/Scene-Renderer \
  --classic ocean-sea-state --backend Metal
```

实时矩 mip 的内存成本仍约 68 MiB。PT 浮点法线增加独立 CPU 数据及 GPU 上传存储；新增主波+细波 float texels 约 17 MiB，替代原先约 4 MiB 的合成法线 texels。实际场景内存和帧成本以导出报告为准。

## 验收

CPU 两项 CTest 通过，包括独立周期/朝向/采样相位、细波周期、浮点精度、GPU 按位打包、缓存复用和非有限值拒绝。完整原生 Metal PT 回归通过，冻结海面 CPU/GPU 相对 L1 为 **3.26×10⁻⁶**。Metal/Vulkan Ocean 与 Temporal 回归通过；新增时域测试用抖动的亮点和相反亮度的邻居检验同一像素的 HDR 平均值，保留出屏与 reactive 拒绝。

额外的 Vulkan 计算 PT / CPU 对照使用同一 Vulkan FFT 捕获、32×24、128 spp、depth=6、33² 网格，原始 RGB 相对 L1 为 **0.205%**，两者无非有限样本。这是小规模正确性探针，不是 Vulkan 原生硬件 RT 或性能对比。

```sh
python3 -B tools/validate_ocean_surface_alignment.py --render \
  --directory build/ocean-surface-alignment-final
```

脚本使用同一构建、同一快照、640×360、time=8、513² 网格，分别捕获 box/tent 单帧与 TAA，并比较两套 4× 分辨率的线性面积平均及未去噪的 512 spp PT。两套实时滤波对应的 PT film 要求逐位一致。四组命令、环境开关、二进制/编译 shader/源码/原始图像哈希和日志保存在[验收 JSON](../img/path-tracing/ocean-alignment-validation.json)。OIDN 图只作展示，原始 PT 用于数值比较；4× 光栅参考不代表收敛的光传输 GT。

| 实时 glint 重建 | 同快照 PT + OIDN |
|---|---|
| ![实时海面](../img/path-tracing/ocean-alignment-tent-raster-taa.png) | ![浮点法线 PT](../img/path-tracing/ocean-alignment-pt-denoised.png) |

[旧 box TAA](../img/path-tracing/ocean-alignment-box-raster-taa.png) · [tent 单帧](../img/path-tracing/ocean-alignment-tent-raster.png) · [4× tent 参考](../img/path-tracing/ocean-alignment-tent-ssaa4.png) · [原始 PT](../img/path-tracing/ocean-alignment-pt.png)。

## 同快照结果与取舍

box/tent 两组原始 512 spp PT film **逐位一致**，实时重建不改变 PT 输入。相对新浮点法线参考的误差如下；参考未经过 OIDN，有限采样与窄太阳亮点的位置会放大逐像素误差。

| 与原始 512 spp PT 比较 | 全水域 L1 | 近景 L1 | 两侧海水 L1 |
|---|---:|---:|---:|
| box 单帧 | 108.82% | 60.46% | 54.27% |
| box TAA | 120.58% | 79.48% | 54.73% |
| tent 单帧 | 123.16% | 82.46% | 54.27% |
| tent TAA | 129.66% | 94.07% | 54.73% |

**tent 并没有改善相对未滤波 PT 的像素 L1。** 它改变了像素重建核，以平滑亮点边界为代价降低锐度。相比同构建 box TAA，太阳 ROI 的相邻像素亮度梯度从 0.71280 降到 0.62387（约 12.5%），平均 Y 为 0.70364 / 0.70272（约 -0.13%），近景平均 Y 为 0.17852 / 0.17875（约 +0.13%）。梯度只是边缘平滑的描述，不能单独证明消除了 aliasing。两侧海水没有因 glint 滤波获得改善。

加入固定像素累积前的同一套浮点法线/tent 结果保存在 `build/ocean-surface-alignment/first-validation.json`。保持 box 时，近景相对 4× box 面积平均误差由 59.65% 降到 48.94%；保持 tent 时由 75.02% 降到 69.68%。这说明反复重采样的修正有作用，但 TAA 与单帧参考仍有偏差，不能宣称重建完全收敛。

本机这一组 20 帧、去除前 4 帧的同步 renderer wall 中位数：box 单帧/TAA **45.06 / 43.59 ms**，tent **50.52 / 51.43 ms**，增加约 5–8 ms。包含 FFT、矩 mip、渲染与 GPU 等待，不含 presentation/导出；一组捕获不是稳定 FPS 基准。性能与锐度取舍可用 box 开关验证，默认海面使用 tent。

后续的[分量诊断与反射积分修正](ocean-reflection-integration.md)已交付；波面可见性与水体散射仍待完善。静态 glint 的重建改善不能替代这些光学模型工作。
