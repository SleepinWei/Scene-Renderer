# Engine 后续计划实施与验收

实施与验收：2026-10-03 至 2026-10-04。接续核心数据封装、GPU 发布回退和静态网格分段上传，本文记录这轮实际落地的系统与边界。此前各批次文档中的“待推进”以本文为当前状态。

## 已实施

| 原计划 | 本轮实现 |
| --- | --- |
| 历史 Texture 与效果配置边界 | Texture CPU 数据与元数据私有，像素替换校验并自动计版；材质快照持有独立只读像素。解码缓存 Texture 冻结，允许跨线程读取，修改须创建替代资产。可变共享 Texture 随引用它的资产组移交。Atmosphere／Ocean／Terrain 使用校验后的整组配置提交 |
| 联合世界加载 | Loader 构建并封存 CPU staging；渲染线程逐帧准备独立 SceneAdapter，建立 renderer 并完成试绘／GPU 等待后返回 ready；主线程才 replace CPU 世界，再按 token 激活已准备 GPU 缓存。失败、取消和过期候选保留旧世界 |
| 图片增量初始化 | 普通材质图片按整行分块写入，默认每帧 8 MiB、每块 256 KiB，与 mesh 共享 2 ms CPU 软目标；至少一个图片 chunk 可推进。缓存最多四个已分配但未完成图片，候选材质持有 lease，完成前保留原材质或不绘制 |
| 生成资源 | 地形等生成 mesh 的顶点／索引 buffer 使用 Uninitialized；计算生产者写入实际绘制范围，indirect 参数先置零。静态地形视图／模型／驻留页不变时保留确切已生成网格，跳过重复计算 |
| 管线准备与持久化 | 联合加载在 CPU 发布之前建立所需管线。保留每设备独立 handle／native cache；Metal Binary Archive 与 Vulkan Pipeline Cache 保存到当前着色器构建目录的 `native-cache/`。Vulkan 校验 vendor、device、driver、UUID；Metal 按设备和系统版本隔离，失败退回普通编译 |
| 完整效果 pass 纳入图 | 大气、海洋模拟、阴影／RSM 改由 RenderGraph 回调记录；图编译检查 mip／layer 初始化、RAW／WAR／WAW 依赖和 transient 生命周期。GPU 调试器中具有图 pass 名称；Vulkan 标签按 Debug Utils 能力启用 |
| RHI 子资源 | Metal／Vulkan 支持单采样 2D／2D-array 存储、mip、单层 2D 视图、指定 mip／layer 的上传、复制、异步读回；逐子资源检查同 pass 读写冲突和提交间依赖，Vulkan 独立跟踪 layout。配额计算全部 mip 与 layer |
| transient 资源复用 | GraphTextures 按编译后的非重叠生命周期复用兼容纹理对象。SSS 背面深度测试先执行，再与场景深度共用一张 Depth32 纹理；每帧验证两个逻辑资源没有重叠。节省 `width × height × 4` 字节，例如 1920×1080 约 7.9 MiB |
| 地形 GPU feedback | 从真实、已抖动的渲染深度重建地形 UV，水平和垂直像素足迹选择 mip；最长边最多 128 个反馈采样，异步读回完成后消费，不在正常帧内等待。高度及材质分别请求；保留 CPU 可见性预测与粗 mip 回退 |
| 多视角 VT 请求 | 阴影 cascade／face 与 RSM 矩阵进入下一帧辅助可见性请求。最多八个辅助视图；主相机至少保留约 3/4 请求容量，其余有界合并。材质预测使用高度源的高度界，避免按零高度平面漏页 |
| 地形 LOD 与时间连续性 | 内存及 raw 高度源生成 34125 个 quadtree 分块 min/max，包含 bilinear 支撑范围；packed 源缺少该信息时保守使用全局界。按分块高度变化、模型尺度、FOV、视口和距离估计屏幕误差，以 2 px 为目标，叶子预算仍是硬上限。共享全局格点的高度 morph 保持接缝一致；草通过同一变形后四角的三角形附着 |
| 逻辑时钟与输入消息 | 60 Hz 固定步长，最多补八次，暂停与速度控制；输入复制为值消息，键盘供固定 tick 重用，鼠标／滚轮增量只消费一次。修复同一事件轮询内多个鼠标／滚轮事件被覆盖。最小化时仍 tick 逻辑；普通渲染快照满队列时跳过，继续事件和模拟；控制消息保持可靠、有序提交 |
| 资产路径与取消 | `--asset-root DIR` 指定根目录；JSON 子资源先相对各自文档解析，再回退根目录；mesh 与 Texture 导入同样解析根目录。文件按块读取，JSON 最大 64 MiB，错误带阶段和绝对路径；Assimp progress、节点转换、高度扫描／bounds 构建及阶段边界检查取消 |
| 大场景提交成本 | 同一 graphics pass 复用的 frame／material binding set 只做一次完整资源检查和依赖汇总；每个 draw 仍验证 pipeline 布局和范围，native 解析保持资源句柄存活检查，不能跳过过期资源保护 |
| 压力处理与测量 | Metal 实际设备分配和推荐工作集、Vulkan 可用时的 heap budget 估计独立于 RHI 逻辑配额。收集 GPU 提交耗时、输入采样到完成确认延迟、CPU p95／p99、跳过的快照、native 内存峰值 |
| 自动品质降级 | 显式启用 `--auto-quality` 或编辑器开关后，配额失败最多下降三个 tier，缩小 FFT、海洋网格、地形叶子及 VT 物理缓存；CPU 请求值不变。固定的阴影／渲染目标仍须满足配额；已有画面时回退并冷却重试；冷启动重试同一 packet，不能恢复时明确抛错。关闭自动策略恢复请求画质 |

