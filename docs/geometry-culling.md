# 物体与三角形裁剪（2026-10-09）

本轮实现减少实际几何绘制量的视锥裁剪。物体裁剪默认开启；GPU 三角形裁剪支持独立开启，先依据场景计时选择，避免把额外 compute 工作误当成帧率提升。此前的 [RenderGraph pass culling](render-graph-culling.md) 负责另一种优化，仍保留。

## 物体级别

实时 `ForwardPbrRenderer` 在验证原始输入之后，使用局部 AABB 的 center／extent，求六个平面的最大有符号距离；等价于检查八个角点的支持点，避免逐角点变换。使用实际渲染的 VP（包含 TAA jitter）和 model；NDC 深度为 0..1，始终不除以 W。

边界保留两个像素的横向余量，并为 float 变换的舍入和大世界平移消去误差保留余量。越过近裁面／眼平面的物体只在确实完全处于平面外时剔除。未知生成边界、实例、透明和 SSS 暂时保守保留；未发布的上传、无效变换／设备归属继续经过原有验证。

剔除发生在主视角 object buffer／binding 准备之前，减少 geometry、forward replay 和 motion 的物体绘制。保留原始输入列表供辅助视角使用。阴影每个 cascade／light face／RSM source 和水体每个主捕获／宽空气／镜面反射列分别测试自己的 VP，不能复用主相机裁剪结果。TAA 的模型历史仍以原始物体 ID 或原始列表位置维护，避免可见列表缩短后改变 fallback ID。

## 三角形级别

大网格使用 GPU 稳定索引压缩，保留原始 vertex／index buffer：

1. **mark**：每个线程测试一个三角形的三个 clip-space 顶点。只删除完全在某个裁剪平面外的三角形，跨越边界的三角形交给硬件 clipping。64 线程组内计算局部 prefix 和保留数量。
2. **scan**：128 线程对组计数进行分段 prefix，生成全局偏移和 `DrawIndexedIndirectArguments`。
3. **scatter**：复制保留三角形的原始索引，按原始顺序写到连续输出。主 geometry／forward／motion 使用此 index buffer 和 indirect draw。

生成网格增加 **prepare** 阶段，直接从 GPU 的源 draw arguments 计算 active dispatch；mark／scatter 使用 indirect dispatch，scan 只读取有效组。预留容量只决定分配大小，不决定实际线程数量。保留生成网格的 firstIndex／baseVertex 语义，输出 firstIndex 归零，零 active count 安全变成零 draw。

不使用全局 atomic 抢占来决定输出顺序，防止共面／边缘三角形因重排而改变结果。当前材质普遍采用双面渲染，因此这里不擅自删背面；透明、SSS、wireframe 和实例路径继续使用原始几何。FFT 海面独立的波面 vertex displacement 尚未接入这个普通 MeshVertex 筛选器。

较小网格（默认少于 2,048 个三角形）和完全位于视锥内的静态网格跳过 compute。静态 mesh、VP、model、viewport 和几何 revision 未变时，直接复用已成功提交的索引结果，零裁剪 dispatch；顶点／索引上传使缓存失效。GPU 生成网格每帧重新筛选。没有成功提交的录制不能发布缓存。

输出／scratch buffer 按当前可见 packet 复用，移出候选列表后释放；三角形输出约 16 bytes／源三角形，加上组 prefix 和两个 arguments buffer。预算／storage range／workgroup 限制不满足时回退原始 draw。只有显式诊断调用读取 GPU 计数，正常渲染热路径没有同步读回。

## 开关与诊断

```cpp
frame.objectCulling = true;   // 默认开启。
frame.triangleCulling = true; // 默认关闭，选择场景后开启。
frame.triangleCullMinimum = 2048;
renderer.render(frame, packets);
auto cpu = renderer.geometryCullStats();
auto gpu = renderer.readTriangleCullCounts(); // 显式同步读回，勿放到每帧热路径。
```

编辑器也可通过环境变量选择：

```sh
SCENERENDERER_ENABLE_TRIANGLE_CULLING=1 ./build/Scene-Renderer --demo
SCENERENDERER_DISABLE_OBJECT_CULLING=1 SCENERENDERER_DISABLE_TRIANGLE_CULLING=1 ./build/Scene-Renderer --demo
```

