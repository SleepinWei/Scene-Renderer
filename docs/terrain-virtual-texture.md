# 地形审查、修复与 Virtual Texture

本次把新 RHI 地形的**高度场和五张 PBR 材质图**接入软件虚拟纹理，Metal 与 Vulkan 共用算法。高度生成、草的位置、前向／延迟材质、阴影／RSM 的 alpha 和材质采样使用同一套页地址规则。实现不依赖硬件 sparse texture。

## 原实现的问题与修复

| 问题 | 影响 | 本次修改 |
| --- | --- | --- |
| 固定为 25,600 个叶节点分配顶点与索引 | 网格约 237.5 MiB，即使镜头只需要少量节点也一直占用 | 默认 `maxLeaves=2048`，原子预留一次细分增加的三个叶节点；超预算保留父节点 |
| 完整高度场上传到 SSBO | 8192² float32 单独占用 256 MiB GPU 存储，原生加载还保留整张 CPU 高度图 | 高度 tile atlas；旧二进制输入直接按页读取，不保留完整 CPU 数组 |
| 只根据四个角点距离选择 LOD | 可能漏掉块内高起伏、靠近相机的部分 | 使用全局高度范围形成保守高度柱，计算其视空间 AABB 到相机的距离 |
| 只处理一级相邻 LOD 差，边缘固定奇偶位移 | 更大的层级差无法正确连接，角落可能移出节点边界 | 在 1280² 公共整数格上检查顶点周围所有 LOD 单元，按最粗步长统一吸附 |
| 边界高度采样 clamp 后仍按完整差分跨度计算法线 | 平面在地形边界的坡度被减半 | 使用实际有效采样跨度计算单边／中心差分 |
| 高度使用硬件线性采样 | Metal 测试发现滤波权重量化产生约 1.24e-5 的平面高度误差，影响细小差分 | 高度用四次 `texelFetch` 和浮点插值；材质保留硬件线性过滤 |
| 高度文件没有大小、读取状态和有限值检查；重复加载覆盖裸指针 | 截断数据、未初始化样本、内存泄漏 | 校验格式／大小／有限值；原生路径分块扫描范围，兼容路径先验证再替换数组 |
| `model`、`polyMode` 等依赖旧 GL 初始化 | 原生路径可能读取未初始化状态 | 给出确定的 CPU 默认值 |
| `SceneAdapter` 地形缓存只判断组件指针 | 重载高度、修改预算或增加／删除草后，GPU 资源仍可能是旧的 | 检查数据版本、路径、尺寸、预算、材质与草状态；材质标量每帧更新 |
| 草按原子执行先后填满固定容量 | 可能只填满一片区域，其他近处区域没有草 | 先计数候选，再按容量确定稳定随机密度，仍保留容量保护 |
| 草的包围盒只在构造时计算 | 修改地形变换后阴影范围失效 | 每次更新重新计算世界包围盒 |

未启用旧的角点裁剪函数：只看四个角点不能保守裁剪山峰，而且旧函数使用了与新 RHI 不同的深度范围。当前网格仍覆盖整个地形，VT 请求使用保守视锥裁剪。

## 页表与物理缓存

`GpuVirtualTexture` 把虚拟资源映射到固定大小的物理 atlas：

- 虚拟 tile 为 64×64，四周各带 2 texel apron，物理槽位为 68×68。
- 默认 atlas 有 8×8 个槽位，即 64 页。最粗的单页 mip 常驻槽位 0。
- 每个 mip 的页表依次竖向排列；RGBA32F entry 保存槽位 X/Y、mip 和有效位。最后两行存放地址参数。
- 请求的页不驻留时逐级查找父 mip，最终使用常驻页，缺页不会生成未定义高度或破洞。
- CPU 根据 tile 的保守高度柱、相机矩阵和当前窗口尺寸预测可见页；同时请求祖先 mip。需要的页超预算时提高 mip bias。
- 默认每次更新最多安装 8 页。当前请求集受到保护，其他非根页按 LRU 淘汰；旧 entry 在槽位复用后失效，避免地址别名。
- `writeTextureRegion` 只更新新 tile 的物理区域；页表在每个成功的页事务后发布。后续坏页读取失败时，已提交的页仍有一致的映射。