## 加载及线程协议

1. CPU staging 验证并封存，future 建立 worker 到逻辑线程的同步。
2. 将 detached publication payload 交给 `prepareScene`，旧世界继续渲染。
3. 独立候选缓存推进普通资产上传；没有 pending 资产时试绘并等待 GPU。
4. ready future 成功后逻辑线程一次替换 CPU 世界，排队 `activatePrepared(token)`。
5. 新世界快照在激活消息之后进入同一有序队列。ready 到激活期间的取消／新加载使旧 token 无效。

设备错误可能让整个 renderer 失败；这不是跨 CPU／driver 的不可失败原子事务。旧成功画面额外占用一张 RGBA8 纹理，两个世界的准备期同时驻留 GPU 资源；足够大的场景仍可能因配额不足而被拒绝，保留旧世界。最小化期间不提交普通 GPU 帧，候选准备会等待恢复；取消及退出会结束候选 future。

普通 `trySubmitFrame` 满队列返回 false，跳过的是本次值快照，已接收帧不被覆盖。下一次成功提交包含当前世界状态。世界命令、场景准备／激活及天空 bake 仍使用可靠队列；GPU worker 异常传回主线程。GLFW、ImGui 与逻辑继续属于主线程，未新增第三条模拟线程。

## 预算与时间统计的含义

- 图片／mesh 创建完整容量仍不可分割；按行上传的最小单位是一整行，不能拆半行。2 ms 是软目标，不能保证 driver 分配、首次编译或任何 native 调用的硬时限。
- 磁盘 cache 加载失败、超出 64 MiB、版本不兼容或无法写入时，继续正常编译；缓存通过临时文件原子替换，删除构建目录即可清除。`SCENERENDERER_DISABLE_PIPELINE_DISK_CACHE=1` 可禁用磁盘缓存。普通进程内 native cache 不受影响。
- Metal GPU 时间是完成的 command buffer 区间；Vulkan 是带 timestamp 的 pass-list submission 区间。不同后端分组不同，峰值不能直接当作完整帧 GPU 时间对比。时间数据在 GPU 完成后更新。
- “输入采样到 GPU 完成确认”包含逻辑／快照、排队、GPU 和完成轮询开销，属于确认延迟；没有测量显示器扫描、实际光子延迟或 OS 合成。
- Metal native 内存是设备报告的分配；Vulkan heap 数字是驱动估计。逐帧采样可能遗漏中间峰值。RHI 配额仍是 buffer／texture 逻辑字节上限，不包含 shader、pipeline、隐式 staging 和 swapchain。

