# Engine 设计审查与改进建议

审查范围为组件／对象所有权、场景加载与资源缓存、场景到 RHI 的转换、渲染调度，以及本次地形 VT 的接入。以下明确区分已经修复的问题和仍需推进的架构工作。

## 本次已修复

| 问题 | 触发与影响 | 修改与验证 |
| --- | --- | --- |
| GameObject 与 Component 相互持有 shared_ptr | 场景清空后对象／组件仍存活，地形数组、纹理等可能无法释放 | 组件 owner 改 weak_ptr；`owner()` 明确检查过期。生命周期测试验证对象释放后 weak 引用失效 |
| `GameObject(std::string name)` 自赋值 | 用带名字的构造函数创建对象，成员名字仍为空 | 明确写入 `this->name`，覆盖构造测试 |
| 组件重复插入先绑定 owner | 被拒绝的组件仍关联对象；模板接口返回未挂载的新实例 | 已有同类型模板返回真正的已挂载实例；共享指针接口只给成功插入的组件绑定 owner；拒绝跨对象重复挂载 |
| Loader 重复 join 已完成线程 | 第二次加载包含旧 threadpool，抛出 system_error | 只 join joinable 线程并清空，验证连续加载及失败后再加载 |
| `hardware_concurrency()-2` 无符号下溢 | 0／1 核报告值导致异常线程数 | 先判断再减，线程数限定为 1–32 |
| Loader worker 异常未处理 | 无效 JSON／地形输入可触发 std::terminate | worker 捕获异常，完成线程 join 后传回调用线程；坏子文件回归验证 |
| 主场景 JSON 未解析就 destroy | 主文件解析失败后丢失当前场景 | 主 JSON 解析成功后才清空；验证失败前后的场景 revision 不变 |
| `RenderManager::generateShader` 未覆盖枚举时无返回 | 旧兼容路径可能发生未定义行为 | 明确抛出错误；消除对应的编译诊断 |
| 地形缓存、高度文件与草包围盒问题 | 见地形审查 | 版本／源路径／预算失效，分页输入，模型变化同步包围盒，GPU 与 CPU 回归 |

组件 API 的所有权变化需要调用者使用 `component->owner()` 或 `component->gameObject.lock()`，不要再直接把 `gameObject` 当作强引用。组件没有 owner 或 owner 已销毁时，`owner()` 抛出逻辑错误，方便定位无效使用。

## 尚未修复的高优先级问题

### P1：资源缓存存在并发数据竞争

`ResourceManager::getResource` / `getResourceAsync` / `find` 对共享 `unordered_map` 没有同步，而 Loader 可以在多个 worker 中调用它们。并发插入可能破坏容器，同一路径也可能被重复解码。路径键尚未统一，cache 保持强引用且没有释放预算。

建议先把 cache 内部容器封装为私有数据，统一规范化的 asset key；为同一 key 保存一个进行中的 future，合并重复请求。锁只保护索引，不覆盖耗时解码。把 CPU 解码结果和 GPU 上传状态分开，GPU 资源只能在渲染线程／设备队列创建。增加并发同 key、不同 key、加载失败与释放的回归，并用 ThreadSanitizer 验证。

### P1：加载失败仍可能留下部分新场景

本次处理了 worker 异常和重复 join，但 `loadSceneAsync` 仍会等待所有 worker，属于并行加载的阻塞接口。主 JSON 验证后会清空原场景，子资源失败仍可能留下部分新数据；`loadObject` / `loadSky` / `loadTerrain` 对部分打开失败只输出信息并返回。

建议将加载目标设为一个新的 `SceneBuildResult`：worker 只构建 CPU 数据，不修改当前世界；成功后一次发布，失败时保留旧场景。接口返回 future／状态与诊断；加入取消、进度与有界 job queue，再改名为真正的异步 API。Loader 的公共线程容器与线程数也应封装，禁止多个调用线程同时调度。

### P1：场景没有统一、稳定的渲染快照

`RenderScene::addObject` 分别锁对象和灯光；`destroy`、sky／terrain 指针及 revision 没有完整同步。当前 `SceneAdapter` 同时锁两个列表进行复制，仍不能保证所有公开字段是同一版本，也无法让 GUI 与异步发布共享明确的边界。