高度采样固定请求 mip 0，由页表决定可用的精度。材质根据 UV 导数计算 mip，并在两个级别之间混合。高度 UV 使用 `uv*(dimension-1)` 保留端点；材质使用 `uv*dimension-.5` 与图片 texel 中心一致。高度第一行对应局部 Z=-1，材质第一行在图片顶部，绘制时翻转材质 V。

五个材质层共用一个页表和槽位分配：底色、切线法线、金属度 B、粗糙度 G、AO R。粗 mip 的底色按项目现有 gamma 2.2 约定在线性能量中预滤波，法线重新归一化。`MaterialExtension` 改为 48 字节，新增明确的 feature 位；不会借用位移、SSS 等数值参数来标记 VT。此地形变体没有单独的 coat／anisotropy／thickness 特殊贴图层。

`SceneAdapter` 将页驻留版本纳入历史 key，页更新时重置时域历史。现有动态生成网格／草仍使用 reactive 标记，因此当前地形像素不累积 TSAA 历史；可靠的地形历史重投影需保留上一帧几何／LOD 状态，列入后续改进。

## 显存预算

以 8192² 虚拟尺寸、默认 2048 个叶节点和 64 页缓存计算，单位为 MiB：

| 项目 | 原实现 | 新实现 |
| --- | ---: | ---: |
| 生成顶点和索引 | 237.5 | 19.0 |
| 高度资源（含新页表） | 256.0 | 5.02 |
| 五层材质 atlas 与页表 | 随原图尺寸增长 | 6.15 |

新网格和两套 VT 合计约 **30.16 MiB**。这不是整个程序的显存统计，不包含 LOD 图、队列、草、阴影、G-buffer 或在途上传。页表随虚拟尺寸增长，物理 atlas 与叶节点预算不随原图分辨率增长。草默认 65,536 个 pose，约 4 MiB。

## 输入与离线分页

支持三种来源：

1. **旧 `heightMap.txt`**：当前历史场景约定为 8192² little-endian float32。先分块扫描高度范围与有限值，再按所需页读行跨度，不保留完整高度数组。粗级别采用端点保持重采样，适合兼容输入；高频地形推荐预滤波 pack。
2. **CPU 数组／普通图片**：便于程序场景和旧材质；GPU 分页，CPU 输入／图片 mip 链仍驻留。不能把这个路径称为完整的 CPU 流式加载。
3. **VT pack**：高度和材质都只读取请求的 tile，运行时不解码整张源图。推荐大场景使用此路径。

离线工具依赖 Python、NumPy 和 Pillow：

```bash
python3 -m pip install numpy Pillow
python3 tools/bake_terrain_vt.py \
  --height asset/terrain/heightMap.txt --width 8192 --height-size 8192 \
  --albedo asset/terrain/albedo.png --normal asset/terrain/normal.png \
  --metallic asset/terrain/metallic.png --roughness asset/terrain/roughness.png \
  --ao asset/terrain/ao.png --output asset/terrain/vt
```

输出 `height.json`、`material.json` 及各自带内容 hash 的 `.tiles` 文件。先完整写入不可变数据，再原子发布 manifest；烘焙失败不会截断旧 pack。旧数据文件可在确认无 manifest 或运行中的 source 使用后清理。高度支持非方形输入，映射到覆盖整个地形 UV 的方形 2 的幂尺寸；高度 mip 先低通再保留端点重采样。缺少的材质图使用白色底色、平面法线、零金属度、全粗糙度和全 AO 默认值；pack 的标量系数乘在这些层上。工具在离线处理中使用完整材质 mip 数据，烘焙大图时需要相应的 CPU 内存。

