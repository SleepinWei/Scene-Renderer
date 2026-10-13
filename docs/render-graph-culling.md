# RenderGraph pass culling（2026-10-09）

第一轮实现 RDG 方向的无效 pass 剔除。实时 `ForwardPbrRenderer` 默认开启；独立 `RenderGraph` 默认保持完整执行，调用者声明输出和副作用之后再启用。已有静态物体视锥裁剪继续由 `RasterVisibility` 处理。

## 算法与执行契约

1. 验证整张图的初始化、范围和重叠反馈，包括最终会被剔除的分支。
2. 按 mip/layer 跟踪资源的最新写入者。`Read` 和 `ReadWrite` 连接到当时的生产者；纯 `Write` 创建新版本。
3. 从 `exportResource` 的最终生产者及 `PassFlags::NeverCull` 反向遍历数据依赖，标记保留 pass。没有根的已启用图可以完全剔除。
4. 对保留 pass 重建 RAW/WAR/WAW 顺序依赖，避免中间写入被剔除后遗漏约束。录制仍按原声明顺序执行。
5. 重新计算保留资源的生命周期和兼容纹理复用槽。死资源不进入分配计划；导出纹理的生命周期延长到图结束，供图外消费者读取。

仅沿 RAW 数据依赖追溯存活性。若沿全部 hazard 边追溯，旧写入和无用读取会因为 WAW/WAR 被错误保留。一个保留 pass 的全部读取均视为必要，保持回调完整执行。

`import` 只表示输入已初始化，不自动把之后的写入视为图外可见。持久历史或图外读取必须显式导出；读回、模拟推进及回调内未声明的持久状态更新必须标记 `NeverCull`。`Write` 必须真正定义声明的整个范围；attachment Load／混合更新应使用 `ReadWrite`。每次 `compile` 生成计划后可直接 `execute(plan)`，无须重复编译；图变更后旧计划及其他图的计划均拒绝执行。

```cpp
engine::RenderGraph graph;
using A = engine::RenderGraph::Access;
graph.enableCulling();
graph.import("history");
graph.exportResource("output");
graph.exportResource("history");
graph.add("unused", {{"temporary", A::Write}}, [] { /* 不录制 */ });
graph.add("resolve", {{"history", A::ReadWrite}, {"output", A::Write}}, [] { /* 录制 */ });
auto plan = graph.execute(); // activePasses / culledPasses 是原始 pass 索引。
```

存活性遍历为 O(P + E)，其中 P 为 pass 数、E 为数据依赖边数。初始化及 hazard 分析仍使用有序 map/set，按实际展开的 mip/layer 单元处理；这不是整个编译器的线性复杂度保证。

## 实时接入

最终显示 `output` 显式导出。大气缓存、海洋模拟、带持久状态的水体捕获／表面、云历史和 TAA 更新显式标记 `NeverCull`；它们的输入链同时保留。水面反射、空气侧折射、焦散和太阳光束仍在 capture 回调内执行，维持辅助视角的完整物体列表。

没有透明物体时不再建立空 alpha pass。普通不含云／水体的 Scene 关闭 TAA 后，motion 没有消费者，自动剔除。带水体或云的 motion attachment 仍被 Load／ReadWrite 使用，保守保留；不能仅因 TAA 关闭就删掉其初始化。

通过 `FrameData::graphCulling=false` 或 `SCENERENDERER_DISABLE_GRAPH_CULLING=1` 恢复完整录制，用于 A/B。`ForwardPbrRenderer::graphPassStats()` 给出最近一帧执行及剔除的 pass 名称。默认未改变 AO、back depth、捕获内部的画质开关。

## 验证

新增 `engine-render-graph` CPU 测试：版本覆盖、ReadWrite、副作用／历史、mip/layer 导出、WAR/WAW 重建、死纹理移除、导出纹理存储保护、剔除分支校验及过期计划。400 个固定 seed 随机图与完整执行的独立标量参考比较，19,200 个声明 pass 中剔除 **10,124** 个，导出值与副作用观测全部一致。

原生 Scene effects 回归中，普通不启用 TAA 的不透明场景从 **9 → 8 pass**，只剔除未使用的 motion；HDR 和显示像素逐值相同。水下吸收回归保留 simulation／capture／volume／surface／eye，开关前后最大 HDR 差异为 **0**。原有 TAA、透明物体、云、水体辅助视角和 Snell/TIR 回归继续参与原生自检。

CPU CTest **4/4** 通过；Metal（API／Shader Validation 开启）及 Vulkan/MoltenVK 的 `--rhi-self-test`、`--water-self-test` 均以 0 退出。两后端的 pass 数及像素对照相同。[结构化结果与原生证据摘录](../img/diagnostics/render-graph-culling/validation.json)。

```sh
cmake -S . -B build
cmake --build build --target engine-render-graph-tests engine-concurrency-tests Scene-Renderer -j 6
ctest --test-dir build -R 'engine-render-graph|engine-concurrency|rhi-contract|rhi-graphics-contract' --output-on-failure
SCENERENDERER_DISABLE_PIPELINE_DISK_CACHE=1 MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer --rhi-self-test
SCENERENDERER_DISABLE_PIPELINE_DISK_CACHE=1 MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer --water-self-test
./build/Scene-Renderer --rhi-self-test --backend Vulkan
./build/Scene-Renderer --water-self-test --backend Vulkan
```

这轮验收的是剔除正确性与实际回调数量；不据此声称海底场景 FPS 提升。图中的很多资源仍是效果状态 token，capture／后处理内部尚未拆成真实纹理子资源 pass。进一步减少水体 GPU 工作，需要先扩展这些资源声明和内部 pass；本轮没有加入遮挡裁剪、并行 GPU 录制、异步 compute 或自动 barrier。
