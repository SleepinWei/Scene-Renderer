# 文档索引

项目介绍与精选效果见 README：[English](../README.md)／[简体中文](../README.zh-CN.md)。构建、运行和截图复现见 [构建与运行](getting-started.md)，完整参数与效果对照见 [图集说明](rendering-gallery.md)。文件放置与依赖管理见 [仓库结构说明](repository-layout.md)，测试入口见 [测试说明](../tests/README.md)，示例资源来源与使用条件见 [场景资源说明](../samples/README.md)。

## 使用与项目概览

| 文档 | 内容 |
| --- | --- |
| [构建与运行](getting-started.md) | 依赖、Metal／Vulkan 构建、资源下载、命令与操作、画廊复现及平台范围 |
| [详细功能展示](rendering-showcase.zh-CN.md)／[English](rendering-showcase.md) | 原 README 的完整图片、分块算法讲解与性能对照，按材质／自然环境／水体／几何／PT 分组 |
| [完整效果图集](rendering-gallery.md) | 全部场景说明、采样配置、开关对照、中间产物与专题入口 |
| [系统设计](system-design.md) | 逻辑／渲染线程、快照、每帧流程、RHI、shader 构建与目录模块 |
| [自然环境模拟应用计划](natural-environment-roadmap.md) | 现有实时能力与缺口、共享天气／时间／地表状态、山湖／森林／海岸阶段及性能预算；环境主干开始实施 |
| [山湖天气实验室交付规格](mountain-lake-environment-spec.md) | L1 时钟／太阳、L2 共享风／云草／波谱、6 秒云量过渡、L3 基础雨／16×16 沿坡收支／空间湿材质、L4 云影／空气雾／近似 P1 PT、检查点与编辑器操作；联合性能门槛和捕获分级路线 |
| [大气空中透视](atmospheric-aerial-perspective.md) | Unreal 同类相机视锥 AP LUT、Rayleigh／Mie 远景雾、水体空气捕获、开关与双后端验证 |
| [天气光照与 PT 参考](weather-lighting.md) | L4 云影／空气段消光／近地雾、限频太阳场、近似 P1 导出、双后端对照与性能测量范围 |
| [课程成员与历史效果](archive/historical-gallery.md) | 原课程项目署名与历史截图 |

## 渲染与效果