## 验收

单独运行 Engine／RHI 回归（不含 PT 目标）：Metal **11/11**、Vulkan/MoltenVK **12/12**、OpenGL **8/8**。Metal 启用 API／Shader Validation；本机没有 Khronos validation layer。CPU 并发与 RHI graphics 契约分别运行 ThreadSanitizer。

提交前另将已提交基线与本轮 Engine 文件导出到独立目录，排除同时进行的 PT 修改及其新增文件。该隔离版本全量 Metal 构建成功，完整 CTest **14/14**（含既有 PT 测试）通过，确认 Engine 提交可独立构建／验证。

新增回归覆盖：

- Texture 只读冻结、非法修改保留、跨线程访问、共享纹理修改自动失效；效果／地形配置继续通过原有天空、海洋和 terrain 数值测试。
- CPU 文档相对路径、显式资产根目录、结构化错误；取消 scope 的线程隔离与嵌套恢复。
- 候选 GPU 准备、取消、试绘失败、后续重试和激活；旧世界在失败后继续提交。
- 真实图片分块逐字节 GPU 读回；逐 mip／layer 上传、清屏、复制、读回，另一个子资源保持不变；非法范围及配额恢复；单 pass 64 个 draw 复用 binding 的依赖仍正确。
- transient 真正减少物理纹理数量，同时拒绝重叠生命周期复用；原有 SSS／前向／deferred／TSAA 像素测试。
- 深度 feedback 的异步读回和页缓存上限；内部高度峰值不丢失、屏幕分辨率／距离改变的 LOD、极端 LOD 接缝的覆盖、方向及 morph 高度一致性。
- 变形后地形的草附着；固定时钟和输入回放与渲染帧划分无关；非阻塞满队列拒绝、接收顺序及关闭排空。
- 已有画面及冷启动下 2048 FFT 超限，自动降低规格并成功绘制，原始 CPU FFT 请求仍为 2048；磁盘 cache 保存及新设备重载。

### 经典场景及配额压力运行

Apple M4／macOS 15.3.1，2026-10-03～04，双线程、CPU 等待队列 2、GPU 在途上限 3。GI 场景 640×360、固定效果时间 8；海洋 320×180、40 帧。统计从 RenderRuntime 建立后开始，排除同步模型导入，包含异步资产准备、绘制及最后排空。本机还有开发负载，数值用于检查路径与生命周期，不作为严格性能基准。

| 场景 | 帧数 / 时间 | CPU p95 / p99 | RHI 逻辑峰值 | native 采样峰值 | 最后待上传 mesh |
| --- | --- | --- | --- | --- | --- |
| Sponza，25 mesh／262267 三角形 | 120 / 2.60 s | 21.36 / 45.55 ms | 316.99 MiB | 365.50 MiB | 0 |
| San Miguel，281 mesh／5617451 三角形 | 600 / 55.69 s | 370.13 / 382.95 ms | 735.25 MiB | 783.44 MiB | 0 |
| 高清海洋，192 MiB 配额 | 40 / 0.38 s | 9.31 / 31.81 ms | 187.34 MiB | 204.61 MiB | 0 |

