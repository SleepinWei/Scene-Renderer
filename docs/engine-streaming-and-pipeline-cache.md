# 分段网格上传与 RHI 管线缓存

2026-10-03，接续 [GPU 发布与内存压力处理](engine-gpu-publication.md)，完成异步静态网格跨帧上传、用时预算与每设备 graphics／compute pipeline 共享缓存。README 与 Engine 设计审查同步更新。

## 修复的问题

此前的“两项资产／32 MiB”只是接纳目标，超过目标的大网格仍会独占一次整块上传，导致渲染线程尖峰。若仅把后续写入拆开，空 buffer 创建时的整块清零仍会触碰全部数据；Vulkan 还会产生一次整块初始化传输。

不同 renderer／效果实例创建相同管线时也重复调用原生构建。窗口变化、失败帧后的 renderer 重建和多个同配置实例可能反复付出编译成本。简单地返回同一个公开 handle 会破坏独立资源所有权：释放其中一个实例会使另一个实例的句柄失效。

## 静态网格的上传与发布

`SceneAdapter` 在 `snapshot.asynchronousStreaming` 路径维护上传任务，保存不可变 `MeshPayload`、待上传 GPU mesh、顶点与索引游标。默认预算如下，可在设备线程通过 `setMeshUploadBudget` 调整：

| 参数 | 默认值 | 作用 |
| --- | --- | --- |
| `bytesPerFrame` | 8 MiB | 一次 resolve 内所有静态网格分段写入的总上限 |
| `bytesPerChunk` | 256 KiB | 单次写入上限，按顶点／索引元素边界取整 |
| `cpuMilliseconds` | 2 ms | 从 resolve 开始计时的软目标，完成一个 chunk 后检查 |
| `maxPendingMeshes` | 4 | 当前候选缓存中已经分配 GPU buffer 的未完成网格数上限 |

已有的每帧两项新资产／32 MiB 接纳目标继续约束新任务与材质图片；材质图片仍整体上传，单个超大图片仍可独占一次接纳。网格 chunk 的字节单独累计，并计入总上传统计；继续已有任务不重新消耗新资产名额。

`BufferDesc.initialization` 新增 `Uninitialized`，仅允许无初始数据且带 `CopyDestination` 的 buffer 使用。Metal／Vulkan／OpenGL 跳过主动清零；默认初始化语义保持为 `Zeroed`。调用方必须在消费前写入全部需要的字节。

`GpuMesh::beginUpload` 一次分配完整顶点／索引容量，然后按连续范围上传，检查容量、有限数值、非零法线和索引范围，逐段累计包围盒。全部顶点和索引写入后才标为 ready，未完成 mesh 不能提交绘制。

首次加载的网格完成前不进入 DrawPacket。已有普通网格更新时，可在新任务上传期间继续绘制已发布几何；细分输入等待完成时暂缓绘制。完整新网格进入候选缓存时才增加上传 epoch，使 TSAA 历史失效。源版本再次变化会替换上传任务；恢复已发布版本、删除对象或失效资产会取消对应任务。

上传游标参与候选缓存事务。候选帧失败时恢复之前的游标，下一帧可以重写同一不可变 payload 的已上传前缀；不会改写已经发布的旧 mesh。部分 buffer 字节不逐字节回滚，未完成资源始终由发布状态隔离，正常 GPU 释放继续使用 completion retirement。

CPU 用时目标不能抢占 native 分配或单次写入。只要存在可推进的任务和有效字节预算，即使前面的地形工作已经超过目标，仍允许至少一个 chunk，避免永远无法完成。待上传任务按场景遍历顺序推进；这是有界的增量上传，尚未实现距离优先级或抢占调度。

## 每设备管线缓存

缓存位于共用 `GraphicsDevice`，Metal／Vulkan／OpenGL 复用同一策略。每次 create 仍先验证 descriptor，再查找底层 native pipeline；每个调用获得不同公开 handle 和独立 descriptor，释放一个 handle 只减少相应 lease。