| 文档 | 内容 |
| --- | --- |
| [RHI 重构计划](rhi-refactor-plan.md) | 后端分层、迁移进度、验证与平台限制 |
| [实时抗锯齿](realtime-anti-aliasing.md) | 语义 mip／8× 各向异性过滤、固定网格高质量 TAA、运动视角与 GPU 成本 |
| [CSM 空间划分与过渡](csm-space-and-transitions.md) | 五级视锥／光空间包围盒、重叠带、可见度混合、texel 稳定与后续验收方案 |
| [新增经典测试场景](classic-benchmarks.md) | Dragon、Buddha、Armadillo、Sibenik 的资源、固定视角、导入修复与真实截图 |
| [可穿越三维体素云](immersive-voxel-clouds.md) | 量化体素、保守有符号距离场、太阳缓存、1080p 无云历史步进与风暴内部闪光 |
| [GPU Driven 体积云](gpu-driven-clouds.md) | GPU tile 队列与间接步进、程序化密度、风速历史、真实截图与性能 |
| [天空与太阳](sky-and-sun-review.md) | 大气散射、太阳能量与 GPU 回归 |
| [AO 单通道与深度优化](ao-depth-optimization.md) | R16F／R32F、线性位置重建、保守 mip、实测质量与性能门控 |
| [AO 射线参考与性能基准](ao-reference-benchmark.md) | 有限半径可见性、匹配 mask、误差／运动序列、分 pass 计时与浮点复现 |
| [原生后处理模块](post-processing.md) | Bloom、景深、相机运动模糊、色调／调色、FXAA、锐化、暗角、色差与颗粒，开关对照及双后端验证 |
| [环境遮蔽 AO](ambient-occlusion.md) | 原 SSAO 审查、GTAO 式地平线积分、边缘保留滤波、真实对照与优化顺序 |
| [DDGI 动态辐照度探针](ddgi.md) | 本地低频漫反射 GI、六面 GPU 采样、可见性／漏光控制、时间延迟、内存预算及双后端验收 |
| [PT 探针与聚类世界 LOD](pt-probes-and-world-lod.md) | 共享 PT 捕获、自动空间聚类、误差预算与完整／简化世界对照 |
| [RSM 演示与历史验证](rsm.md) | 默认关闭，仅保留 demo；太阳／天空间接光、算法边界与捕获方法 |
| [植被与 Wave Particles](vegetation-wave-particles.md) | 八物种程序植物、共享预算／间接 LOD、风动历史、局部波环／尾流及 Metal/Vulkan 验证 |
| [程序化自然外观调研](procedural-nature-review.md) | SIGGRAPH 沙纹／植被方法、局部 Gabor 沙纹、距离密度与覆盖补偿及后续计划 |
| [程序树木与实例阴影](procedural-trees.md) | 分枝骨架、种子变体、叶片透射、林缘／森林效果和 GPU 级联投影者剔除 |
| [FFT 海洋与透明水体](ocean-fft-and-rendering-review.md) | 频谱、高清波纹、折射与散射 |
| [远海海况与海洋 PT](ocean-sea-state-and-pt.md) | JONSWAP/TMA 风浪、独立涌浪、相机加密捕获、Metal/Vulkan 验证与后续模拟路线 |
| [实时/PT 海水对照](ocean-realtime-pt-comparison.md) | 同快照、线性 HDR 分区比较、初始波谱缓存与后续外观/体积优化 |
| [共享水面参数与太阳反射](ocean-physical-surface.md) | 精确 Fresnel、有限太阳滤波、GGX 粗糙水面、新旧实时/PT 对照与剩余误差 |
| [海面 glint 抗锯齿](ocean-glint-antialiasing.md) | 子像素太阳积分、斜率矩 mip、线性水面 TAA、超采样消融及未解决的远景偏差 |
| [海面远景太阳与天空反射积分](ocean-reflection-integration.md) | 分量诊断、准确反射 Jacobian、天空/Fresnel 积分及同构建 Metal/Vulkan 验收 |
| [海面浮点法线与 glint 重建](ocean-float-normals-and-glints.md) | 独立原始 FFT 法线、CPU/GPU 精度对齐、tent 太阳重建及冻结海面时域累积 |
| [实时水体升级验收](water-realtime-upgrade.md) | 近景密集网格、水下捕获、路径散射、对比与 GPU 耗时 |
| [近岸水体功能验收](water-coastal-features.md) | 可开关折射、局部多次散射、浅水波／泡沫／湿沙、动画与双后端验证 |
| [近岸水体第一轮性能报告](water-coastal-performance.md) | 操作提示、边界修复、CPU／GPU 分阶段计时、提交间隙、前后对比与复测 |
| [实时水体第二轮修复与性能报告](realtime-water-followup.md) | 地形共享顶点／亚像素历史复用、独立海面场景反射、单次观察光路与同配置性能对照 |
| [水下渲染关键路径分析](water-critical-path.md) | 双视角 A/B、原生提交依赖链、两段体积输运、捕获退化与优化优先级 |
| [水体运行时优化与线程调度](water-runtime-optimization.md) | 捕获退化修复、自适应散射、真实提交／等待对照、任务机制与 RDG 决策 |
| [RenderGraph pass culling](render-graph-culling.md) | 版本数据依赖、输出／副作用根、子资源剔除、生命周期重建及原生对照 |
| [物体与三角形裁剪](geometry-culling.md) | 实时视锥、GPU 稳定 index compaction、active indirect dispatch、静态缓存与绘制量对照 |
| [渲染内存保护](memory-safety.md) | 默认进程／GPU 配额、构建临时数据预留、Metal staging 与 AS 检查、系统压力暂停、图像保留和协作式保护边界 |
| [Cluster 分块与层级构建](cluster-partition-and-hierarchy.md) | 位置拓扑跨属性接缝、Morton 空间补齐、累计法线约束、Sponza／Dragon／Buddha 同构建性能与图像比较 |
| [程序化岩洞 cluster 与动态 LOD](cluster-cave.md) | 2,484 万逻辑三角形、真实分块／线框、洞内与误差预算路线、分批诊断、共享参考渲染器及受保护捕获验收状态 |
| [Coastal Cliff 高密度峡谷测试](cluster-cliff-nanite-test.md) | 943 万逻辑三角形、共享实例、原始几何基线、Hi-Z 图像验证与 UE Nanite 对照工程 |
| [Coastal Cliff 父层级 LOD 改进](cluster-parent-lod-optimization.md) | 连通父组、逐顶点误差继承、法线预算消融、36.56% LOD 减量与性能／图像验证 |
| [Cluster Hi-Z 优化与动态 LOD](cluster-hiz-optimization-and-lod.md) | 粗起始层／多级归约、晚期协作追加、Metal encoder 合并、Sponza 与百万三角形扫描模型、实际分块与动态 LOD 流程 |
| [Cluster 两阶段 Hi-Z](cluster-two-stage-hiz.md) | 复用 G-buffer、历史早期筛选与当帧晚期恢复、实际中间结果与图像证明 |
| [Cluster 性能优化](cluster-performance.md) | 跨网格批处理、当帧 cluster Hi-Z、Sponza 阶段与 encoder 间隙对照 |
| [分层 Cluster 几何](hierarchical-cluster-geometry.md) | Nanite 核心路径原型、meshlet／QEM 层级、GPU 屏幕误差选择与间接绘制 |
| [Hi-Z 物体遮挡裁剪](hiz-culling.md) | 当帧遮挡物 depth、保守最远深度金字塔、GPU indirect 数量与三角形缓存组合 |
| [透明天空窗口与空气侧折射](water-air-refraction.md) | 宽空气捕获、太阳圆盘、浮标、折射／全反射与可见性验证 |
| [水下太阳光束](water-sun-shafts.md) | FFT 折射光通量、水深切片、体积散射、独立开关与性能对照 |
| [分级焦散与礁石接收](water-caustic-cascades.md) | 16／48／128 m 覆盖、沿阳光方向接收捕获、层级过渡、网格与能量验证 |
| [海底潜水场景](water-underwater-diving.md) | 6 m 水深、礁石沙沟、海草、悬浮颗粒、水体能见度与上下水面对照 |
| [水底细节与实时焦散](water-seabed-caustics.md) | 细砂纹、实时 FFT 光子投射、开关对照、能量校验与 GPU 耗时 |
| [实时水下渲染](water-underwater-rendering.md) | 水中距离雾、双向折射、Snell 窗口／全反射、透明水底、验证和视角对照 |
| [折射、BSSRDF 与近岸海浪审阅稿](water-refraction-bssrdf-shoreline-plan.md) | 方案依据、空间散射／翻卷波扩展、分阶段质量门槛与实施差异 |
| [地形与 Virtual Texture](terrain-virtual-texture.md) | 四叉树、分页、预算与离线工具 |
| [VT／CSM／PCSS 效果捕获](render-diagnostics-gallery.md) | 实际 GPU 缓冲、页表与回退可视化、阴影图集、PCF／PCSS 对照及复现 |
| [VT／CSM／PCSS 修复记录](vt-csm-pcss-fixes.md) | 缺页边界混合、级联稳定、线性深度半影与 GPU 验证 |
| [草地植被与 FFT 湖面](vegetation-and-lake-water.md) | GPU 放置过滤、共享角点、生图水域 mask、FFT 周期与显示范围分离及验收 |
| [Mountain Lake 山湖场景](mountain-lake.md) | 官方资源、网格转高度场、材质 VT 与湖面 |
| [TSAA](tsaa.md) | 运动向量、重投影、历史裁剪与截图复现 |
| [Dragon、地形与 FFT 水面的离线 PT](path-tracing-procedural.md) | 官方透明龙／BDPT 焦散、固定时间捕获、岸线／草丛／沙滩与水下吸收 |
| [水体 BSSRDF／Jade Dragon](path-tracing-subsurface.md) | 均匀介质、HG、随机游走次表面和双后端验证 |
| [粗糙介电与半抛光玉石](path-tracing-rough-dielectric.md) | GGX 反射／折射、出射侧消光、原始积分与双后端验证 |
| [Blender 测试场景](blender-path-tracing.md) | Classroom／Barcelona 来源、共享 BLAS/TLAS、完整植被、材质转换与三后端对照 |
| [PT 功能与实时整合计划](path-tracing-improvement-plan.md) | 渐进 GPU 视口、共享资源与增量更新、Runtime／FFT 连续测量、预览合批／准备调度／主视图裁剪与 720p 导航、Metal／Vulkan 原生 RT、SPPM 与 Hybrid 通道的阶段验收 |
| [PT 外观与加速计划](path-tracing-appearance-plan.md) | 阶段验收、原生 RT、体积 BDPT／VCM／SMS 与非均匀材质 |
| [CPU 路径追踪](path-tracing-cpu.md) | 实时场景转换、材质、采样与输出验证 |
| [PT 材质外观扩展](path-tracing-materials.md) | PT／实时共用 GGX、切线旋转、coat／sheen／微表面能量路线与 CPU／双后端验收 |
| [毛发与布料 PT](path-tracing-fibers.md) | 圆形纤维 R／TT／TRT、粗糙漫反射／有界 sheen／薄片透射、静态样例与三后端对照 |
| [毛衣／Bunny／头发模型](path-tracing-grooms.md) | 针织 UV 外观与真实纱线、面积毛根采样、短绒毛和中长发 groom、模型生成与验收 |
| [日光／夕阳绒毛与针织精修](path-tracing-grooms-sunlight.md) | 72,000 根 Bunny 绒毛、细密针目与三股纱线、实时晴空／夕阳照明、768×768 高采样图与后端检查 |
| [纱线级针织数据与毛衣材质](path-tracing-knitted-yarn.md) | YarnSim 周期控制点连接、蜂窝针织几何、方向纱线反光、夕阳毛衣与三股纱线近景 |
| [独立高密度头发 demo](path-tracing-hair-demo.md) | 40,000 根发丝、分层／发束导向、三个参考视角与独立生成／渲染入口 |
| [Yuksel 公开头发基准](path-tracing-hair-benchmarks.md) | HAIR 文件导入、完整 50,000 根直发／波浪发、官方 PBRT 方程与原生曲线参考、当前整图差异 |
| [PT 与实时共享材质计划](shared-material-roadmap.md) | 共享输入、coat／薄叶／sheen、辅助视图、自然材质及数值／成本门槛；后续阶段未实施 |
| [OIDN 路径追踪降噪](path-tracing-denoising.md) | 现成 HDR denoiser、辅助 AOV、设备选择与离线处理 |
| [收敛优化与 BDPT 焦散](path-tracing-convergence.md) | GPU Guiding、Radiance Cache、平滑玻璃、CPU BDPT 与误差对照 |
| [Photon mapping 与池底焦散](path-tracing-photon-mapping.md) | CPU／Metal／Vulkan 共享光子、解析水体对照、混合透明 any-hit 与 GPU batch |
| [SPPM 渐进焦散](path-tracing-sppm.md) | CPU 独立光子批次、逐像素收缩半径、解析水体能量验证与收敛对照 |
| [GPU 路径追踪与渐进视口](path-tracing-gpu.md) | GPU 会话、暂停／续采／取消、直接显示、稀疏上传与完整场景测量、Metal／Vulkan 与离线对照 |