Sponza／海洋开启 Metal API 与 Shader Validation；San Miguel 此组只开启 API Validation。三组均无失败画面回退；海洋发生一次配额压力，冷启动同包重试并降至 tier 1 后成功。分别跳过 952、16850、134 份普通快照，逻辑丢弃时间均为 0，普通提交等待为 0。GI 场景全部静态 mesh 上传完成后仍运行并正常退出；共享图片驻留约 159.25／343.04 MiB，静态网格上传累计约 8.63／242.92 MiB。

San Miguel 另在提交重复检查优化前，开启 API + Shader Validation 跑满 1200 帧（404.65 s），上传队列同样清空、无配额拒绝。线程采样确认持续执行阴影提交及 MetalTools 检查，主线程仍响应事件。这两组改变了验证开关，不能把耗时差全部归因于提交优化。

大场景成本仍然明显：最后一组 San Miguel 的 GPU submission 峰值约 60.37 ms，输入采样到完成确认峰值约 1496 ms。非阻塞普通提交保证主逻辑继续运行，但不能消除渲染线程／GPU 本身的执行开销；尚不能承诺该场景低延迟或稳定高帧率。后续应根据 pass 分析优化阴影视图剔除、绘制合批及 native descriptor／binding 复用。

```sh
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer \
    --classic sponza --hidden --size 640x360 --frames 120 --time 8
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=0 ./build/Scene-Renderer \
    --classic san-miguel --hidden --size 640x360 --frames 600 --time 8
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer \
    --classic ocean --auto-quality --gpu-resource-budget-mib 192 \
    --hidden --size 320x180 --frames 40 --time 8
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer \
    --render-gallery img/metal terrain
```

本机相同海洋在 128 MiB 配额下，三级降级后仍因固定渲染资源下限而明确拒绝冷启动（进程退出 1），没有假装降级成功。这里 native 分配仍高于 RHI 配额，因为 driver／pipeline／staging 不在逻辑配额内。

本轮重新生成并人工检查 `img/metal/terrain.png` 与 `terrain-wireframe.png`；地形 HDR 均值／峰值约 0.07380／0.46631，线框约 0.06803／0.46509。两张图展示当前屏幕误差 LOD、拼接／高度 morph 及同表面附着的草。

## 保留的明确边界

- 图的 pass 顺序由应用声明，编译依赖用于验证；尚未实现自动重排、多队列并发执行或 native heap aliasing。当前物理复用复用完整 texture 对象，不能把它描述为任意资源自动节省显存。
- 首次 cache miss 的 pipeline 编译仍同步。试绘在旧世界继续显示期间完成准备，但 GPU worker 可能出现编译尖峰；没有承诺完全无卡顿的后台 pipeline 编译。
- 初次效果纹理分配／初始化和某些生成资源仍有不可分割工作；普通静态 mesh、图片写入已增量化，生成 mesh 不再整块清零。
- GPU feedback 基于地形包围高度范围中的可见深度，可能带入其他几何的误请求；预测／粗 mip 回退保证有界可用性，没有独立 terrain-ID 附件。辅助阴影请求来自上一帧。当前水体使用屏幕折射及天空 LUT，不存在独立反射相机 pass；辅助视图 API 可接入未来反射相机。
- LOD 变化和页驻留更新时地形标记 reactive；视图、模型和驻留稳定时复用确切几何并允许 TSAA。尚未生成 LOD 变化时逐顶点的前帧变形位置，不能声称跨 LOD 的完整运动历史。
- 自动降级不能减少普通场景图片／mesh 的不可压缩请求，也不自动增大配额；联合加载候选失败仍保留旧世界。恢复画质由关闭开关明确触发，不按时间反复升降引起分配抖动。
- Assimp 或第三方图片解码中不提供 progress 的内部步骤无法强行抢占；取消在支持的回调和阶段边界生效。
- OpenGL 4.1 明确拒绝 mip／array 存储扩展，维持现有单 mip／单 layer 兼容契约。历史 Metal GL 桥、完整应用 TSan、Windows／Linux 原生 GPU 实机验证未执行。
