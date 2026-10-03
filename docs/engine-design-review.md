# Engine 设计审查与实施状态

截至 2026-10-03，项目已从“渲染器直接读取可变组件”的课程式封装，推进为**主线程拥有世界、CPU job 准备资产、独立渲染线程消费不可变快照、RHI 负责设备**的原生运行结构。这个边界适合继续扩展现有引擎；仍需避免把它等同于完整商业引擎的资产服务、ECS 或 render graph。

## 按推荐顺序已落地

| 顺序 | 原问题 | 实施结果 |
| --- | --- | --- |
| 1：资源 cache 与加载事务 | Loader worker 并发写无锁 unordered_map，同文件重复解码；子文件失败仍清空旧场景 | 私有 AssetCache、路径规范化、共享进行中的 future、失败可重试与未使用条目释放；独立 staging，所有资源验证成功后主线程一次 replace |
| 2：snapshot 与资产版本 | 渲染器读取相机、组件和公开容器，还回写太阳参数；GPU cache 依赖地址 | SceneSnapshotBuilder 在逻辑线程复制值与 const CPU payload；SceneAdapter.resolve 只在渲染线程消费；单调 ID、Mesh／Material 内容版本、地形来源与草状态失效 |
| 3：有界 jobs 与上传 | 公共 threadpool、无限任务／帧积压、VT 在帧内同步读取磁盘 | 有界 CPU／IO／协调队列；独立 RenderRuntime、2 个等待帧、设备线程检查、错误传播及排空 join；VT future、16 页上限、每帧 8 请求／8 上传，普通资产接纳预算；静态 mesh 按字节／CPU 用时分段，最多四个已分配上传任务 |
| 4：模块、实体与类型基础 | RenderManager 同时处理逻辑／GPU，字符串 static cast，手工 pass 顺序 | 原生编辑器不经 RenderManager.render 读取世界；独立设置描述、私有组件注册表、按具体类型索引的 getComponent<T>()、稳定 ID；有序 RenderGraph 验证初始化／读写声明并记录现有 pass |

主线程继续承担 GLFW、编辑器和主逻辑，渲染线程独占 RHI 和原生效果对象；CPU 路径追踪与旧 OpenGL 编辑器保留各自路径。详细线程图、接口、预算、验证和兼容边界见 [Engine 多线程说明](engine-multithreading.md)。

## 保留并验证的生命周期修复

- Component owner 使用 weak_ptr，避免 GameObject 引用环；过期 owner 明确抛错。
- GameObject 名称构造、重复组件挂载及模板返回值正确；Mesh／Material 复制生成不同 ID，GameObject／Component 禁止复制。
- Loader 捕获异常并在 future 中传播，不保留已 join 的线程；坏主／子 JSON 和缺少文件不会破坏当前场景。
- `RenderManager::generateShader` 对不支持枚举明确抛错。
- 地形源版本、预算、接缝、边界法线、草容量及包围盒修复见 [地形审查](terrain-virtual-texture.md)。

## 本轮继续修复

RenderScene 的结构与灯光索引改为私有并迁移全部调用方；重复插入去重，删除／清空／组件刷新保持索引一致。GpuImageCache 在同设备跨材质共享普通／默认／packed 图片，区分尺寸和完整内容，独立 sampler，空闲 LRU 64 MiB。上传接纳按实际缺失图片计费，等待帧不再清除仍被引用的下游资产。加入渲染 CPU p95／p99、队列等待和 GPU 图片统计。具体原因、验证和边界见 [后续修复记录](engine-followup-fixes.md)。

组件表与 owner 已私有，组件增删自动维护灯光索引；生产者通过封存与 future 明确移交对象。后台世界修改使用有界、绑定世界代际的值命令；RHI buffer／texture 可统一设定逻辑负载配额。实现、使用方式、回归与限制见[组件、命令与资源配额](engine-world-commands.md)。

核心可变数据的第一阶段封装已完成：Transform、Light、Camera、Mesh／Material 与 MeshRenderer 的参数和容器使用检查接口；几何／图片修改自动失效，材质标量独立计版，Camera 与 CPU 资产参与封存移交。附带修复 JSON 基础形状材质、贴图替换及相机裁剪范围。具体接口、验收与剩余边界见 [可变数据边界](engine-data-boundaries.md)。

## 尚需推进的设计工作

