# Engine 组件边界、世界命令与资源配额

日期：2026-10-03。接续 [上一批修复](engine-followup-fixes.md)，落实设计审查中的组件注册表、跨线程世界修改入口，以及 RHI buffer／texture 的统一配额。

## 原问题与修复

之前 GameObject 的组件表和 Component owner 可以被调用方直接修改，绕过类型检查、灯光索引与线程归属。场景结构虽然已私有，删除组件仍需要手动 `refreshObject`；后台任务也没有一个只提交值的世界修改入口。Loader 的生产者停止写入仅是调用约定，没有封存检查。

现在组件注册表、owner、deferred 状态与世界关联均为私有。具体组件类型按 `type_index` 索引，`getComponent<T>()` 对精确类型采用索引查询；基类查询保留确定顺序的动态转换。JSON 的字符串反射入口仍保留，但相同名称对应不同具体类型、相同具体类型使用不同名称、把已有 owner 的组件挂到其他对象，都会拒绝。重复挂载同类型同名组件保留原实例。GameObject 与 Component 禁止复制，避免复制 owner／线程归属。

增删组件和修改 deferred 自动更新世界修订号、灯光索引与 loader bootstrap 失效状态；删除组件同步清除 owner。已有 `refreshObject(id)` 作为兼容入口保留，普通组件增删不再需要手动调用。对象 ID 索引同时包含普通对象、sky 与 terrain。

查询组件、查询 owner、组件注册，以及变换／网格／材质选择／灯光／地形的现有修改接口检查所属逻辑线程。`Transform::setTRS` 在写入前验证所有数值有限且缩放绝对值至少为 `1e-6`；零缩放会导致逆矩阵失效，现在返回明确错误且不部分更新。

这些检查覆盖封装接口。Transform、灯光参数、Camera，以及 Mesh／Material 内部仍有历史公开字段，直接赋值能绕过接口，**它们仍只允许所属逻辑线程读写**。本轮没有完成全部字段私有化，也没有将组件包装成线程安全共享对象；渲染线程继续只消费不可变快照。

## 加载与线程移交

原生加载顺序为：

1. 解码 worker 构建 detached GameObject，完成后 `sealForTransfer()`，通过 future 交付。
2. 协调线程接管封存对象，构建 staging、验证与准备 CPU payload。
3. staging 完成后 `RenderScene::sealForTransfer()`，封存所有对象并关闭 staging 命令入口，再通过 future 交付。
4. 主线程 `replaceWith(staging)` 接管内容，重新指定组件归属并发布新世界。

封存后生产线程不能继续通过对象／组件接口读写。未封存的外线程对象、单独的外线程组件不能直接挂进当前世界；已发布对象不能同时加入另一个世界。发布只在生产者已完成、future 已同步的交付边界执行，不支持生产者与接管线程同时访问。旧 OpenGL 加载保留 context 线程上的同步事务。

## 后台任务修改世界

主线程调用 `scene->commandPort()`，得到绑定当前世界代际的弱入口，再把入口与稳定 ID 交给后台任务。命令只携带 ID 和值，不携带 GameObject／Component 指针、GPU handle 或任意回调。

| 命令 | 内容与检查 |
| --- | --- |
| `SetTransform` | 对象 ID、Transform ID 与 TRS；组件已替换则拒绝旧请求，非法数值不写入 |
| `RemoveComponent` | 对象与组件 ID；删除后清除 owner 并更新索引 |
| `RemoveObject` | 删除普通对象列表中的对象；sky／terrain 使用已有专门结构接口 |
| `SetDeferred` | 对象 ID 与布尔值；同步更新世界修订号 |

多生产者队列容量为 **256**，满时立即返回 `QueueFull`，不等待主线程。主循环每次最多消费 **64** 条，窗口最小化时仍消费。执行发生在主逻辑线程，执行器不持有队列 mutex；每条命令有独立 future 和取消标记，单条执行异常不会阻断后续命令。

```cpp
// 主逻辑线程：在启动后台工作之前取得入口及 ID。
auto port = scene->commandPort();
const auto objectId = object->assetId;
const auto transformId = object->getComponent<Transform>()->assetId;

// 后台线程：只计算并提交值；post 不等待命令执行。
auto ticket = port.post(engine::SetTransform{
    objectId, transformId, {4, 5, 6}, {0, 10, 0}, {1, 1, 1}});
// ticket.cancel() 可取消尚未开始执行的请求。
```

