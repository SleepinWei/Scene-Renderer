# 原生后处理模块

2026-10-08。实时 Raster 管线通过 `GpuPostProcessor` 统一执行 HDR 到显示图像的转换，覆盖 Metal／Vulkan 的 Forward、Deferred 和 Scene 路径。编辑器新增 **Post processing** 面板；额外效果默认关闭，默认指数色调映射、相机曝光和 gamma 保持原行为。

## 效果与操作

```sh
./build/Scene-Renderer --classic cornell --size 1280x720
```

展开 **Post processing**，各效果均有独立开关和参数。**Soft cinematic preset** 开启 ACES fitted、弱 Bloom、轻微降饱和与暗角；**Reset post effects** 恢复默认。总开关关闭时回到原指数色调映射，保留相机曝光及全局 HDR 开关。

| 效果 | 方法与参数 |
| --- | --- |
| Bloom | HDR 软阈值提取，最多 6 级降采样，归一化上采样；阈值、软拐点、强度、扩散 |
| 景深 | 不透明深度重建视空间距离，32 点圆盘 gather、背景深度抑制；焦距、焦区、像素半径 |
| 相机运动模糊 | 当前深度重建位置，再投影到上一帧相机；12 点限长、深度加权采样，快门比例和最大像素长度 |
| 色调映射 | 原指数映射、ACES fitted、Reinhard、线性截断；额外 EV 与相机曝光相乘 |
| 调色 | RGB 增益估计冷暖／绿品红偏移，饱和度与对比度；不是 LUT 或完整 ACES 色彩管理 |
| FXAA | 显示空间亮度边缘判定与沿边缘采样，可与 TSAA 独立组合 |
| 锐化 | 四邻域 unsharp mask，使用局部范围限制过冲 |
| 暗角 | 椭圆径向平滑衰减；强度与形状 |
| 色差 | 屏幕径向红／蓝偏移，以像素为单位，在 FXAA／锐化前处理 |
| 颗粒 | 显示空间确定性噪声，按模拟时间以 24 Hz 更新；暂停模拟时保持不变 |

景深焦距使用视空间前向距离；`focusRange` 的一半以内保持清晰，之后平滑过渡到最大半径。运动模糊的快门参数是相对于相邻提交相机位移的比例，不是秒数。按住鼠标右键转头，W/A/S/D 移动，Q/E 垂直移动。

## 实际渲染对照

以下为 960×720、固定相机与 8 秒场景时间、RSM 开启、TSAA 关闭的 Metal 图集。每种模式渲染 16 帧；镜头、场景和线性 HDR 光照保持相同。组合图包含 ACES、冷暖／饱和度调整、Bloom、FXAA、锐化、暗角和颗粒；景深图单独展示前方箱体清晰、后方渐渐模糊。

| 默认显示 | Bloom |
| --- | --- |
| ![默认指数映射](../img/diagnostics/post-processing/cornell-post-off.png) | ![HDR Bloom](../img/diagnostics/post-processing/cornell-bloom.png) |

| 景深 | 组合后处理 |
| --- | --- |
| ![前方箱体焦区](../img/diagnostics/post-processing/cornell-dof.png) | ![ACES 与组合显示效果](../img/diagnostics/post-processing/cornell-post-combined.png) |

[单独调色](../img/diagnostics/post-processing/cornell-grade.png) · [捕获设置与 GPU 时序](../img/diagnostics/post-processing/post-process-metrics.json)。图集时序记录主渲染提交，阶段区间包含等待且可能重叠，不能求和或当作后处理净成本；本图集不是隔离的性能基准。

## 管线与资源

```text
场景／水体／云的线性 HDR
  → TSAA（已有独立开关）
  → 相机运动模糊（可选）
  → 景深（可选）
  → HDR Bloom 金字塔（可选）
  → 曝光、白平衡增益、色调映射、饱和度／对比度、gamma
  → 色差、FXAA、锐化、暗角、颗粒（可选）
  → RGBA8 输出 → 编辑器 UI → 呈现
```

`PostProcessSettings` 是随不可变 `FrameData` 传递的值配置；逻辑线程只更新配置，GPU 资源与 pass 由渲染线程维护。`GpuPostProcessor::record` 接收调用方的 HDR、深度和输出视图，不拥有场景，不进行每帧 CPU 像素回读。`commit` 仅在提交成功后发布相机历史，避免失败帧污染下一帧。

Bloom／镜头／显示中间纹理按需创建，效果关闭后通过 RHI 延迟退役释放；全关闭时没有额外图像缓存，也只执行一次显示转换。需要 FXAA 等显示效果时才创建全分辨率 RGBA16F 中间结果。Bloom 的降采样使用 ceil 尺寸，支持奇数尺寸和 1 像素末级，上采样按归一化权重混合，避免平坦亮区随层数额外增亮。

Bloom、景深与运动模糊在 HDR 空间处理，保持高光能量；屏幕效果在 gamma 后处理。`readHDR()` 仍返回 TSAA 后、后处理前的线性图像，因此这些视觉开关不会改变照明、路径积分或 HDR 诊断结果。

## 验证与边界

```sh
./build/Scene-Renderer --post-process-self-test
./build-underwater-vulkan/Scene-Renderer --post-process-self-test
SCENERENDERER_DISABLE_PIPELINE_DISK_CACHE=1 SCENERENDERER_GPU_PROFILE=1 \
  ./build/Scene-Renderer --render-gallery img/diagnostics/post-processing post-gallery
```

Metal API／Shader Validation 与 Vulkan 自检覆盖默认显示的独立 CPU 指数／gamma 参考、HDR 开关、Bloom 高光扩散／平坦能量／释放、不同色调映射、清晰焦平面与散焦、静止／移动／重置的相机模糊、TSAA 抖动去除、独立显示开关与总开关、动态颗粒、组合效果、奇数及 3×1 重建，以及非法数值拒绝。[Metal 验证](../img/diagnostics/post-processing/metal-validation.txt)、[Vulkan 验证](../img/diagnostics/post-processing/vulkan-validation.txt) 与 [水体／TSAA 回归](../img/diagnostics/post-processing/water-regression.txt) 日志保存于图集目录。

编辑器 [独立渲染线程](../img/diagnostics/post-processing/editor-smoke.txt) 和 [单线程](../img/diagnostics/post-processing/single-thread-smoke.txt) 路径均完成 8 帧启动检查；线程路径无 publication 拒绝或 fallback。

这是实时屏幕空间近似：景深不是多层 bokeh 重建；水、玻璃和云沿用不透明深度，可能在交界处出现错误虚化。运动模糊目前只覆盖相机运动，不读取物体速度，也不模拟天空的旋转拖影。色差和颗粒用于外观调节；没有自动曝光、LUT 调色或镜头畸变。本模块尚未接入单独的渐进 PT 视口输出，离线 PT 的 OIDN 与线性检查点继续使用自己的流程。