地形 JSON 示例（此对象作为场景的 `terrain` 文件）：

```json
{
  "heightVT": "asset/terrain/vt/height.json",
  "materialVT": "asset/terrain/vt/material.json",
  "maxLeaves": 2048,
  "grass": "true"
}
```

`heightVT` 替代旧 `heightMap`，`materialVT` 替代五张完整 GPU 贴图。材质层使用现有 Material 的标量控制；保留旧 `material` 路径配置时，pack 路径只记录文件名，不解码这些完整图片。输入尺寸上限为 16384，页大小／apron 必须与 pack 版本 1 一致，加载时校验文件总长度与 plane 格式。

直接编辑 CPU `heightData` 或原材质图片后，调用 `TerrainComponent::invalidateHeight()` 或 `SceneAdapter::invalidateAssets()`。变更 source 路径／预算自动触发缓存重建。运行时源文件应保持不变；替换文件后重建 source。pack manifest 不能与不同版本的 tile 数据混用。

## 运行与验证

```bash
./build/Scene-Renderer --classic terrain
./build/Scene-Renderer --render-gallery img/metal terrain
ctest --test-dir build --output-on-failure
python3 tests/test_bake_terrain_vt.py
```

新示例生成 1024² 高度与底色，不需要下载资源；示例为了展示几何使用 4096 叶节点，默认业务预算仍为 2048。画廊保存 `terrain.png` 和 `terrain-wireframe.png`，使用相同相机与时间。

自动验证覆盖：

- RGBA8／RGBA32F 部分上传只修改指定区域；非法尺寸、越界、空指针、字节数和缺少 CopyDestination 被拒绝。
- 固定页数、每帧上传数、LRU 淘汰、根页常驻、无槽位别名、GPU 缺页回退及端点采样。
- 非方形高度重采样、apron、高度文件流式页与 CPU 原图逐字节一致、pack 读取。
- GPU 四叉树与独立 CPU 距离参考、平面高度及边界法线、非平坦网格（包含强制 LOD 0/5 相邻）的总面积／绕序／内部边配对、索引范围与预算保护。
- VT 底色与材质系数进入 G-buffer，草贴合相同高度、密度容量、风动和间接实例绘制。
- 离线工具的 gamma 预滤波、法线归一化、apron、非方形平面 mip，以及截断／NaN 源拒绝。

平台回归在 Apple M4 上使用原生 Metal API／Shader Validation；Vulkan 使用 MoltenVK 数值与渲染测试。本机没有 Khronos validation layer。MoltenVK 的 MetalTools 调试组合存在已知阻塞，因此 Vulkan 回归关闭该组合。

2026-10-03 最终验证：Metal CTest 8/8、Vulkan CTest 9/9、离线烘焙工具 3/3 通过。两个 GPU 后端均通过强制 LOD 0/5 相邻的闭合网格验证；原生 Metal 画廊输出的地形与线框图片已检查。

## 当前边界与下一步

当前是 **CPU 可见性预测 + 有预算的同步页读取／上传**，没有 GPU feedback buffer、后台 IO job 或硬件 sampler feedback。页表正确性、显存上限与缺页回退已实现，但快速移动相机时可能出现短暂的粗 mip／高度变化；远处高频高度需要预滤波 pack，几何没有跨 mip 的形变过渡。

建议后续优先实现异步 IO 与上传队列，再加入屏幕反馈、阴影／反射视角请求、保守的每块 min/max 高度与屏幕误差 LOD。当前 LOD 仍用固定 5–50 场景单位距离和原子预算分配，预算耗尽时分配顺序由 GPU 执行次序决定；本次测试验证的是容量及闭合几何，不承诺最优的细节分配或帧间相同叶集合。

参考：[GPU Gems 2：Tile-Based Texture Mapping](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-12-tile-based-texture-mapping)。