实际主循环已调用 `applyCommands()`。需要结果的调用方轮询 ticket future；不要在主线程消费队列之前阻塞等待结果。取消仅保证消费前的取消检查，不能撤销已经执行的命令。

| 结果 | 含义 |
| --- | --- |
| `Applied` | 主线程已执行 |
| `MissingTarget` | 对象离开世界，或组件已删除／替换 |
| `StaleWorld` | 场景替换／destroy 使入口所属代际过期 |
| `Cancelled` | 执行前已取消 |
| `QueueFull` | 容量不足，需要调用方决定重试／丢弃 |
| `Closed` | 世界已释放或入口已关闭 |
| `Invalid` | 变换参数无效 |

场景替换与 destroy 使排队请求和旧入口失效。后台任务即使在替换之后才向旧入口提交，也得到 `StaleWorld`，不会意外修改新场景。新场景工作必须由主线程重新取得入口。入口只持有 weak_ptr，不延长世界生命周期；世界释放后，等待的 future 与后续提交均收到 `Closed`。

## RHI 统一资源配额

`Device::setResourceBudget(bytes)` 对当前设备所有 RHI buffer／texture 的逻辑负载统一限额，`0` 为不限额。原生编辑器可使用：

```sh
./build/Scene-Renderer --classic cornell --gpu-resource-budget-mib 256
```

参数作用于原生编辑器及其单线程对照；独立自检／离屏画廊入口不使用这个编辑器参数。默认不启用配额，旧 OpenGL 编辑器不支持此参数。

Buffer 按 descriptor 字节数计费；RGBA8／Depth32、RGBA16、RGBA32 texture 每像素分别计 4、8、16 字节，并检查尺寸乘法溢出。分配前检查剩余配额，超限不进入后端创建；后端创建失败或注册失败不计费。活资源和等待 completion retirement 的资源都继续占用配额，实际安全销毁后才减计；重复销毁不重复减计。关闭设备释放全部已登记资源。`resourceMemory()` 提供 buffer／texture 当前字节、配额与逐次成功分配的峰值，避免仅逐帧采样遗漏峰值。

这个配额覆盖通过 RHI 创建的 mesh、VT、阴影／帧目标和图片；图片 cache 的 64 MiB 空闲 LRU、每帧两项／32 MiB 上传接纳目标仍有各自用途。配额**不包含** driver heap 对齐、隐式 staging、交换链、pipeline、view／sampler 开销，不能作为实际系统显存上限。超限明确抛出 `ResourceBudgetExceeded`，由既有错误传播与退出路径处理；尚无自动内存压力淘汰、降级或 GPU 发布回滚。

## 验证

2026-10-03，Apple M4/macOS：Metal CTest **11/11**（API／Shader Validation 开启）、Vulkan/MoltenVK **12/12**、OpenGL 兼容路径 **8/8**。Vulkan 关闭本机已知阻塞的 MetalTools 组合，没有 Khronos validation layer。

- CPU 并发测试覆盖四个并发生产者、各生产者 FIFO、容量、限量消费、取消、代际失效、关闭、弱入口过期、错误消费线程与单条异常恢复；最新测试在 ThreadSanitizer 下通过。
- 场景回归覆盖名称／类型冲突、组件 owner 清除、自动灯光索引、未封存外线程拒绝、封存后生产者拒绝、解码→协调→主线程接管，以及值命令不提前修改世界、旧组件／旧世界请求失效。
- RHI 契约覆盖混合 buffer／四种 texture 格式计费、原生分配失败、超限前拒绝、延迟释放继续计费、重复释放、峰值与关闭归零。
- Metal 原生 Cornell 编辑器在 256 MiB 配额下完成 8 帧并排空退出，逻辑负载峰值约 134.12 MiB；8 MiB 下在阴影 atlas 分配前报告请求与可用字节，返回错误退出。该运行是配额行为验收，不是性能基准。

完整图形应用与第三方 AppKit／GLFW 未运行 TSan；Windows／Linux 未实机验收。下一步继续收紧历史标量／Mesh／Material 的写入口，补充内存压力策略、实际 native heap 统计、大 buffer 分段上传和 pipeline cache，再扩展 RHI 子资源图及 VT feedback。

## 后续核心数据封装

Transform／Light／Camera 核心参数、Mesh／Material 容器及 MeshRenderer 设置现已迁移到检查接口，CPU 资产参与封存移交，几何与贴图槽修改自动更新内容版本。此更新收紧了前述历史字段边界；Texture 原地像素和部分效果配置仍待迁移。详见 [可变数据边界与资产移交](engine-data-boundaries.md)。