| 优先级 | 当前边界 | 下一步与验收 |
| --- | --- | --- |
| P1 | 世界结构、组件注册表与核心 Transform／Light／Camera／Mesh／Material 数据已私有；Texture 像素、部分效果配置与兼容字段仍公开 | 继续迁移历史 Texture 与效果配置，约束共享可变资产组移交；新模块使用检查过的 API／命令。稳定 ID 不复用，后续 ECS 槽位需 generation |
| P1 | 配额压力时回收空闲图片／等待退役，候选缓存事务与成功画面回退已实现；CPU 世界仍独立发布 | 完整世界的 CPU／GPU 联合两阶段加载、品质降级、实际 native heap 统计与大场景压力验收；动态 GPU 状态依靠成功画面隔离，详见发布文档 |
| P1 | 异步静态 mesh 分段上传、用时预算和每设备 pipeline cache 已实现；完整分配、图片／生成资源和首次管线构建仍不可分割 | 扩展图片／生成资源的增量初始化与管线预热；测量大场景启动、GPU 时间、端到端输入延迟及实际 native heap 峰值，详见分段上传文档 |
| P2 | 场景取消不能中断正在执行的 Assimp／磁盘操作；路径仍沿用历史 cwd 约定 | 资产根目录、结构化诊断、分阶段取消与请求代际；失败／过期结果不发布 |
| P2 | graph 是有序记录及校验，大气／阴影／海洋模拟仍在图前执行 | 将效果纳入资源图，增加 RHI mip/layer/subresource、transient 生命周期和 debug marker；再实现自动调度／资源复用 |
| P2 | 相机与编辑器逻辑仍按主线程帧 tick，PT 启动接口会阻塞 | 输入消息与独立固定步长模拟；PT 任务状态／取消；验收暂停、慢 GPU、加载时逻辑时钟与交互行为 |
| P2 | VT 是 CPU 预测，LOD 用固定距离和全局高度界；动态地形不累积 TSAA | 屏幕 feedback、阴影／反射视角请求、分块 min/max、屏幕误差 LOD、morph 与可靠运动历史 |

推荐后续仍按依赖顺序：继续完善历史资产接口与联合世界发布，扩展增量资源初始化与管线预热，再扩展 graph 与 RHI 子资源，最后推进屏幕反馈 VT 和地形时间连续性。屏幕 feedback、morph、全局 ECS、跨队列 GPU 调度没有在本次实现中伪装为已经完成。

## 验证范围

CPU cache／job／帧队列和 graph 契约有独立测试，并在 ThreadSanitizer 下运行。原生 GPU 回归检查 immutable snapshot、场景加载事务、GUI 数据、worker 异常、线程归属、异步 VT 页表及窗口变化；原有天空、海洋、阴影、材质、地形和 TSAA 数值回归继续执行。

没有对完整应用及第三方 AppKit／GLFW 运行 ThreadSanitizer，也没有 Windows／Linux 实机多线程验收。Metal 使用 API／Shader Validation；Vulkan 使用 MoltenVK，本机没有 Khronos validation layer。

2026-10-03 最终原生回归：Metal **11/11**、Vulkan/MoltenVK **12/12**，包含线程故障传播、GUI／世界快照隔离、真实 Texture 路径合并、异步地形缩放和单线程对照。

后续 GPU 图片共享与世界结构回归：Metal **11/11**、Vulkan/MoltenVK **12/12**，新增地址复用 GPU 自检在两后端通过；OpenGL 兼容路径 **8/8**。

组件／世界命令／RHI 配额后续回归：Metal **11/11**、Vulkan/MoltenVK **12/12**、OpenGL **8/8**，CPU 命令队列再次通过 ThreadSanitizer；256 MiB 正常运行与 8 MiB 明确拒绝分配的编辑器路径通过。

核心数据封装后续回归：Metal **11/11**、Vulkan/MoltenVK **12/12**、OpenGL **8/8**，CPU LogicAsset 错误线程访问／复制检查通过 ThreadSanitizer；详情见 [可变数据边界](engine-data-boundaries.md)。

内存压力与 GPU 帧发布阶段已完成并通过三后端回归：空闲图片回收、候选缓存／材质参数隔离、窗口与海洋超限时保留旧像素、正常快照恢复。完整范围、成本与剩余联合加载工作见 [GPU 发布文档](engine-gpu-publication.md)。

分段静态网格上传与每设备 graphics／compute pipeline cache 已完成三后端回归及 CPU RHI TSan 检查；完整实现、预算与尚未拆分的工作见 [分段上传与管线缓存](engine-streaming-and-pipeline-cache.md)。
