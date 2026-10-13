# 详细功能展示与对照

[English](rendering-showcase.md) · **简体中文** · [返回项目首页](../README.zh-CN.md)

这里保留从 README 移入的完整展示、算法说明和性能对照，按主题重新组织。首页展示精选结果；[完整图集与参数](rendering-gallery.md)保留采样配置和复现步骤。各项数据对应原捕获的相机、构建和配置，不能直接当作当前默认内存保护下的性能结果。2026-10-13 岩洞改动已构建并通过内存／RHI 测试，新批次光栅、内存峰值和新增视角仍待 GPU 验收。

项目输出按各段标明后端、积分方式和降噪配置；Unreal Nanite、PBRT 与 Blender Cycles 图片单独标为参考引擎结果。

[材质与光照](#材质与光照) · [大气与自然环境](#大气与自然环境) · [水体与潜水](#水体与潜水) · [几何与可见性](#几何与可见性) · [后处理](#后处理) · [路径追踪](#路径追踪)

## 材质与光照

![此前启用 RSM 的 Sponza 演示](../img/metal/sponza.png)

该 Sponza 捕获启用了 RSM；当前默认关闭，保留为历史光照演示。

### 经典场景与 PBR

Sponza、San Miguel 与 Sibenik 用于观察建筑材质和阴影。现有太阳／天空 RSM 对照保留为[可选光照演示](rsm.md)，RSM 默认关闭，不再持续开发；Stanford 扫描模型及 Damaged Helmet 展示金属、非金属与纹理材质。

| San Miguel 庭院 | Sibenik Cathedral |
| --- | --- |
| ![Metal San Miguel](../img/metal/san-miguel.png) | ![Metal Sibenik Cathedral](../img/metal/sibenik.png) |

| Stanford Bunny：三种材质 | Damaged Helmet：PBR 纹理 |
| --- | --- |
| ![Metal Stanford Bunny](../img/metal/bunny.png) | ![Metal Damaged Helmet](../img/metal/helmet.png) |

### 实时／PT 共用各向异性材质

Metal／Vulkan 实时 Forward、Deferred 和 Scene 着色现已共用 PT 的各向异性 GGX 反射参数。直接光以 CPU BSDF 对照验收，天空反射采用固定预算近似。下图为方向光下的实时样例：各向同性、各向异性、旋转 90°；[共享范围与验收](path-tracing-materials.md#m1-r实时渲染与-pt-共用材质输入)。

![实时各向异性 GGX 反射](../img/path-tracing/realtime-anisotropic-ggx.png)

### 环境遮蔽 AO

地平线积分 AO 配合几何法线重建与边缘保留滤波，减少柱脚和拱廊附近的采样条带。以下 Sponza 对照关闭 RSM 与 TSAA，保持相同光照和曝光，单独展示 AO 的效果。[算法说明与更多对照](ambient-occlusion.md)。

| 原 24 点 SSAO：AO 缓冲 | 地平线 AO + 边缘保留滤波：AO 缓冲 |
| --- | --- |
| ![Sponza 原 SSAO 可见性](../img/ao/sponza-ao-legacy-visibility.png) | ![Sponza 地平线积分与滤波后的 AO](../img/ao/sponza-ao-gtao-visibility.png) |

| AO 关闭：最终画面 | AO 开启：最终画面 |
| --- | --- |
| ![Sponza 关闭 AO](../img/ao/sponza-ao-off.png) | ![Sponza 开启地平线 AO](../img/ao/sponza-ao-gtao.png) |

### 材质过滤与改进 TAA

原生 Metal／Vulkan PBR 材质新增按语义生成的 mip 链、三线性和 8× 各向异性过滤、alpha 覆盖保持，以及近似法线方差过滤。高质量 TAA 重建固定输出像素网格，加入当前／历史双三次采样、深度边缘检查与运动权重；在 **Enable TSAA** 下切换 **TAA high quality**。Sponza 帘布高频误差相对上一版开启 TAA 的捕获下降 **41.43%**。[实现、游戏引擎 AA 方案、移动视角、GPU 成本与限制](realtime-anti-aliasing.md)。

![旧 TAA／新过滤与 TAA／未改变的 raw PT 参考](../img/diagnostics/aa/metal/sponza-upgrade.png)

### 动态辐照度探针 DDGI：实时 GI 第一阶段

新增可选的离线 PT 探针捕获，共享现有积分器、场景缓冲与 Metal 原生求交；增加可开关的拓扑约束空间聚类 LOD，用于构建简化 PT 世界，编辑器 PT 面板可设置位移和 UV 误差。[PT 探针、自动 LOD、对照与限制](pt-probes-and-world-lod.md)。编辑器 DDGI 仍默认使用光栅捕获，PT 探针目前通过参考捕获命令使用。 Sponza 独立辐照度误差从 **37.25% 降到 19.79%**；聚类世界减少 **15.1%** 三角形和原生 AS 内存。三轮交替 PT 测试中位墙钟缩短 2.18%，范围仍重叠，尚不能宣称稳定吞吐加速。


可开关的本地探针场提供场景遮挡、天空间接光和漫反射染色。首阶段采用 GPU 六面光栅捕获、8×8 辐照度和 16×16 距离矩，支持漏光控制、时间反馈与每帧更新预算；默认缓存 3.135 MiB，硬限制 64 MiB。[实现、更新延迟、验证和阶段限制](ddgi.md)。通过 `--classic cornell --ddgi` 启动，或在原生编辑器的 **DDGI diffuse global illumination** 面板打开。

| DDGI 关闭 | DDGI 开启：可见性与反弹染色 |
| --- | --- |
| ![受控房间关闭 DDGI](../img/diagnostics/ddgi/metal/room-off.png) | ![受控房间开启 DDGI](../img/diagnostics/ddgi/metal/room-on.png) |

Sponza 使用场景预设：1,280 个探针、2 m 间距、12.539 MiB 缓存，每帧更新 8 个。[真实场景验证与开销](ddgi.md#sponza-场景测试)。通过 `--classic sponza --ddgi` 启动，截图自动隐藏编辑器 UI。

通过 `--classic sponza --ddgi-probes state` 或 `--ddgi-probes irradiance` 查看实际 GPU 探针。编辑器支持建筑遮挡、X-ray 和单独 Y 层；绿色表示活动、红色表示拒绝、黄色表示未访问。[原理、位置设置与分层图](ddgi.md#原理与-sponza-探针布局)。显示默认关闭，不进入光照或 TAA 历史。

![Sponza 原生 GPU 探针分类，无 UI](../img/diagnostics/ddgi/probes/metal/sponza-probes-state.png)

| Sponza：DDGI 关闭 | Sponza：DDGI 开启 |
| --- | --- |
| ![Sponza 关闭 DDGI](../img/diagnostics/ddgi/metal/sponza-off.png) | ![Sponza 开启 DDGI](../img/diagnostics/ddgi/metal/sponza-on.png) |

[Sponza raw PT 参考对照](ddgi.md#sponza-路径追踪参考对照)：同快照 480×270、2,048 spp／16 bounces。开启 DDGI 后，对齐内区线性 HDR 的 normalized L1 从 **80.46% 降至 49.72%**；47 个独立辐照度查询仍有 **34.76%** 误差。近似和采样限制见报告，第一阶段 GI 精度仍未达到 PT 参考。

上面的数值关闭了 TAA。冻结同一份探针场并开启原生 TAA 后，对齐内区误差为 **44.55%**，帘布高频残差下降 **55.98%**。拱门辐照度偏亮和未遮挡的天空镜面仍需修复；两条路径共用材质资源，但 legacy BRDF／法线求值尚未完全统一。[抗锯齿与光照消融](ddgi.md#抗锯齿材质一致性与拱门偏亮)。

![DDGI：TAA 关闭／开启／raw PT 参考](../img/diagnostics/ddgi/metal/gt/aa/sponza-aa-comparison.png)

## 大气与自然环境

### 天空与太阳

大气散射与解析太阳盘共享光照参数，驱动场景、间接光照和海面。远景云层支持天气分布与风速变化。

| 白天天空 | 地平线日落 | 日落云层 |
| --- | --- | --- |
| ![Metal 白天天空](../img/metal/sky-day.png) | ![Metal 日落太阳](../img/metal/sky-sunset.png) | ![Metal 日落体积云](../img/metal/clouds-sunset.png) |

### 可穿越三维体素云

128³ XYZ 密度场结合保守距离场、GPU 间接步进和太阳光照缓存，支持靠近及进入云体。下图采用 1080p 全分辨率，关闭云时域累积；风暴预设包含内部体积闪光。

| 三维云体 | 风暴形态与内部闪光 |
| --- | --- |
| ![Metal 三维体素云](../img/metal/cloud-volume.png) | ![Metal 三维风暴云](../img/metal/cloud-vortex.png) |

### 大地形、湖泊与植被

Mountain Lake 展示 8×8 km 山湖地形：高度与材质 VT 分页、LOD、FFT 湖面、随距离变化的草地密度，以及湿沙和沙滩 PBR 材质。

![Metal Mountain Lake 山湖场景](../img/metal/mountain-lake.png)

| 湖岸草地 | 沙滩与湿沙 |
| --- | --- |
| ![Metal 湖岸植被](../img/metal/mountain-lake-ground.png) | ![Metal 湖岸沙滩](../img/metal/mountain-lake-beach.png) |

环境面板统一驱动太阳／风／云量／降雨，16×16 地表单元保守沿坡转移水量；实时几何、水面捕获与冻结的 CPU／GPU PT 共用空间湿度。固定 tick 检查点支持暂停和精确重放。新增可独立开关的地面云影、解析空气消光／近地雾，接入地形与水体几何捕获；大气空中透视通过相机视锥 LUT 增加随太阳变化的 Rayleigh／Mie 散射与 RGB 透射，并查询山体／云对太阳散射的遮挡；地面云影强度可单独调低；近似 P1 PT 可导出冻结云天空和独立空间太阳透射场。水面云倒影、湖位耦合及雨滴碰撞／飞溅仍待后续。[天气操作](mountain-lake-environment-spec.md) · [L4 对照、验证与限制](weather-lighting.md) · [大气远景雾](atmospheric-aerial-perspective.md)。

本次开发版实景加强大气空中透视，并减弱固定颜色的近地雾与地面云影：地面云影强度 **30%**，aerial 光学距离缩放 **2**，山体阴影范围 **8 公里**。山体／云对太阳的遮挡参与空气散射，并修正天空中的局部太阳贡献。下图视线与太阳方向夹角为 **90.0°**，太阳高度约 **20°**；侧光表现为较宽的空气阴影带和柔和光束。**Metal、960×540、固定模拟时间、RSM 关闭。** [控制项、验证、捕获来源与限制](mountain-lake-side-light.md)。

![山湖 90° 太阳侧光与较弱近地雾](../img/diagnostics/environment/aerial-side/mountain-lake-side-combined.png)

<details>
<summary>空气太阳遮蔽：关闭／开启／开启并叠加较弱近地雾</summary>

![相同 90° 视角下的空气太阳遮蔽对照](../img/diagnostics/environment/aerial-side/comparison.png)

三组保持相机、太阳和曝光不变。中间加入空气太阳遮蔽，右侧进一步加入减弱后的高度雾。窄光束、视锥外山体遮挡和运动序列仍需后续完善；此图不代表已达到 60 fps。

</details>

![云影与空气雾对照](../img/diagnostics/environment/l4/weather-overview.png)

![空间地表湿度 GPU 对照](../img/diagnostics/environment/l3-spatial/raster-overview.png)

### 程序树木与森林：修复后的渲染

阔叶树、针叶树和灌木采用种子分枝几何，树皮与叶片独立材质，并接入 alpha-tested 级联阴影。下图为 **Metal 实时渲染，960×720、固定时刻 8 s、16 帧 TAA**，已在 **2026-10-12** 修复叶片缝隙中的太阳圆盘伪影与叶片材质标记漏传后重新生成。同机位、同曝光下，单树树冠区域接近纯白的像素从 **854 降至 0**；Metal、Vulkan 回归通过。[实现、修复前后对照、验证与限制](procedural-trees.md)。

| 修复后的单树 | 修复后的森林 |
| --- | --- |
| ![修复后的 Metal 单树渲染](../img/nature/tree-single.png) | ![修复后的 Metal 森林渲染](../img/nature/tree-forest.png) |

### Virtual Texture 与软阴影

高度／材质 VT 使用物理页缓存、页表、祖先回退与 GPU 深度反馈；五级 CSM 分配近远阴影精度，PCSS 估计随遮挡物距离变化的半影。

| VT 实际 GPU 物理缓存 | CSM 阴影与级联分区 |
| --- | --- |
| ![VT 高度与材质物理图集](../img/diagnostics/vt-cache.png) | ![CSM 级联与过渡带](../img/diagnostics/csm-cascades.png) |

![PCF、默认太阳 PCSS 与放大光源 PCSS 对照](../img/diagnostics/pcss-comparison.png)

PCSS 对照依次为 PCF、默认太阳和放大光源；第三列用于展示更宽的半影。[页表、阴影图集与完整诊断](render-diagnostics-gallery.md)。

## 水体与潜水

### FFT 海洋与透明水体

主波与短波 FFT 叠加，表现浪峰、细小波纹与泡沫；折射、RGB 吸收和近似单次散射表现浅水透射与透亮浪尖。

| 大浪海面 | 浅水折射与散射 |
| --- | --- |
| ![Metal FFT 海洋](../img/metal/ocean.png) | ![Metal 透明水体](../img/metal/ocean-clear.png) |

实时水面已加入近景密集网格、独立水下颜色／位置层，以及沿水中路径的单次散射积分。[前后对比、验证与 GPU 耗时](water-realtime-upgrade.md)。

海岸预设新增可开关的 DDA 折射与地形回退、局部多次散射、浅水波、持久泡沫和湿沙；旧预设保持新增效果关闭。[开关、对照、动画与限制](water-coastal-features.md)。 [第一轮性能报告：分阶段耗时、提交间隙与边界修复](water-coastal-performance.md)。

![Metal 近岸浅水波与泡沫](../img/metal/coastal-beach.png)

水下视角加入沿距离的吸收／散射、水出空气折射及全反射。用 `--classic coastal-underwater` 打开，Ocean 面板可切换水下视角、雾和 **Short wave ripples**。水下预设加入 0.5–2 米 FFT 短波，使近处折射与 Snell 窗口边界随波面起伏。[图像、光学路径与验证](water-underwater-rendering.md)。

| 水下观察海面 | 俯视水底 |
| --- | --- |
| ![Metal 水下海面](../img/diagnostics/water/ripple-demo/coastal-underwater.png) | ![Metal 水下水底](../img/diagnostics/water/seabed-demo/coastal-underwater-seabed.png) |

海床示例加入 25 cm 沙纹、细微法线与穿过实时 FFT 波面的太阳焦散。用 `--classic coastal-seabed` 打开，在 Ocean 面板切换 **FFT seabed caustics** 并调整 **Caustic strength**。[海床对照、实现与 GPU 耗时](water-seabed-caustics.md)。

### 海底潜水、太阳光束与水面折射

`underwater-dive` 预设从水下 6 米开始，沿礁石与海草之间的沙质通道观察。距离吸收与单次散射形成蓝绿色能见度；漂浮颗粒、覆盖地形和水下模型的 16／48／128 米级联焦散，以及随波面聚焦的太阳光束，表现海底潜水的空间与动态光照。预设使用 32 步带阴影的体积光积分。

| 海底潜水：太阳光束与海床焦散 | 抬头观察：折射天空、太阳与浮标 |
| --- | --- |
| ![Metal 海底潜水与太阳光束](../img/diagnostics/water/air-refraction/underwater-dive.png) | ![水下折射的天空、太阳与浮标](../img/diagnostics/water/air-refraction/underwater-dive-snell-window.png) |

```sh
./build/Scene-Renderer --classic underwater-dive --size 1280x720
```

按住鼠标右键转头，**W/A/S/D** 移动，**Q/E** 垂直移动。将 **Camera → View pitch** 设为约 **68°** 可观察天空窗口；接近水平的水下视角会出现全反射，窗口边界随波面法线移动。

Ocean 面板可独立切换 **Underwater distance fog**、**Underwater sun shafts**、**FFT seabed caustics** 和 **Wide air refraction**，并调整光束对比度、颗粒密度与体积步数。宽空气视图补充原相机视野外的水上物体；潜水预设将散射增益设为 1，保留天空与物体的对比。Metal／Vulkan 水体验证通过。宽视图仍是屏幕空间近似，当前预设优先展示画质，尚未达到 60 fps 目标。

[潜水场景与操作](water-underwater-diving.md) · [焦散覆盖范围](water-caustic-cascades.md) · [太阳光束实现](water-sun-shafts.md) · [折射对照、验证与性能](water-air-refraction.md)

水体新增默认关闭的自适应半分辨率散射，并修复捕获关闭查询退化及编辑器反馈拆分提交。[实测结果、线程调度与 RDG 评估](water-runtime-optimization.md)。

## 几何与可见性

### 可见性、剔除与层级几何

实时 RenderGraph 现支持可关闭的无效 pass 剔除：从显示输出和显式历史／副作用追溯子资源数据依赖，重建生命周期；普通场景关闭 TAA 时可剔除无用 motion。[算法与验证](render-graph-culling.md)。

实时物体视锥裁剪默认开启；可选 GPU 三角形裁剪通过稳定索引压缩减少 indirect draw 的数量，支持静态缓存和生成网格 active dispatch。验证网格的物体 3 → 1、三角形 16,384 → 2,312，像素一致；三角形路径需按场景计时选择。[实现、开关与验证](geometry-culling.md)。

新增可开关的分层 Cluster 几何：128 三角形 meshlet、保留组边界的 endpoint QEM 简化、GPU 屏幕误差 LOD 选择、cluster 视锥剔除和 indexed indirect 绘制；建设在有界 CPU worker 上执行。这是参考 Nanite 核心思路的原型，分页流送、可见性缓冲着色和混合光栅仍未实现。[算法、开关与验证](hierarchical-cluster-geometry.md)。

跨网格 cluster 批处理将 66 次 dispatch 合并为 3 次。可选两阶段 Hi-Z 复用早期 G-buffer 深度，并通过当帧复查与晚期补绘恢复表面。四分之一分辨率起始层、多级融合归约及协作式晚期追加将两阶段计算从 16 次 dispatch／16 个 encoder 降到 **9 次／2 个**。Sponza 静态关键 GPU 窗口仍比无遮挡慢约 **3.2%**；移动镜头的汇总中位数与逐帧配对结论相反，尚未确认稳定收益，因此通用设置的遮挡默认关闭；岩洞等场景可覆盖此预设。百万三角形扫描模型的三副本遮挡压力场景降低约 **21%**。显示像素、深度与六张 G-buffer 均通过同 LOD 对照。[最新优化、实测限制、实际分块与动态 LOD 全流程](cluster-hiz-optimization-and-lod.md)。

![Sponza 实际 cluster LOD 与三角形线框](../img/diagnostics/cluster-geometry/metal/overview.png)

![Sponza：实际早期与最终 G-buffer albedo](../img/diagnostics/cluster-two-stage/metal/two-stage/camera-return/attachments.png)

![Stanford Dragon：固定 4 px 预算的动态 LOD、实际 cluster 分块与层级](../img/diagnostics/cluster-hiz-optimized/dragon/flow/dynamic-lod.gif)

Cluster 构建现在将精确位置拓扑与原始属性地址分离，用 Morton 顺序和有界空间补齐提高利用率；父表示至少减少 12.5% 的子三角形才保留。UV／切线接缝继续保护，并限制累计法线偏差。[构建方法、原生对照、逐帧计时及限制](cluster-partition-and-hierarchy.md)。

**分块算法的思想。** Attribute BFS 将三角形当作图节点，按完整属性顶点地址建立共享边邻接，再用广度优先遍历填满最多 128 个三角形的块。UV／法线／切线接缝可能让几何连续的表面在图中断开，留下小块。Position topology 为精确相同的位置额外建立几何编号，用它寻找邻居，同时保留所有原始属性地址。Morton 空间排序选择局部种子；候选优先复用已有属性顶点，其次控制包围盒大小，邻居耗尽时在有限范围内补齐附近碎片。分块提高装满程度，本身不减少源三角形。

**2 px／4 px 的含义。** **Cluster LOD error (pixels)** 是允许的几何位置偏差投影到实际渲染视口后的误差估计。透视相机下近似为 `位置误差 × 实例缩放上界 × 像素焦距 / 源覆盖包围盒最近深度`，分母至少取近裁剪距离。例如父级估计误差为 3.2 px，2 px 预算会保留更详细的子级，4 px 则可接受这个父级。

| 像素预算 | 通常的 LOD 行为 |
| --- | --- |
| 0 px | 保留详细叶块，拒绝位置误差非零的父级 |
| 2 px | 近似更严格，通常绘制更多三角形 |
| 4 px | 允许更大的几何近似，通常绘制更少三角形 |

这里衡量几何位置，不能当作最终 RGB 误差或 meshlet 宽度：高光、阴影和法线贴图可能放大小幅形状变化。同一相机下，视口宽高都加倍，同一偏差占据的像素数大约加倍，因此固定 px 预算会倾向于选择更详细的 LOD。调大预算只能选择已经建好的父级。当前父构建最多合并八个连通子组，用 endpoint QEM 简化并保守继承逐顶点误差，继续保护原始边界与属性接缝。[父构建、质量取舍与实际 GPU cut](cluster-parent-lod-optimization.md)。

下面的 Sponza／Stanford 数据使用上一版配对父组与 0.05 法线上限；当前 Coastal Cliff 父构建参数与结果单独列在后面。

| 场景 | 叶块利用率：优化前→后 | 层级内存：优化前→后 |
| --- | ---: | ---: |
| Sponza | 22.9%→91.0% | 16.66→3.39 MiB |
| Stanford Dragon | 63.9%→95.7% | 42.64→17.34 MiB |
| Happy Buddha | 62.8%→96.2% | 53.42→21.96 MiB |

固定 4 px、每模式 72 样本，逐帧配对的 cluster／geometry GPU 窗口分别降低 **3.4%、2.6%、3.4%**；这是部分 GPU 耗时。新切面保留更多三角形，与独立 CPU cut 一致；Hi-Z 的深度、HDR、显示图及六张 G-buffer 与同 cut 一致。Dragon 相对完整几何仍有 **21.2%** HDR LOD 误差，Sponza 的 0 px 有六个显示像素不同。Sponza 开启 Hi-Z 仍慢于自身无遮挡路径，因此遮挡保持默认关闭。

![Sponza：分块优化前后、实际 GPU LOD 与源画面对照](../img/diagnostics/cluster-partition/sponza/comparison.png)

![Dragon：实际分块与 PBR 细节优化前后](../img/diagnostics/cluster-partition/dragon/detail.png)

### 高密度程序化石灰岩洞

可穿行的四段岩洞包含破碎岩壁、石拱、弯曲钟乳石、矿物岩柱和落石。凹凸直接位移真实顶点，使用共享 icosphere 拓扑与连续投影 UV。**9 个唯一网格／2,883,584 个唯一三角形**，实例化为 **76 次绘制／24,838,144 个逻辑三角形**。二进制 PLY 与 1024 像素程序纹理总计 **84.8 MB**，生成和加载都检查 **10 GB 磁盘上限**，无需额外下载模型。

![程序化岩洞：原生 PBR、真实 cluster ID 与选中 LOD](../img/diagnostics/cluster-cave/partition.png)

![程序化岩洞：从近到远的真实分块和动态 LOD](../img/diagnostics/cluster-cave/dynamic-lod.png)

![程序化岩洞：已捕获的 GPU 切面与 LOD 三角形分布](../img/diagnostics/cluster-cave/dynamic-lod.gif)

[局部分块边界与原生三角形线框](../img/diagnostics/cluster-cave/detail.png) 可以区分 meshlet 色块与色块内部更小的三角形。动画展示离散捕获视角，播放速度不代表渲染帧率。

1280×720／Metal，固定 4 px 预算，提交三角形相对各自视角的 0 px 参考减少 **近景 25.57%／中景 33.94%／远景 46.28%／更远 54.64%**。关闭物体与遮挡剔除，单独展示 LOD；这些比例不能当作帧率收益。**9 个共享层级占 67.35 MiB**，均未触及各自构建预算。七个视角的 GPU 索引与独立 CPU 切面完全一致，ID／层级图深度与选中 PBR 几何逐值相同。入口视角的 HDR 相对 RMSE 为 **1.17%**。[同视角画质比较](../img/diagnostics/cluster-cave/quality.png)、[精简捕获数据](../img/diagnostics/cluster-cave/summary.json)。

这些图片与动画来自**内存保护接入前**已验收的原生捕获。新捕获路径复用一个渲染器生成参考／LOD 图，每批最多 **65,536 个三角形**，默认预算下预留 **24 MiB 临时内存**，完成后释放该批 GPU 缓冲；大规模成员表逐行写入 JSONL。仍保留默认**进程 3 GiB／GPU 1.5 GiB**预算和系统压力拒绝机制。新增两个洞内大厅视角，以及同视角 **0／2／4／8 px** 对比路线。**新版本的受保护 GPU 捕获待运行验证**；已完成构建和四项内存／RHI 测试。[捕获步骤和验收范围](cluster-cave.md)。

```sh
python3 tools/prepare_cluster_cave.py --level 7
./build/Scene-Renderer --classic cluster-cave --size 1280x720
# 独立的小规格目录，保留高密度源模型：
python3 tools/prepare_cluster_cave.py --level 6
./build/Scene-Renderer --memory-info
# 系统内存压力恢复正常后运行：
./build/Scene-Renderer --cluster-scene-flow cluster-cave-small build/cluster-cave-small --backend Metal
python3 tools/visualize_cluster_scene_compact.py build/cluster-cave-small img/diagnostics/cluster-cave-small
```

### Bistro：完整街区的 cluster 分块与 LOD 演示

免费 [ORCA Bistro](https://developer.nvidia.com/orca/amazon-lumberyard-bistro) 外景包含整条街道、建筑、商铺、路灯和植被：**2,832,120 个源三角形／132 个材质网格**。保留原始 FBX 几何；演示使用最长边 512 像素的纹理和本引擎太阳／天空光照。作者 Amazon Lumberyard，2017，**CC BY 4.0**；[署名与转换说明](../samples/licenses/bistro.txt)。

1280×720／Metal 捕获前等待 **117 个网格层级**全部发布：**25,565 个叶 meshlet**，每块最多 128 个三角形，叶块利用率 **86.52%**，唯一层级数据 **40.74 MiB**。彩虹色表示实际 GPU 选中的 meshlet；层级色表示所属父组：**蓝／青色 = 叶级 0，绿色 = 1，黄绿色 = 2**。灰色为不走 cluster 管线的小网格。保留原始透明裁切，截图全部收起 UI。

![Bistro：完整细节与 4 px 的 PBR、实际 GPU 分块和 LOD 层级](../img/diagnostics/bistro/flow/partition.png)

![Bistro：沿街近景至高处远景、实际选中分块与 LOD 三角形分布](../img/diagnostics/bistro/flow/dynamic-lod.gif)


<details>
<summary>为什么这些 cluster 看起来像规整的矩形？</summary>

Meshlet 按**三角形数量预算**分组，不规定固定的世界空间边长。Bistro 有较大的平面和重复建筑构件，纹理细节不会增加源三角形。当前按材质网格分别构建，优先共边邻居和顶点复用，Morton 用于选择种子；有界空间补齐也允许把断开的几何片装入一个 meshlet。实际拾取的三个 cluster 分别有 **128 个三角形／4 个共边连通片**、**26／1**、**128／2**，因此当前分块尚未保证每个 meshlet 都是单个连续的表面片。

层级颜色表示 **LOD 级别**，许多不同 meshlet 会同为蓝色或绿色。现在的白线来自**全局唯一 GPU raster ID 纹理**的可见编号变化，包含遮挡交界，通过编号解码而非 RGB 边缘猜测；黑线是真实三角形的原生线框，并与选中切面的填充深度测试。此前 `packet * 997 + cluster` 的显示编号在街道完整切面中有 **1,048 次冲突**，其中 **3,135 个实际可见 cluster** 内有 **20 次**；全局前缀编号已消除这些冲突。七个视角的 PBR 图和 0 px 参考图与此前捕获逐字节相同。

![Bistro：可见 cluster 编号边界与真实三角形边缘](../img/diagnostics/bistro/flow/boundary-detail.png)

[拾取 cluster 的完整几何与连通片](../img/diagnostics/bistro/flow/surface-blocks.png)使用原索引范围的主平面投影，包含隐藏三角形；[ID 验证与连通性数据](../img/diagnostics/bistro/flow/boundary-summary.json)。

</details>

远景路线升到屋顶上方，避免相机退入建筑。下表统计 cluster 管线提交量，**关闭视锥和 Hi-Z 剔除**以单独展示 LOD；每个减量都与**该视角的 0 px 切面 2,831,143 个三角形**比较。另有 958 个小网格三角形保持原绘制，预处理移除 19 个退化源三角形。四个移动视角固定 **4 px** 预算。

| 视角 | 选中 cluster | 提交的 cluster 三角形 | 同视角 LOD 减量 |
| --- | ---: | ---: | ---: |
| 完整细节／0 px | 25,565 | 2,831,143 | 0.00% |
| 近景／街道 | 25,040 | 2,747,589 | 2.95% |
| 中景／街道 | 24,907 | 2,727,225 | 3.67% |
| 远景／抬高 | 24,682 | 2,697,501 | 4.72% |
| 更远／俯瞰 | 24,628 | 2,688,493 | 5.04% |

Bistro 的 LOD 减量较小：**20,762 个**候选父组中只有 **946 个**通过简化门槛，透明裁切／双面材质保护另使 **287,118 个三角形**停留在叶级。场景有较多属性接缝和小建筑碎片，所有层级均未触及内存预算。它适合测试多网格场景，可简化的密集连续曲面少于扫描岩石；上述提交量不能当作 FPS 收益。

七个视角的 GPU 索引均与独立 CPU 选择完全一致，成员计数与实际 GPU indirect 参数一致；分块图／层级图深度与该切面的 PBR 几何**逐值完全相同**。LOD 画质仍有近似误差：固定街道视角相对 0 px 切面的 **HDR 相对 RMSE 为 3.60%**，像素预算不约束最终着色误差。[同视角画质与放大差分](../img/diagnostics/bistro/flow/quality.png)、[整个街区](../img/diagnostics/bistro/flow/overview.png)、[立面局部放大](../img/diagnostics/bistro/flow/detail.png)、[层级图例](../img/diagnostics/bistro/flow/level-legend.png)、[捕获摘要](../img/diagnostics/bistro/flow/summary.json)和[完整 GPU 成员记录](../img/diagnostics/bistro/flow/flow.json.gz)。

<details>
<summary>复现 Bistro 分块捕获与交互式 LOD</summary>

将官方 Bistro v5.2 归档下载到 `samples/downloads/bistro-v5.2.zip`（**894 MB**，含室内的解压包约 **1.72 GB**）。源资产和转换包留在本地，不随 Git 提交。Python 工具需要 NumPy 和 Pillow。

```sh
python3 tools/prepare_bistro_scene.py --texture-size 512
cmake --build build --target Scene-Renderer -j2
./build/Scene-Renderer --cluster-scene-flow bistro build/bistro-clusters --backend Metal
python3 tools/visualize_cluster_scene_compact.py build/bistro-clusters build/bistro-clusters/flow --scene 'Bistro exterior'
./build/Scene-Renderer --classic bistro --backend Metal
```

应用中可调整 **Hierarchical cluster geometry**、**Cluster LOD error (pixels)** 和 **Cluster triangle wireframe** 查看原生 LOD。交互场景默认 2 px，上面的诊断捕获使用 4 px。

</details>

### Coastal Cliff 父层级 LOD 与 UE Nanite

CC0 **Coastal Cliff 峡谷**使用十个共享原始 LOD0 实例（**943 万逻辑三角形**）。连通父组和逐顶点保守误差继承将同视角 4 px 的 LOD 减量从 **3.24% 提升到 36.56%**，提交三角形 **885 万 → 580 万**。Apple M4／Metal、960×540 下，几何 GPU 窗口 **8.31 → 6.22 ms**，两阶段 Hi-Z **6.86 → 5.31 ms**，配对加速分别为 **24.58%／22.21%**。

法线上限由 0.05 调至 0.15，源图 HDR 相对 RMSE **0.578% → 1.181%**。静态 CPU cut／Hi-Z 输出完全一致；移动反向视角有两个深度竞争像素，由独立绘制顺序重放核验。唯一层级内存 **13.32 → 19.95 MiB**，构建 **2.88 → 8.19 秒**，0 px 扫描慢 3.76%。计时不包含阴影、光照和后处理。[消融、原生图像检查、原始计时与限制](cluster-parent-lod-optimization.md)。

![Coastal Cliff：父构建前后、实际 GPU LOD 与源图比较](../img/diagnostics/cluster-parents/cliff/benchmark/comparison.png)

![Coastal Cliff：相机远近变化与实际 GPU LOD](../img/diagnostics/cluster-parents/cliff/benchmark/optimized/flow/dynamic-lod.gif)

同一布局已在 Metal 上运行 **UE 5.8 Nanite**，验证了实际分块／三角形视图，以及每实例完整 943,284 三角形的 fallback。UE 图像用于几何对照，未做跨引擎光照和 GPU 性能比较。[素材准备与 UE 验证](cluster-cliff-nanite-test.md)。

![UE Nanite 在 Coastal Cliff 峡谷中的实际分块](../img/diagnostics/cluster-cliff/unreal/nanite-clusters.png)

可选当帧 Hi-Z 遮挡裁剪通过静态遮挡物 depth-only pass、保留天空孔洞的最远深度金字塔和 GPU indirect arguments，减少主视角实际几何绘制，支持与三角形裁剪组合。[实现与开关](hiz-culling.md)。

![Hi-Z GPU 可见性、开关对照与深度层级](../img/diagnostics/hiz-culling/hiz-overview.png)

这是带窗口测试场景的实际 Metal GPU 捕获：**红框**为被剔除物体，**绿框**为保留物体，**青框**为选中的遮挡物。非零主绘制 **69 → 17**，主三角形 **864 → 240**，另有 **48** 个三角形用于遮挡物预绘制；开启前后像素完全一致。移除遮挡物的视图是独立参考渲染，便于查看墙后的物体。

<details>
<summary>实际 GPU Hi-Z 深度金字塔：mip 0–9</summary>

![实际 GPU 最远深度金字塔](../img/diagnostics/hiz-culling/hiz-pyramid.png)

白色表示 clear depth **1**，窗口空隙也参与归约。9 次 GPU 层级归约与独立 CPU 最大值参考逐值一致；伪彩色使用统一拉伸范围，方便观察深度。[捕获数据与复现方法](hiz-culling.md#visualization)。

</details>

## 后处理

统一的原生 HDR 到显示模块支持可选 Bloom、景深、相机运动模糊、调色、FXAA、锐化、暗角、色差与颗粒。**Post processing** 面板提供独立参数、指数／ACES fitted／Reinhard／线性四种色调映射和轻量电影预设；新增效果默认关闭。[操作、管线、对照与限制](post-processing.md)。

| 默认显示 | ACES 与组合后处理 |
| --- | --- |
| ![Cornell 默认显示](../img/diagnostics/post-processing/cornell-post-off.png) | ![Cornell 组合后处理](../img/diagnostics/post-processing/cornell-post-combined.png) |

| HDR Bloom | 景深：前方箱体清晰 |
| --- | --- |
| ![Cornell HDR Bloom](../img/diagnostics/post-processing/cornell-bloom.png) | ![Cornell 景深](../img/diagnostics/post-processing/cornell-dof.png) |

```sh
./build/Scene-Renderer --classic cornell --size 1280x720
```

展开 **Post processing**，独立启用效果，或选择 **Soft cinematic preset**。Metal／Vulkan 验证通过；景深使用不透明深度，运动模糊目前支持相机运动。

## 路径追踪

CPU／Metal／Vulkan 路径追踪支持多次反弹、纹理材质、折射和均匀介质随机游走；CPU BDPT 用于平滑玻璃焦散。程序化地形、植被与 FFT 水面可冻结为离线场景。

CPU／Metal／Vulkan PT 已支持各向异性 GGX 反射。下图从左到右是相同金属的各向同性、anisotropy=0.8、anisotropy=0.8 且旋转 90°。**Metal PT，未降噪，480×160、256 spp、8 次反弹。** [材质参数与验收](path-tracing-materials.md)。

![各向同性、拉丝方向与旋转方向的 GGX 金属](../img/path-tracing/anisotropic-ggx.png)

CPU／Metal／Vulkan PT 已支持圆形纤维毛发散射，以及布料的粗糙漫反射、有界掠射 sheen 和薄片透射。样例包含 3,500 根锥形薄带毛发和垂落布料网格。**Metal PT，未降噪，640×360、256 spp、8 次反弹。** 原生曲线与动力学留待后续；[参数、范围与验收](path-tracing-fibers.md)。

![路径追踪毛发薄带与垂落布料](../img/path-tracing/hair-cloth.png)

绒毛与布料样例采用实时夕阳云天空捕获与 5° 低角度有限太阳照明。Stanford Bunny 使用 **72,000 根更细的短绒毛**；奶油色毛衣包含 **1,280 个真实正面纱线单元与 23,360 根细绒毛**，另有布料纱线近景。**Metal PT，768×768、1024 spp、12 次反弹，OIDN 展示图。** [照明、材质调整、未降噪图与验收](path-tracing-grooms-sunlight.md#夕阳展示)。Bunny 数据来自 [Stanford University Computer Graphics Laboratory](https://graphics.stanford.edu/data/3Dscanrep/)。

| 夕阳下的 Bunny 绒毛 | 夕阳下的针织毛衣 | 布料纱线近景 |
| --- | --- | --- |
| ![夕阳 Bunny 绒毛](../img/path-tracing/bunny-fur-sunset.png) | ![夕阳针织毛衣](../img/path-tracing/knit-sweater-sunset.png) | ![夕阳布料纱线](../img/path-tracing/knit-yarn-sunset.png) |

[YarnSim 纱线级针织样例](path-tracing-knitted-yarn.md) 导入公开的松弛纱线控制点，连接周期 spline span，并增加 CPU／Metal／Vulkan 共用的**各向异性介质纱线反光**。毛衣正面包含 **280 个蜂窝周期单元**；独立样片包含**三股几何绕线和真实针目孔隙**。**Metal PT，768×768、1024 spp、12 次反弹、OIDN 展示，夕阳照明。** 袖子与服装接缝仍为程序化原型。数据来自 [Leaf 等，2018](https://graphics.stanford.edu/projects/yarnsim/)，原始资产未重新分发。

| 毛衣正面蜂窝针织 | 三股纱线针织近景 |
| --- | --- |
| ![夕阳蜂窝针织毛衣](../img/path-tracing/knitted-sweater-sunset.png) | ![夕阳三股纱线蜂窝针织](../img/path-tracing/knitted-honeycomb-sunset.png) |

[独立头发 demo](path-tracing-hair-demo.md) 默认 **40,000 根发丝**，约为原样例的 **8.9 倍**，加入头皮分层与共用波浪发束导向。提供独立生成／渲染入口和三个参考视角。**Metal PT，768×768、256 spp、12 次反弹，OIDN 展示图。**

![高密度独立头发 demo](../img/path-tracing/hair-demo.png)

[Yuksel 公开头发基准](path-tracing-hair-benchmarks.md) 接入直发／波浪发及配套头模，每套保留全部 **50,000 根发丝**。**Metal PT，512×512、256 spp，OIDN 展示图。** 毛发来源：[Cem Yuksel](https://www.cemyuksel.com/research/hairmodels/)；头模由 **Murat Afshar** 提供。已加入独立 PBRT 方程检查和原生曲线参考图，整图外观尚未完成对齐。

| Yuksel 直发 | Yuksel 波浪发 |
|---|---|
| ![Yuksel 直发](../img/path-tracing/yuksel-straight.png) | ![Yuksel 波浪发](../img/path-tracing/yuksel-wavy.png) |

这组头发展示采用圆形纤维散射与实时大气捕获的晴空照明。低角度天空和太阳一起旋转、增强到 10 倍源强度，并采用更窄的发丝高光，呈现暖色太阳反射；每套保留 **50,000 根发丝，Metal PT，512×512、1024 spp、12 次反弹，OIDN 展示图**。头部为中性漫反射材质。毛发来源：[Cem Yuksel](https://www.cemyuksel.com/research/hairmodels/)；头模由 **Murat Afshar** 提供。[照明、材质参数、原始图与验收](path-tracing-hair-sunlight.md)。

| 强日光中的直发 | 强日光中的波浪发 |
|---|---|
| ![强日光直发](../img/path-tracing/yuksel-straight-sunlit.png) | ![强日光波浪发](../img/path-tracing/yuksel-wavy-sunlit.png) |

下面展示上文建筑与自然环境场景的路径追踪结果，除标明未降噪的 photon mapping 样例外，使用 Open Image Denoise（OIDN）降噪。Sponza 与 San Miguel 使用 Metal PT，分辨率为 320×240、64 spp、16 次反弹；地形、山湖与海洋使用 640×480 Metal PT，将程序化动画冻结在 8 秒时刻。采样配置与未降噪原图见[路径追踪图集](rendering-gallery.md#路径追踪)。

| Sponza：Metal PT＋OIDN，64 spp | San Miguel：Metal PT＋OIDN，64 spp |
| --- | --- |
| ![路径追踪 Sponza 中庭](../img/path-tracing/oidn-sponza.png) | ![路径追踪 San Miguel 庭院](../img/path-tracing/oidn-san-miguel.png) |

| Stanford Dragon：CPU BDPT 玻璃焦散＋OIDN | Jade Dragon：Metal PT 次表面散射＋OIDN |
| --- | --- |
| ![Stanford 透明龙与焦散](../img/path-tracing/dragon-glass.png) | ![半抛光玉龙](../img/path-tracing/jade-polished-boundary.png) |

| 晴天泳池池底：Metal photon mapping＋OIDN | 水上泳池视角：Metal photon mapping＋OIDN |
| --- | --- |
| ![晴天泳池池底焦散](../img/path-tracing/pool-sunlit-underwater.png) | ![晴天泳池水上焦散](../img/path-tracing/pool-sunlit.png) |

短涟漪与 0.266° 太阳角半径在浅蓝瓷砖上形成清晰亮纹；400 万发射路径、64 spp、半径 0.025，水体使用冻结的解析涟漪网格。[近景原图](../img/path-tracing/pool-sunlit-underwater-raw.png) · [平水面对照](../img/path-tracing/pool-sunlit-flat.png) · [复现与验收](path-tracing-photon-mapping.md#晴天泳池更明显的网状亮纹)。

| 池底：Metal photon mapping，100 万条光路＋32 spp，未降噪 | 水体折射焦散贡献，未降噪 |
| --- | --- |
| ![池底光子映射焦散](../img/path-tracing/pool-photon.png) | ![水体折射焦散 AOV](../img/path-tracing/pool-photon-caustics.png) |

池底使用固定解析波形；固定半径 photon map 为有偏估计，CPU／Metal／Vulkan 共用相同光子数据。[复现、验收与通用 PT 优化](path-tracing-photon-mapping.md)。

新增 CPU [SPPM 渐进光子映射](path-tracing-sppm.md)：每轮重新采样相机与光子路径，逐像素缩小合并半径，改善太阳池底焦散收敛。光子按批次释放；有限预算下仍有密度偏差。

| 地形与草丛：Metal PT＋OIDN，128 spp | Mountain Lake：Metal PT＋OIDN，128 spp |
| --- | --- |
| ![路径追踪地形与草丛](../img/path-tracing/pt-terrain.png) | ![路径追踪山湖与倒影](../img/path-tracing/pt-mountain-lake.png) |

| 湖岸沙滩：Metal PT＋OIDN，1024 spp | 浅水折射：Metal PT＋OIDN，4096 spp |
| --- | --- |
| ![路径追踪湖岸沙滩与湿沙](../img/path-tracing/pt-mountain-lake-beach.png) | ![路径追踪 FFT 浅水](../img/path-tracing/pt-ocean-clear.png) |

沙滩保留世界坐标纹理细节；水下太阳路径使用 BSDF／相位混合采样，保持原太阳与曝光。[沙滩原图](../img/path-tracing/pt-mountain-lake-beach-raw.png) · [浅水原图](../img/path-tracing/pt-ocean-clear-raw.png) · [修复与验证](path-tracing-procedural.md#水下太阳路径采样)。

### 海况与实时／PT 外观对齐

最近几轮开发构建覆盖可标定的 JONSWAP/TMA 风浪、独立涌浪、共享水面光学参数，以及 glint 和反射修正。实时与 PT 共用 FFT 波形和细波配置，相机加密 PT 网格将水面覆盖扩至 2 km。

| 更新 | 结果与验收 |
| --- | --- |
| [海况与共享水面参数](ocean-sea-state-and-pt.md) | 可标定浪高／周期、独立涌浪、IOR 1.333、精确 Fresnel 与有限太阳反射。[共享水面对照](ocean-physical-surface.md)。 |
| [Glint 空间滤波与 TAA](ocean-glint-antialiasing.md) | 子像素太阳积分、斜率矩 mip、线性 HDR 水面历史；近景单帧对光栅 SSAA 的误差下降，远景和 TAA 仍有偏差。 |
| [原始浮点 FFT 法线与重建](ocean-float-normals-and-glints.md) | CPU／GPU PT 保留独立主波／细波周期；tent glint 重建和冻结水面直接累积。Tent 使边缘更柔和，但增加成本及相对原始 PT 的像素 L1。 |
| [远景太阳与天空反射积分](ocean-reflection-integration.md) | 准确反射 Jacobian 的斜率空间积分、像素内 Fresnel 与天空辐射乘积积分；水体／反射／太阳／泡沫／边界可加分量。Metal／Vulkan 水体回归通过。 |

![风浪与独立涌浪：Metal 原生 RT＋OIDN，512×320，256 spp](../img/path-tracing/ocean-sea-state.png)

| 当前实时 TAA | 同快照 Metal 原生 RT＋OIDN，512 spp |
| --- | --- |
| ![当前实时海面](../img/path-tracing/ocean-reflection-after-raster-taa.png) | ![同快照路径追踪海面](../img/path-tracing/ocean-reflection-pt-denoised.png) |

共用快照、相机、海况、时刻、640×360 分辨率和导出器；数值比较使用 OIDN 前的原始 PT。最新新旧模型消融使用同一二进制与编译 shader，两组原始 PT 图逐位一致。这些是开发构建结果，捕获命令、源码／构建哈希和验收日志保存在阶段记录中。

| 最新 TAA 对原始 512 spp PT 的比较 | 前一反射模型 | 当前反射模型 |
| --- | ---: | ---: |
| 全水域相对 RGB L1 | 129.66% | 113.49% |
| 远景相对 RGB L1 | 171.88% | 134.01% |
| 两侧海水相对 RGB L1 | 54.73% | 43.56% |

近景 glint 基本未变；海面自身反射、远景非高斯斜率、粗糙天空反射与水体输运仍有差异。一组同步 20 帧捕获中，单帧约增加 3 ms，不能当作稳定 FPS 基准。[完整图像、分量统计、成本与限制](ocean-reflection-integration.md) · [最新验收记录](../img/path-tracing/ocean-reflection-validation.json) · [原始 PT](../img/path-tracing/ocean-reflection-pt.png)。

<details>
<summary>此前的 glint 重建及最新天空反射分量</summary>

| 此前的 box 重建 | 此前的 tent 重建 |
| --- | --- |
| ![Box glint 历史捕获](../img/path-tracing/ocean-alignment-box-raster-taa.png) | ![Tent glint 历史捕获](../img/path-tracing/ocean-alignment-tent-raster-taa.png) |

这两组浮点法线历史捕获使用相同 PT 输入。Tent 将 glint 区域的相邻亮度梯度降低约 12.5%，但增加原始 PT 像素 L1，成本约增加 5–8 ms。梯度下降不能独立证明消除了 aliasing。[重建消融](ocean-float-normals-and-glints.md)。

| 中心法线天空反射 | 像素积分天空反射 |
| --- | --- |
| ![旧天空反射贡献](../img/path-tracing/ocean-reflection-before-reflection.png) | ![新天空反射贡献](../img/path-tracing/ocean-reflection-after-reflection.png) |

天空反射图是带实际混合权重的可加辐射分量。此前的 8 位法线 PT 与光栅 SSAA 参考保留在各阶段记录中，不跨参考变化比较误差。[初始对照与波谱缓存基准](ocean-realtime-pt-comparison.md)。

</details>

![路径追踪 FFT 大浪海洋：Metal PT＋OIDN，256 spp](../img/path-tracing/pt-ocean.png)

### Blender 测试场景

Blender 官方 Classroom 与 Barcelona Pavilion 经离线导入后，由本项目 Metal PT 渲染并使用 OIDN 降噪。[资源、材质转换与验证](blender-path-tracing.md)。

| Classroom | Barcelona Pavilion |
| --- | --- |
| ![Classroom Metal PT＋OIDN](../img/path-tracing/blender-classroom-materials.png) | ![Barcelona Pavilion 池水 Metal PT＋OIDN](../img/path-tracing/blender-barcelona-water.png) |

Barcelona 已接入顶层 Bump／Normal Map 图集、介电材质的粗糙度纹理和封闭池水的反射／折射／吸收；修复隐藏粒子发射器遮住水面及 UV 方向。池水为明确的艺术配置（IOR 1.333、深度 0.5 世界单位），可切换源参数。[原始预览](../img/path-tracing/blender-barcelona-water-raw.png) · [复现与限制](blender-path-tracing.md#barcelona-材质与池水) · [验收记录](../img/path-tracing/blender-water-validation.json)。

CPU／Metal／Vulkan 使用 **共享几何 BLAS/TLAS**。新版 Barcelona 保留全部 **20,622 个植被粒子**，约 **5,497 万个实例三角形**。revision 5 接入 Blender corner MikkTSpace 切线，并分别烘焙薄叶的漫反射与透射；CPU 几何／加速数据约 **54.4 MiB**。以下为 640×360、256 spp 的 Metal PT＋OIDN 预览。[原始图](../img/path-tracing/blender-barcelona-thin-raw.png) · [材质验收与复现](blender-path-tracing.md#薄玻璃太阳反射链)。

![Barcelona 完整植被与薄叶透射：Metal PT＋OIDN](../img/path-tracing/blender-barcelona-thin.png)

Blender Cycles 原场景 GT：1024 spp、原材质与完整植被，无降噪、强度截断或 glossy 模糊。旧引擎预览使用缩减粒子预算；上方完整植被图的池水与材质配置仍与源场景不同；[对照与线性参考](blender-path-tracing.md#blender-cycles-gt)。

![Barcelona 原场景 GT：Blender Cycles 1024 spp](../img/path-tracing/blender-barcelona-cycles-gt.png)

新增独立的 **同参数 Cycles 对照**：双方使用相同冻结几何、512 个粒子、相机、贴图、灯光、GGX 闭包、薄玻璃和封闭池水。320×180、4096 spp、深度 24，无降噪的 Metal／Cycles 总 RGB 能量差 **0.067%**，逐像素仍有反射采样噪声。这项受控基线尚不覆盖原始 Blender shader 图与法线图。[测量、独立种子噪声与复现](blender-path-tracing.md#同参数-cycles-线性对照)。

分离闭包法线并增加 bump shadowing 后，小场景 Metal／Cycles 原始 RGB 相对 L1：Lambert／薄叶从 **0.838% 降至 0.290%**，GGX／薄叶从 **2.662% 降至 0.582%**（192×96、4096 spp）。太阳提议将完整 Barcelona 的 160×90、512 spp、seed 1 对照从 **4.67% → 3.53% → 2.98%**（水体、再到薄玻璃反射）；薄玻璃异常像素从 **29.06 降至 0.0656**（Cycles **0.0462**），RGB RMSE **0.156 → 0.049**。seed 2 的 L1 从 **3.07% 略升至 3.19%**，池水／薄叶稀有路径与原分层材质仍需改进。统计缓存的独立单线程 trace 微基准提升约 **1.58 倍**。[最新验收与限制](../img/path-tracing/blender-thin-validation.json)。

![完整 Barcelona 法线图对照：Metal 原图、Cycles 原图、线性绝对误差](../img/path-tracing/blender-barcelona-thin-matched.png)
