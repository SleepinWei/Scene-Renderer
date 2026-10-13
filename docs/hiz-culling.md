# 当帧 Hi-Z 物体遮挡裁剪

本轮为实时 `ForwardPbrRenderer` 增加可选的 GPU 遮挡裁剪。视锥内、完全被静态不透明遮挡物挡住的物体，其主视角 indexed indirect `indexCount` 变为零。正常渲染不读回可见性，也不等待上一帧查询。

## 管线

1. 原有物体视锥裁剪先缩短候选列表；原始列表保留给阴影和水体辅助视角。
2. CPU 根据保守投影面积筛选较大的静态遮挡物，按包围体最近深度排序，受数量和三角形预算限制。
3. 使用当帧实际 VP（含 TAA jitter）绘制独立、全分辨率的 depth-only 遮挡物。复用与主材质一致的 alpha cutoff／虚拟纹理采样，保留透明孔洞；不改变主场景 depth attachment。
4. 使用 8×8 compute 将标准 0..1 深度复制到 R32Float mip 0，各 mip 取覆盖区域的 **最大深度**，避免逐层 fullscreen graphics pass。Clear depth=1；天空空隙和奇数分辨率尾行／尾列必须参与归约。
5. GPU 批量测试物体的保守屏幕矩形：物体最近深度必须严格大于覆盖区域的最远遮挡深度加余量，才可剔除。 mip 层级按矩形大小选择，覆盖到的全部格子都参与比较。
6. 同一 GPU command list 输出间接参数；geometry、forward replay、motion 使用这个结果。CPU 仍录制这些 indirect commands，但零 count 的物体不执行实际几何绘制。

RDG 增加 `hiz-visibility` 依赖节点；原生 GPU 时间戳分别标记 `hiz/occluder-depth`、各个 `hiz/pyramid-*`、`hiz/object-tests` 和可选的 `hiz/triangle-composition`。

GTAO 现有金字塔存储最近有效线性深度，忽略空白像素；这与保守遮挡的要求不同，不能直接作为本轮的 Hi-Z 数据。这里使用独立金字塔和空隙语义。

## 保守性和几何范围

投影使用 double 计算，并为 vertex shader 两次 float 矩阵乘法的舍入／大世界消去误差扩展边界；屏幕边界增加两个像素，深度比较使用正余量。近裁面或眼平面相交、未知／非有限包围体无法可靠投影时保留。选为遮挡物的物体强制保留，避免自遮挡剔除。

只裁剪已发布的静态普通网格。透明、SSS、wireframe、实例、GPU 生成网格和 shoreline 特殊材质保留原路径；FFT 波面不作为遮挡物。另有可开关的 [当帧 cluster 级遮挡](hierarchical-cluster-geometry.md)，使用当前 LOD 的间接绘制建设预深度。实例 compact 和海面 tile 裁剪留待后续接入。

阴影、RSM、反射、宽空气折射和海底捕获继续使用完整源几何及各自的可见性。主相机的遮挡结果不能复用于其他视角。TAA 模型历史继续维护源对象身份，物体移动／遮挡物移动／相机切换时使用当帧数据，不受上一帧零 count 影响。

启用三角形裁剪时，Hi-Z 输出与三角形 indirect arguments 在 GPU 上组合，写入独立的最终参数；不会污染可复用的三角形裁剪缓存。可见性恢复后仍能恢复正确的三角形数量。

Scene 的 cluster 遮挡现默认采用历史早期筛选／当帧晚期恢复，直接复用 G-buffer 深度；没有专用 depth prepass，也没有物体级 object test。该模式 `readHiZDepth()` 返回实际当帧早期金字塔，`readClusterHistoryDepth()` 返回本帧使用的历史金字塔，最终数量／阶段集合分别使用 `readClusterCullCounts()`／`readClusterVisibility()`；物体 `readHiZCullCounts()` 为空。[两阶段实现与验证](cluster-two-stage-hiz.md)。

## 使用和诊断

默认关闭，先在目标场景测量遮挡收益和预绘制成本：

```cpp
frame.hizCulling = true;
frame.hizOccluderMinArea = .04f;  // 保守投影面积占 viewport 的比例。
frame.hizMaxOccluders = 32;
frame.hizOccluderTriangleBudget = 250000;
renderer.render(frame, packets);
auto cpu = renderer.hizCullStats();
auto gpu = renderer.readHiZCullCounts(); // 同步诊断，勿用于每帧绘制决策。
```

```sh
SCENERENDERER_ENABLE_HIZ_CULLING=1 ./build/Scene-Renderer --demo
SCENERENDERER_DISABLE_HIZ_CULLING=1 ./build/Scene-Renderer --demo
```

关闭变量优先于开启变量及 FrameData。compute／storage image／mip view 不受支持、没有合适遮挡物、所有候选都已选为遮挡物，或者目标分配超过 RHI 资源预算时回退原始路径。可见性 buffer 和纹理按尺寸／容量复用，关闭功能释放可选 culler。

`hizCullStats()` 返回是否实际执行、候选／遮挡物数量、prepass 三角形、mip 数和额外资源大小。`readHiZCullCounts()` 返回 CPU 视锥裁剪后 packet 对应的原始／最终三角形数量及遮挡物标记。启用旧单阶段 cluster Hi-Z 时，`readHiZCullCounts()` 仍是物体阶段诊断，不能代表最终 meshlet 输出；最终数量使用 `readClusterCullCounts()`。`prepassTriangles` 是选中源网格的三角形预算用量，cluster 的实际预绘制数量由当前切面决定。CPU 统计无法提前给出 GPU 剔除数量；读回只供自检／诊断使用，且只有主 command list 成功提交后才发布诊断结果。