建议引入不可变的 `RenderWorldSnapshot`，对象、灯光、相机、环境与版本一次发布。修改走场景命令队列；统一结构 revision 与每个资产的内容 revision。所有改变渲染输出的 add/remove/参数变更应有相应历史／缓存失效规则。

### P1：地形 IO 尚未移出渲染线程

VT 已限制页数与每帧上传量，但 `GpuVirtualTexture::update` 直接调用源的 `readPage`。慢磁盘或首次粗页跨度读取可能影响帧时间。源回调与文件句柄目前按单线程使用，不能直接在多个 job 中并发调用。

建议拆成 request → IO/decode → ready → upload → publish 五个状态，并按字节数和时间分别限制解码、上传。使用版本与取消 token 拒绝过期结果；页表只发布成功上传且可用于该帧的映射。GPU 屏幕反馈可作为下一阶段增加精度的输入。

## P2：结构与可维护性

| 当前设计 | 建议 | 验收标准 |
| --- | --- | --- |
| 组件由字符串定位，再 static_pointer_cast | 类型化 `ComponentId` / 模板查询；实体使用稳定 ID 与 generation | 类型不匹配可检测，删除／重用实体不会命中旧 GPU cache |
| 非地形网格／材质 cache 主要依赖对象地址和显式 invalidate | 为 mesh、image、material 增加内容版本；区分几何、图片、标量更新 | 编辑同一对象的数据也能定向重建，避免整场景清缓存 |
| RenderManager 集中编辑器、旧 GL pass 与原生渲染调度 | 把编辑器控制、CPU 场景、渲染 snapshot、RHI renderer 分为明确模块 | 脱离 GUI 可渲染与测试；旧 GL 头不再进入核心场景数据 |
| ForwardPbrRenderer 包含阴影、G-buffer、透明、水、运动与后处理的手工顺序 | 在现有显式 CommandList 上建立小型 render graph，声明读写及资源生命周期 | 验证未初始化读取／反馈环，自动安排 transient targets 与调试标记 |
| RHI 纹理仍限定单 mip／单 layer 2D | 按实际需求增加 mip/layer/subresource 描述及能力查询 | Metal／Vulkan 对相同接口有契约验证，Unsupported 不被静默替换 |
| 地形采用固定距离 LOD、全局高度界及非确定性原子预算 | 屏幕误差 LOD、分块 min/max、优先级／稳定预算；保留前帧状态做 morph | 峰值误差和预算可测，移动相机时减少细节跳变，并恢复地形 TSAA 累积 |
| 程序场景与单例持有全局时钟／设置 | 显式传入时间、帧尺寸、render settings 与 asset service | 同一 snapshot 能在固定输入下稳定重放，不依赖已有 GUI 状态 |

目前 RHI 的显式资源、binding layout、命令依赖、在途帧、异步 readback 与延迟释放是合适的基础。本次增加区域上传和明确的材质 feature 位，继续把算法放在 renderer 中、把原生对象及提交留在 backend 中；VT 不需要回到 GL 兼容桥。

## 推荐实施顺序

1. 先封装资源 cache，修复并发访问与重复解码；为 scene 加载建立“成功才发布”的事务。
2. 接入统一 snapshot 与资产版本，移除渲染／编辑器跨线程读取公开可变容器。
3. 增加有界 IO／GPU upload job，接管 VT 和普通大资源的加载；记录实际帧时间与显存峰值。
4. 在功能保持一致的前提下拆出 render graph、稳定实体 ID 与类型化组件接口。
5. 再推进屏幕反馈 VT、地形屏幕误差 LOD、morph、可靠运动历史与更丰富的 RHI 子资源。

本次未重写整个 ECS、render graph 或资源服务，也未运行 ThreadSanitizer。并发风险是基于现有调用路径与无同步容器访问的代码审查结论，不能将 GPU 回归通过当成线程安全的证明。

2026-10-03 最终回归：Metal CTest 8/8、Vulkan CTest 9/9 通过。应用自检覆盖组件生命周期、重复加载、worker 异常传播与恢复、地形源替换后的缓存／历史失效，以及草组件的添加和移除。