`SCENERENDERER_DISABLE_TRIANGLE_CULLING=1` 优先于开启变量／FrameData，便于强制基线。CPU 统计给出源／保留物体、裁剪 draw、候选容量、缓存命中、实际 compute dispatch 和 scratch 大小；`readTriangleCullCounts()` 给出生成网格真实 active input count 与最终 indirect draw 三角形数量。

## 验证与性能范围

专用原生测试覆盖六个平面、近裁面／眼平面跨越、非整组大小、跨 scan chunk 的进位、原始索引顺序、生成容量与 active count 的区别、firstIndex／baseVertex、零数量、在途帧复用、流式几何变更和 TAA。主渲染器对照包含 geometry／forward replay。

96×96 固定视角网格测试中，物体 **3 → 1**，三角形 **16,384 → 2,312**（减少约 85.9%），HDR 最大误差 **0**，显示像素逐值相同。另一个 16,513 三角形的 GPU 压缩结果为 2,359 个，和 CPU 参考的计数及索引顺序完全一致。这是实际 GPU arguments 的数量，不是只跳过 CPU 回调。

Metal（API／Shader Validation 开启）和 Vulkan/MoltenVK 的专用裁剪自检通过；Metal 强制开启三角形路径的水体回归、Vulkan 图形完整回归、CPU CTest **4/4** 通过。水体保留屏外镜面物体、宽空气折射、Snell/TIR、焦散／光束和 TAA；图形回归验证多光源／大世界阴影和 RSM。[结果与证据摘录](../img/diagnostics/geometry-culling/validation.json)。

```sh
cmake --build build --target Scene-Renderer rhi-vulkan-validation -j 6
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer --geometry-culling-self-test
./build/Scene-Renderer --geometry-culling-self-test --backend Vulkan
ctest --test-dir build -R 'engine-render-graph|engine-concurrency|rhi-contract|rhi-graphics-contract' --output-on-failure
SCENERENDERER_GPU_PROFILE=1 SCENERENDERER_GEOMETRY_CULL_REPORT=build/geometry-culling-timing.json \
  MTL_DEBUG_LAYER=0 MTL_SHADER_VALIDATION=0 ./build/Scene-Renderer --geometry-culling-self-test
```

局部基准分别测量完整网格、静态视角缓存和移动视角重新筛选的 geometry＋cull GPU 阶段（3 帧预热、8 次采样），不包含同步诊断读回的 CPU 时间，也不等于整帧 FPS。初版每帧重筛的小网格合计约 0.461 ms，完整网格约 0.174 ms；因此 GPU 三角形路径默认关闭，并增加静态缓存和生成网格 active dispatch。后续按目标场景选择阈值；水下目前较重的片元散射不会因视锥三角形裁剪自动减少。

最终版本在 Apple M4／Metal、验证层关闭的同一次局部运行中：

| 路径 | geometry＋cull GPU 中位数 | 裁剪 dispatch／帧 |
| --- | ---: | ---: |
| 原始索引 | 0.147 ms | 0 |
| 静态相机复用压缩索引 | 0.111 ms | 0 |
| 相机微移、重新筛选 | 0.153 ms | 3 |

静态局部 geometry 耗时降低约 25%；移动重筛略慢。这个小网格／96×96 局部结果未覆盖实际大场景、TAA 持续 jitter 或海底完整帧；不同运行中的初版／最终数字不能单独归因于代码变化。

[原始局部时间戳与统计](../img/diagnostics/geometry-culling/metal-timing.json) 保存每阶段数据、cache hits、dispatch 数和额外 buffer 大小。

已新增独立的 [当帧 Hi-Z 物体遮挡裁剪](hiz-culling.md)，可与本轮的三角形缓存组合。另有可开关的 [分层 cluster LOD 与 meshlet 视锥剔除](hierarchical-cluster-geometry.md) 原型。cluster 级 Hi-Z 已作为独立开关接入；实例 compact、normal cone 和 FFT 波面专用裁剪尚未实现。