缓存 key 使用完整状态与文件字节，不仅依赖文件路径、长度、mtime 或哈希。graphics key 包含顶点 ABI、绑定布局、shader 与 entry point、各颜色附件格式、深度／混合／剔除／线框状态；compute key 包含绑定布局、shader 与 entry point 及线程组尺寸。所选后端 shader 文件与 reflection 文件内容也进入 key，同长度、同 mtime 的 shader 修改仍会产生新条目。调试 label 不影响共享，布局排列顺序不同可能产生额外条目。

graphics 与 compute 合计默认保留最多 **64 个空闲 native 条目**，按 LRU 淘汰；带活 handle 的条目保持 pin，不强制释放。`setPipelineCacheIdleLimit` 可调整空闲条目数量，设为 0 则不保留空闲管线。淘汰先等待 GPU 安全释放，再选择无 lease 条目；处理 retirement 回调引起的再次释放，避免递归淘汰。创建失败不会留下缓存条目或句柄；设备关闭时释放所有句柄与 native 条目。

`pipelineCacheStats()` 返回 graphics／compute 原生构建次数、命中、淘汰、native／idle 条目数与活 handle 数。缓存只在设备所属线程使用。

## 运行统计

双线程编辑器日志新增网格上传累计 MiB／chunk 数／最近一次 resolve 的待上传网格数，以及原生管线构建／缓存命中次数。网格累计值包括已 resolve、之后可能回滚的候选帧；resolve 中途抛错时没有返回完整统计，因此不是严格的物理传输计数。管线命中统计是 create 请求复用次数，不是绘制次数。

`SceneFrame.resolveCpuMilliseconds` 覆盖整个 resolve，包含地形／材质等工作；它及既有渲染 CPU p95／p99 都不是 GPU timestamp。

## 验证

在 Apple M4／macOS 上完成 Metal **11/11**、Vulkan／MoltenVK **12/12**、OpenGL **8/8**。Metal 开启 API／Shader Validation；本机 Vulkan 没有 Khronos validation layer。新增回归包括：

- 300,000 个顶点、9.6 MB 数据的真实 GPU 跨帧上传：第一帧精确上传 8 MiB／32 个 chunk，后续完成后逐字节读回与 CPU 数据一致。
- 极小字节预算、极小用时预算、待上传任务上限；任务取消／替换／回滚与前缀重放；旧 mesh 保持可用、完整发布后包围盒和历史版本更新。
- 未完成网格拒绝绘制、上传空洞拒绝；buffer 默认清零与 Uninitialized 参数契约。
- graphics／compute 独立句柄、共享 native、LRU 与活句柄保护、关闭释放、失败重试、状态／绑定 ABI 区分，以及同长度同 mtime 的 shader 内容变化。
- 两个真实 renderer 共享管线，释放第一个后第二个的输出像素保持一致。

CPU buffer 与 graphics 契约测试另通过 ThreadSanitizer；完整应用及第三方 AppKit／GLFW 没有运行 TSan。40 帧双线程 Metal 内置场景在 256 MiB 逻辑配额下完成，退出时 0 个待上传网格、38 次原生管线构建／7 次缓存命中、0 次配额拒绝。该运行验证路径与统计，不作为性能基准。

## 剩余边界

- 分段路径覆盖异步静态 mesh 的顶点／索引。同步画廊／单线程对照、材质图片、地形／草／细分的生成 buffer 和海洋初始化仍有整块工作。
- 完整 buffer 分配依然不可分割；取消／替换、候选事务和等待 GPU 退役可能使旧资源与新任务同时驻留。四任务上限不是全局显存上限，逻辑 RHI 配额继续单独控制 buffer／texture。
- Pipeline cache 在当前设备、当前进程内复用，首次构建仍同步；尚无离线预热、磁盘持久化、Metal binary archive 或持久化 Vulkan pipeline cache。缓存 key 保留 shader 文件字节，条目数量限制不代表字节数或实际 driver heap 预算。
- 大场景压力、实际 native heap 统计、自动品质降级、CPU／GPU 联合世界加载，以及完整资源图／子资源与 VT feedback 仍需后续推进。Windows／Linux 与历史 Metal GL 兼容桥没有实机验收。
