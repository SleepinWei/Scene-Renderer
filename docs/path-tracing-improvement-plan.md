# Path Tracing 后续改进计划

以 Blender Classroom／Barcelona、Sponza／San Miguel、玉龙／FFT 海洋构成测试矩阵，延续 [外观与加速四阶段计划](path-tracing-appearance-plan.md)。粗糙介电阶段已完成；本次新增 Blender 场景包、颜色图集、CPU／Metal／Vulkan 入口和导入回归。下方按完成状态推进，不把计划写成已支持功能。

## 已完成：第一轮材质改进（2026-10-05）

- [x] CPU／Metal／Vulkan 平滑 thin-sheet dielectric：两界面内部反射、直行透射、RGB 阴影透射，不进入介质栈；保留直穿路径的 NEE/MIS 对照。
- [x] 常见顶层 Transparent／Mix／Add／Principled Alpha 转换为覆盖率图；不透明图集强制 alpha=1，避免 UV 空白制造几何空洞。
- [x] PBR roughness／metallic 使用独立线性 ORM 图集，颜色保持既有 pow(2.2) 编码；增加实际 Blender 烘焙通道回归。
- [x] 两场景重新导出、原始 CPU／Metal／Vulkan 对照、前后同参数预览与导入／能量／白炉测试。