## 验证

```sh
cmake --build build --target Scene-Renderer rhi-vulkan-validation -j 6
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer --hiz-culling-self-test
./build/Scene-Renderer --hiz-culling-self-test --backend Vulkan
SCENERENDERER_GPU_PROFILE=1 SCENERENDERER_HIZ_REPORT=build/hiz-timing.json \
  MTL_DEBUG_LAYER=0 MTL_SHADER_VALIDATION=0 ./build/Scene-Renderer --hiz-culling-self-test
```

原生自检对照关闭／开启结果，覆盖完全遮挡、前景、部分露出、alpha 孔洞、几何细缝、奇数分辨率尾列、深度相等、near／eye 相交、屏幕 Y 方向、移动物体／遮挡物／相机、大世界坐标、三角形缓存组合、源阴影、forward replay、resize 和在途 TAA 帧。

局部性能测量必须计入选中遮挡物 depth pass、完整 pyramid 和 GPU tests，不能只报主 geometry 的缩短。小场景或开放海面可能因为额外 passes 而变慢；这也是当前不自动开启的原因。

Metal API／Shader Validation 和 Vulkan/MoltenVK 的 Hi-Z 专用测试均通过，22 组图像对照 HDR 最大误差 **0**、显示像素完全相同；CPU 渲染图／并发／RHI 契约测试 **4/4** 通过。Metal 强制启用 Hi-Z 的水体回归和 Vulkan 完整图形回归通过，包含屏外镜面物体、宽空气折射、Snell/TIR、海底体积、焦散／太阳光束和 TAA。

密集遮挡验证场景（129×97）有 64 个静态密集网格藏在一个两三角形的遮挡平面后：

| 项目 | 关闭 Hi-Z | 开启 Hi-Z |
| --- | ---: | ---: |
| 非零主视角绘制 | 65 | 1 |
| 主视角三角形 | 524,290 | 2 |
| 遮挡物 prepass 三角形 | 0 | 2 |
| geometry＋完整 Hi-Z 阶段中位数 | 0.499 ms | 0.372 ms |

计时在 Apple M4／Metal、验证层关闭时记录，3 帧预热、8 帧采样；计入 depth prepass、全部 mip 和 GPU 测试。阶段样本波动明显：基线 0.380–0.682 ms，Hi-Z 0.056–1.398 ms。中位数降低约 25%，不能据此保证其他场景或稳定整帧 FPS 的提升。保留默认关闭，建议在真实遮挡场景测量后开启。

[Metal 验证结果](../img/diagnostics/hiz-culling/metal-validation.json)、[Vulkan 验证结果](../img/diagnostics/hiz-culling/vulkan-validation.json)、[原始阶段时间戳样本](../img/diagnostics/hiz-culling/metal-timing.json)。Vulkan 当前本机未安装 Khronos validation layer；Metal 验证层已开启。

[回归结果与证据摘录](../img/diagnostics/hiz-culling/regressions.json)。


<a id="visualization"></a>
## 可视化和复现

![Hi-Z GPU 可见性、开关对照与深度层级](../img/diagnostics/hiz-culling/hiz-overview.png)

这是一组 **960×720 的独立静态测试场景**，不等同于前文 129×97 的密集网格性能基准。窗口墙体是单个遮挡物，墙后 64 个立方体和墙前物体用于展示完全遮挡、孔洞可见性及保守保留。实际 GPU arguments 显示 52 个物体被剔除，非零主绘制 69 → 17，主三角形 864 → 240，预绘制额外 48 个三角形；显示像素相同、HDR 最大误差 0。

红／绿／青色矩形使用 culler 实际保存的保守屏幕边界和 GPU 输出计数，分别表示剔除、保留和遮挡物；跨 near／eye、无法可靠投影的边界保留但不画框。左下图通过再次渲染并移除选中的遮挡物显示墙后几何，属于独立参考视图。

![实际 GPU 最远深度金字塔](../img/diagnostics/hiz-culling/hiz-pyramid.png)

所有 mip 均来自实际 R32Float texture subresource 的 GPU 读回。绘图脚本用独立 CPU max reduction 检查 9 次层级转换，包含奇数尺寸 30×22 → 15×11 → 7×5 → 3×2 → 1×1；所有像素逐值一致。原生 clear depth 1 显示为白色，窗口空隙逐层保守扩张。深度颜色仅为显示而统一拉伸，不改变原始 GPU 值。

[可见性、边界和数量元数据](../img/diagnostics/hiz-culling/capture/occlusion-window-capture.json)、[无损 float32 mip 归档](../img/diagnostics/hiz-culling/capture/occlusion-window-depth.npz)、[绘图验证结果](../img/diagnostics/hiz-culling/visualization-checks.json)。

```sh
SCENERENDERER_HIZ_CAPTURE_DIR=img/diagnostics/hiz-culling/capture \
  MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer --hiz-culling-self-test
python3 tools/visualize_hiz.py img/diagnostics/hiz-culling/capture \
  --output img/diagnostics/hiz-culling --archive-depth
```

Python 绘图需要 NumPy 和 Pillow。`--archive-depth` 保存逐值验证的压缩 NPZ；脚本既可读取新捕获的 `.f32`，也能从归档重建图像。Vulkan 可在自检命令末尾增加 `--backend Vulkan`，建议指定独立捕获目录。

新增 `readHiZDepth(mip)` 和 `hizCullBounds()` 仅供离线诊断；普通交互渲染不调用 GPU 读回。截图时关闭 TAA 和三角形裁剪，固定相机和材质，保证 off／on 对照的唯一差异是 Hi-Z 开关。