## Engine 与资源管理

| 文档 | 内容 |
| --- | --- |
| [后续计划实施与验收](engine-runtime-completion.md) | 联合加载、增量图片、固定时钟、纹理子资源、图复用、地形反馈与降级 |
| [设计审查](engine-design-review.md) | 架构评价、实施状态与后续工作 |
| [多线程与渲染分离](engine-multithreading.md) | 线程归属、快照、任务与帧队列 |
| [组件与世界命令](engine-world-commands.md) | 组件注册、后台命令与资源配额 |
| [数据边界与资产移交](engine-data-boundaries.md) | 可变数据封装、版本失效与发布 |
| [GPU 帧发布](engine-gpu-publication.md) | 内存压力、事务回滚与成功画面保留 |
| [分段上传与管线缓存](engine-streaming-and-pipeline-cache.md) | 上传预算、共享 native 管线与 LRU |
| [后续修复记录](engine-followup-fixes.md) | 场景访问、图片共享与生命周期修复 |

## 历史资料

- [旧 Metal GL 兼容层迁移](metal.md)：默认构建已转入新 RHI；旧桥需显式启用。
- [原始架构笔记](archive/original-architecture.md)：早期课程项目结构。
- [原始开发计划](archive/original-progress.md)：早期进度与问题清单。

各技术文档中的验收结果对应其记录的代码与日期；当前工作副本有进一步修改时，应重新运行相关测试。