薄玻璃目前只支持平滑、平行双界面；源 Layer Weight 建筑玻璃以 IOR=1.5 近似，未复现完整旧 closure。粗糙薄片、node group 展平、完整层间法线／位移及 mip／footprint 过滤仍待完成。详见 [材质改进与验收](blender-path-tracing.md#薄玻璃与多通道材质改进)。

## 已完成：Barcelona 材质与池水（2026-10-05）

- [x] 顶层 Bump／Normal Map 切线图集与原 corner normals；PNG／UV 方向与出射 normal 修复。
- [x] 粗糙 dielectric 读取线性 ORM；常量吸收／散射系数与封闭介质边界导入。
- [x] 隐藏粒子发射器修复；明确 pool／source 配置，池水反射／折射／吸收。
- [x] 原相机预览、CPU／Metal／Vulkan 原始图对照及实际 Blender 导出回归。[验收](blender-path-tracing.md#barcelona-材质与池水)。

新增原 `.blend` Cycles 1024 spp 参考：关闭 clamp／glossy 过滤、保存线性 EXR；另保留源设置对照。后续优先匹配石材分层反射、叶片透射、完整实例以及水下照明／折射焦散，建立同材质线性比较。

完整旧 Layer Weight closure、多材质共享体积、非均匀水体、rough thin-sheet、池水焦散专项采样仍待实现。

## 已完成：同参数 Cycles 输运基线（2026-10-05）

- [x] 冻结同一包的相机、几何、512 个粒子、纹理、HDR、有限太阳与池水参数；独立 Cycles 重建，不复用原始复杂 shader 图。
- [x] CPU／Metal／Vulkan 显式 Lambert、单次散射相关 Smith GGX 对照模式；平滑薄片与无色固体 Glass 对应，默认 PBR 保留。
- [x] 原始线性 PFM 的 RGB L1／RMSE／能量／固定区域及独立 seed 噪声比较，禁用降噪、截断和自适应。

相同条件的输运基线与原始 Blender 外观参考各自保存。[复现与测量](blender-path-tracing.md#同参数-cycles-线性对照)。后续先启用法线图并解决切线基准，再实现原材质分层、Layer Weight 与叶片透射；焦散收敛是独立的采样改进，不能用总能量一致代替局部图像验收。

## 已完成：corner 切线与薄叶漫透射（2026-10-05）

- [x] revision 5 导出 Blender corner MikkTSpace tangent／handedness；CPU／Metal／Vulkan 插值后处理非均匀缩放、镜像实例、UV 反射及背面，旧包保留 UV frame fallback。
- [x] 独立薄叶 Lambert R／T 闭包、两半球 PDF、背面 NEE；反射／透射分别烘焙，不进入玻璃／水体介质栈。
- [x] 八组切线解析验证、32,768 次白炉／概率检查、实际 Blender 烘焙及完整 Barcelona 原始 CPU／Metal／Vulkan 与开启 normal 的 Cycles 对照。

原 Glossy／Layer Weight 分层、组内节点仍未恢复。薄叶 BDPT 策略密度尚未验收，目前显式拒绝该组合；GPU radiance cache 不处理薄叶尾项。详情见 [切线与透射验收](blender-path-tracing.md#切线基准与薄叶透射)。

## 已完成：掠射法线与闭包遮蔽（2026-10-06）

- [x] CPU／Metal／Vulkan 分离 Diffuse 与 Glossy／Translucent 法线，增加 bump shadowing 与有效镜面反射保护。
- [x] 统一几何余弦测度下的 `f*cos`，保留各 lobe proposal PDF；独立球面积分与 Monte Carlo 检查一致性及能量。
- [x] 原生 GPU fixture 增加三个闭包的强 normal-map 回归；BDPT 对未验收的伴随闭包显式拒绝，GPU cache 禁用该尾项。

图像、独立 Cycles 对照与剩余薄叶差异见 [掠射法线验收](blender-path-tracing.md#掠射法线与闭包遮蔽)。后续优先恢复 Layer Weight／Glossy 分层、解决透射修正的参考定义与纹理过滤；再推进原生 RT 与焦散策略。

## 已完成：太阳反射采样与实例统计缓存（2026-10-06）

- [x] 按像素／sample 重放 Barcelona 高亮，区分池水反射太阳链与薄玻璃镜面链。
- [x] CPU／Metal／Vulkan 在水面上方增加反射 continuation cone，保留 50% 原始 BSDF proposal 与完整混合 PDF；导入池水与 FFT 海洋均覆盖。
- [x] 缓存 scene 的 scattering／dielectric／kind count，避免每条相机路径扫描全部实例。
- [x] 有限太阳圆盘独立积分、采样方差和通用 dielectric 池水测试，完整场景开关对照与后端回归。

结果与适用范围见 [太阳反射验收](blender-path-tracing.md#太阳反射链与实例统计缓存)。多次漫反射后经薄玻璃的镜面链仍未定向采样；下一步考虑有限太阳的镜面链 proposal／SMS，并独立验证策略密度与 MIS，不能直接开放尚未验收的体积／薄叶 BDPT。

## 第一优先级：材质对应关系与可验证外观

| 工作 | 本次暴露的问题 | 完成标准 |
| --- | --- | --- |
| 薄表面 dielectric 与 alpha | 平滑薄玻璃、顶层 alpha 与薄叶 Lambert 透射已接入；粗糙薄片及原分层仍缺失 | 独立 thin-sheet BSDF，不进入体积栈；反射／透射与 alpha 图采样一致；法线入射、掠射、TIR 适用范围和能量数值回归 |
| 材质中间表示与转换诊断 | 旧 diffuse/glossy、Fresnel／Layer Weight 混合被简化，组内颜色存在 fallback | 显式支持材质／不支持节点列表；先补 Principled 与常见旧 closure，再展平 group；固定版本导出记录每项近似 |
| 多通道烘焙与纹理过滤 | 底色／alpha／roughness／metallic 已烘焙；顶层 bump／normal 已接入；group、层间 normal 和过滤待补 | 底色、roughness、metallic、alpha、切线 normal 分离；正负缩放及 UV seam 测试；线性／sRGB 约定统一；mip 与射线 footprint 过滤 |
| 灯光和色彩基线 | 旧 light node、blackbody、spread 与显示管线不同 | 保留原功率单位、变换面积、有限太阳立体角；面积灯／点灯独立照度积分；原始线性 HDR 对照，显示变换单独评估 |
| 更准确的场景捕获 | viewport depsgraph 与 render-only 设置、dust／volumeLight 合成不同 | 显式 render modifier／粒子设置与 collection 可见性；完整集合实例测试；未支持的 hair／volume／compositor 明确诊断 |

先用小型合成场景验证闭包、相机、UV 和灯光，再渲染 Classroom／Barcelona。Cycles 参考需使用匹配的材质、灯光、相机和线性色彩条件；保留源 scene 与转换 scene 两组图，避免把外观映射差异当作 PT 错误。

## 第二优先级：实例、内存和性能测量

Barcelona 的完整评估图中，独立几何约 28 万三角形，粒子展开后约 5,497 万三角形。2026-10-05 已实现 CPU／Metal／Vulkan 的软件共享 BLAS/TLAS，并渲染全部植被；几何与加速结构按独立 mesh／实例分别计量。实际图片、前后内存和线性对照见 [完整实例验收](blender-path-tracing.md#共享-blastlas-与完整植被)。

1. **阶段测量已部分完成。** 报告记录包解析／纹理解码／独立几何解码、BLAS／TLAS、emitter、相机介质、场景导出、GPU 准备、dispatch／readback、CPU film 更新、checkpoint／最终文件写出与 OIDN，以及唯一／实例三角形、内存负载与读回次数。当前为 CPU 墙钟；求交／材质／介质的硬件 GPU timestamp、upload 独立耗时与进程峰值仍待补齐，不能从现有字段推断 GPU kernel 内部瓶颈。
2. **软件 BLAS／TLAS 已完成。** mesh 的几何和 BLAS 只存一次；TLAS 保存实例变换／世界 bounds，局部射线不归一化以保留 world t。独立展开几何／世界空间 brute force 检查镜像、非均匀缩放、UV alpha、法线和材质；介质 ID 与世界空间 emitter 面积按实例处理。固定 spp checkpoint 默认间隔 256（首轮 4／16 spp），自适应检查保持 32；间隔改变不影响固定样本结果。
3. **接入原生 Metal RT。** RHI 提供能力查询、BLAS／TLAS 生命周期、静态缓存和求交接口；能力不足时回退软件。对照 same ray 的 hit/miss、t、法线和 barycentric，再对照原始 PFM。参考 [Metal acceleration structures](https://developer.apple.com/documentation/metal/ray-tracing-with-acceleration-structures)。
4. **Vulkan 按实际设备能力接入。** 查询 acceleration structure／ray query 与限制；本机 MoltenVK 不假定具备原生 RT。保留软件路径，并在支持 RT 的设备上验收。[Vulkan 官方能力说明](https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html)。
5. **缓存与数据布局。** 场景包使用索引顶点、材质去重、压缩／mip 图集；几何与 BVH 以源内容／导出版本作缓存键，避免每次重建。增加资源预算和阶段内存峰值，缓存必须处理参数及 topology 失效。
6. **实测后决定 Wavefront。** 将求交、材质、阴影、介质分队列可能降低分歧，也会产生 compaction／队列开销。只在相同场景、后端和误差预算下比较；保持固定样本的 megakernel 参考。[PBRT GPU 映射](https://www.pbr-book.org/4ed/Wavefront_Rendering_on_GPUs/Mapping_Path_Tracing_to_the_GPU)。

验收要求：完整实例场景能加载和渲染，不依赖粒子预览预算；几何内存主要随独立 mesh 增长；镜像／非均匀变换和重复 emitter 无能量偏移；记录端到端耗时及线性图误差，禁止只给 rays/s。

## 第三优先级：appearance modeling

- **微表面多次散射与分层材质：** 修复高 roughness GGX 的单次散射能量损失；增加 coat／rough transmission／anisotropy，验证白炉和互易关系，再用于建筑材质及半抛光玉石。
- **非均匀玉石：** 物体空间 σa／σs 三维场、色根／云絮／杂质、局部 majorant 和 delta tracking；常量场必须退化到现有均匀随机游走。尺度与系数作为同一预设保存。
- **真实薄叶、织物与毛发：** 先薄表面透射和 rough diffuse，再评估 sheen／织物方向性与曲线 primitive；没有实际场景需求前不增加全套 hair solver。
- **相机与光谱：** 加入 thin-lens 景深、快门时间与 motion blur；光谱 absorption／IOR、色散在 RGB 基线验证后独立推进，不用 RGB 染色冒充色散。

每项都配解析／独立积分测试和开关对照；玉石预设仍为艺术参数，实测矿物标定需要独立数据来源。

## 第四优先级：收敛、体积和焦散

1. 在真实有限太阳／HDR、面积光与体积介质上补齐 BDPT 顶点及策略密度、自由程、介质边界、roulette 和连接 MIS。旧表面 BDPT 继续回归；粗糙／体积 BDPT 的拒绝检查只在实现完整策略后解除。
2. 基于这个参考实现 VCM 光子连接／合并；有偏预览明确光子半径和预算，逐级减半半径并增加样本检查偏差。水面／海床、玉龙体积与室内玻璃分别验收。[VCM](https://www.iliyan.com/publications/ImplementingVCM/)。
3. 对难采样的折射镜面链评估 SMS。普通 BDPT 不保证能连接所有 delta 链，需专项失败诊断与 PDF／MIS 验证。[SMS](https://rgl.epfl.ch/publications/Zeltner2020Specular)。
4. 在不含难处理镜面／体积的场景先做 guiding 和 light tree；之后再评估 ReSTIR GI／PT 的空间／时间复用，透明、介质及重投影失效分别处理。保留无复用的固定 spp 参考。

大太阳、radiance cache、自适应停止、光子合并和降噪均不能替代真实光源条件下的线性参考。比较同耗时误差或达到固定误差的耗时，分别统计亮区、暗区、玻璃、焦散和体积区域。

## 验收矩阵与执行顺序

| 场景 | 主要验收点 |
| --- | --- |
| 合成相机／三角形／白炉／照度 fixture | 捕获、尺度、PDF、互易、灯光单位与稳定性 |
| Classroom | 室内多反弹、集合实例、多 emitter、暗部与材质图集 |
| Barcelona 完整粒子包 | BLAS/TLAS、非均匀变换、薄玻璃、水面反射、内存峰值 |
| Sponza／San Miguel | 大模型、alpha 纹理、导入与纹理内存、收敛基线 |
| 玉龙／FFT 海洋 | 粗糙 dielectric、嵌套介质、真实太阳、体积与焦散 |

下一轮优先补掠射角 normal 防护／阴影边界对齐、node group／Layer Weight 分层与纹理过滤，并调查受控 normal／GGX 对照中的高亮异常样本；系统侧在现有软件 BLAS/TLAS 上补硬件 GPU 时间戳与原生 Metal RT，再按实际 Vulkan 能力适配。体积 BDPT 与非均匀玉石继续按原计划分阶段执行。

## 薄玻璃镜面链（2026-10-06）

已接入外部介质非 delta 顶点的薄片反射太阳 cone：场景构建时缓存最多八个薄玻璃方向，薄玻璃 cone 使用 10% 概率，其余保留原 BSDF／水体混合，重叠 cone 累加完整密度；真实镜面／直通事件继续使用原 Fresnel 和 tint。解析有限太阳、两块有色直通玻璃、重叠 cone、白炉与 catalog 上限已通过。CPU／Metal／Vulkan 共享 catalog，独立冻结包渲染工具支持原生 GPU 验证。数值验收及复杂链限制见 [薄玻璃太阳反射链](blender-path-tracing.md#薄玻璃太阳反射链)。

后续处理池水与薄叶中的剩余稀有路径；对多次镜面反射／折射需要独立求解与完整策略密度验证，继续保留原分层材质、SMS 和 BDPT 的后续计划。

## 2026-10-06 优先项：photon mapping 与通用加速

按当前任务暂缓材质扩展，优先 photon mapping 与通用 PT 优化，不引入 ReSTIR。已接入可复现的池底／平水面／无水体样例、CPU 并行光子发射、CPU／Metal／Vulkan 共享固定半径间接光估计、焦散 AOV、混合透明 any-hit 和独立 GPU spp 批次。支持边界、偏差与验收见 [专项文档](path-tracing-photon-mapping.md)。该阶段不代表 Barcelona 受控 closure／体积 photon gathering 或 BDPT 迁移已经完成。
