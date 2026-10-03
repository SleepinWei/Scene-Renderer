# Engine 后续修复记录

日期：2026-10-03。接续 `4ef5acc` 的主逻辑／渲染分离，继续处理设计审查中的 P1 资产与世界边界。

## 场景结构访问与灯光索引

之前 `RenderScene::objects`、灯光数组、sky／terrain 和相机仍是 public，调用方能绕过 `checkLogicThread()`、修订号与灯光维护。海洋预设直接清空对象数组、保留灯光数组，使太阳成为没有对应场景对象的隐藏引用；单独删除对象也缺少安全接口。

现在结构私有，读取返回 const 容器并检查逻辑线程。使用 `addObject`、`removeObject(id)`、`clearObjects`、`addSky`、`addTerrain`、`setCamera` 完成结构修改。重复插入相同 ID 是无变化操作；不存在的删除不改变版本。删除／清空维护灯光索引并释放 loader bootstrap 引用，活对象的组件变更后使用 `refreshObject(id)` 重新索引。海洋预设显式保留太阳对象、移除其他对象／灯光，效果保持原本意图。

Loader 在生产线程构建私有 staging，通过 future 完成后主线程 `replaceWith` 移交内容；这个接口只适用于已停止写入的 detached staging。所有现有原生、OpenGL、历史 Metal 和 PT 结构访问调用方均已迁移。组件内字段仍由主线程修改，const 容器并不使其引用的 GameObject／组件自动不可变；渲染线程仍必须消费快照。

## 同设备 GPU 图片共享与预算

之前 CPU 解码结果已共享，但 `GpuMaterial` 每次创建都会复制图片数组并新建六张 texture。材质变体或多个材质使用同一图片时重复上传；白色等默认图也重复占用 GPU 资源，32 MiB 接纳预算还会重复计费。

新增 `GpuImageCache`，同一设备共用 texture／view，材质保留独立参数、binding set 和 sampler。当前图片格式统一为 RGBA8UNorm，按完整像素内容与尺寸确认共享，hash 只用于候选索引。不可变 shared source 提供重复请求快路径，其弱引用检查防止 CPU 地址复用命中旧内容；不同 filter 不要求重复创建 texture，图片内容变化或尺寸变化会创建新条目。测试显式复用已过期 CPU alias 的内存地址，确认不会误命中旧纹理。

普通图、默认图和 packed special 图均共享。材质快照使用 shared image，special 图独立拥有 CPU 数组，不让小图通过 alias 指针保留整个 MaterialPayload。CPU 数据为内容校验保留，其大小约等于缓存图片字节数，不能把该 cache 当成只持有 GPU handle 的服务。

默认 **64 MiB 空闲预算**，只淘汰没有外部 GPU lease 的 LRU 条目；活材质、旧绘制包和 GPU completion retirement 仍决定最终释放时机。binding set 先退休，图片 lease 后释放。条目统计包含 resident／idle bytes、uploads／uploaded bytes、hits 和 evictions。这个预算不限制所有活图片，更不是 mesh、VT、render target 与 native heap 的全局硬预算。

## 上传等待期间的资产保留

之前 SceneAdapter 在成功接纳网格后才把对应材质标为使用中。若网格因当帧预算不足而等待，末尾清理会删除仍被场景引用的材质记录，下一帧再创建；细分记录也存在同类问题。

现在在接纳前标记全部引用依赖，暂未绘制也保留仍被引用的材质与细分记录；GPU 创建成功后才更新缓存 source。材质预算仅计缓存缺失图片，并合并单个材质中的重复图。保留每帧两项资产与 32 MiB 软目标，大资源仍允许独占一次上传，避免永远等不到预算。

## 运行统计

退出日志增加最近最多 256 个渲染线程帧的 CPU p95／p99、主线程提交队列的最大等待时间、共享 GPU 图片字节／上传／命中次数。每 32 帧和退出时更新分位数，异常退出也发布已完成帧的样本。它们帮助定位队列背压与上传尖峰；CPU 时间不是 GPU timestamp，队列等待不是输入到画面的端到端延迟。

## 验证与剩余工作

GPU 自检新增同 source／相同内容只上传一次、内容与尺寸不同不混用、RGBA8 读回、坏数据不污染 cache、错误线程拒绝、活 lease 不被预算淘汰、空闲 LRU 及不同 sampler 共享。场景检查覆盖结构跨线程读写拒绝、重复插入、删除／清空灯光索引和活组件刷新。

预算回归先同步上传三组资产，再在异步两项预算下同时替换三个网格，检查第三个网格延后完成时仍使用原材质，没有额外创建或阻塞进展。原有天空／太阳、海洋、地形／VT、TSAA、GUI 快照、故障传播、缩放和单线程对照继续执行。

本轮不包含私有组件注册表、跨线程世界命令队列、全局显存硬预算、分段 buffer 上传或 pipeline cache；这些按依赖顺序继续推进，再扩展 RHI 子资源图和屏幕反馈 VT。Windows／Linux 与历史 Metal GL 桥未做实机运行验收。

2026-10-03 在 Apple M4/macOS 上验收：Metal 全量 CTest **11/11**、Vulkan/MoltenVK **12/12**，新增地址复用用例在两后端分别再次通过 GPU 自检；OpenGL 兼容路径全量 **8/8**。Metal 启用 API／Shader Validation，Vulkan/MoltenVK 关闭本机已知阻塞的 MetalTools 组合。CPU 并发基础设施未改动，沿用前轮 ThreadSanitizer 验证；本轮未将完整图形应用用于 TSan 验收。

另用 `--classic cornell --hidden --size 480x270 --frames 300 --time 8` 检查超过 256 帧后的滚动统计（Metal API／Shader Validation 开启）：300 帧排空完成，最近 256 帧渲染 CPU p95／p99 为 9.57／13.04 ms，最大队列等待 36.58 ms；共享图片 11 次上传、19 次 acquire 命中。该单次运行只证明统计与持续运行路径正常，不作为跨硬件性能基准；图片命中是资产 acquire 次数，不是逐像素采样计数。
