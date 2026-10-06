# Path Tracing 外观与加速迭代计划

沿用现有 CPU、Metal、Vulkan PT 和冻结场景。保留软件 BVH、固定采样与原始线性 PFM 作为对照，不用降噪图证明输运正确。按以下顺序推进；每阶段以代码、数值测试、实际渲染和说明文档验收。

## 1. 粗糙介电边界与玉石外观

- [x] 增加 PT 独立的介电 roughness；零值保持平滑玻璃行为，保留旧 BDPT 参考。
- [x] CPU、Metal、Vulkan 实现 GGX VNDF 反射／折射、精确 Fresnel、TIR、透射方向 Jacobian 和 radiance／importance 比例。
- [x] 粗糙边界参与 NEE／MIS；透射阴影段使用出射侧介质的消光，随机游走介质切换一致。
- [x] 增加 PDF 积分、独立方向积分、互易关系、合法参数和双后端一致性回归。
- [x] 玉石提供平滑／半抛光对照，记录 roughness、吸收、散射和模型尺度；预设为艺术参数，不宣称实测矿物标定。

本阶段使用各向同性单次散射微表面模型。高粗糙度的微表面多次散射补偿与各向异性单独扩展，不能把能量损失用任意加亮掩盖。

第一阶段于 2026-10-05 验收：Metal 5/5、Vulkan/MoltenVK 5/5，玉龙平滑／半抛光／粗糙实际图与参数见 [粗糙介电说明](path-tracing-rough-dielectric.md)。后续三个阶段尚未实现。

同日新增 [Blender Classroom／Barcelona 测试场景](blender-path-tracing.md)，作为材质映射、实例和系统性能的基线。其完整植被约 5,497 万个实例三角形；软件 BLAS/TLAS 与阶段 CPU 墙钟测量已完成，完整植被已有渲染图。第二阶段继续补硬件 GPU 时间戳与原生求交。细化任务与验收标准见 [功能与系统优化计划](path-tracing-improvement-plan.md)，本文件各阶段状态保持独立。

Blender 材质第一轮改进已完成：平滑 thin-sheet、常见顶层 alpha、线性 PBR roughness／metallic 图集与原生遮罩 UV；具体完成状态及剩余范围见上述细化计划。原生求交、体积 BDPT 和非均匀玉石仍未实现。

revision 5 进一步接入 Blender corner MikkTSpace 法线帧与薄叶 Lambert R／T，并完成开启 normal 的小场景和完整 Barcelona Cycles 对照。非均匀缩放在物体空间合成 normal 后整体逆转置；原 Glossy／Layer Weight 分层、掠射 normal 防护差异及高亮异常样本仍需后续处理。[验收与限制](blender-path-tracing.md#切线基准与薄叶透射)。

## 2. 原生 GPU 求交与性能测量

- [x] CPU／Metal／Vulkan 软件共享 BLAS/TLAS、完整 Barcelona 植被、实例身份和世界空间 emitter 验证。
- [x] 导入、构建、导出、GPU 准备、提交／读回、film／checkpoint 的 CPU 墙钟分解，以及内存负载与读回计数。

- [ ] 统计软件 BVH 求交、材质、介质与 checkpoint 输出的时间，使用相同积分器、同场景和同样本预算。
- [ ] RHI 增加 capability 查询、BLAS／TLAS、资源生命周期与射线求交接口。
- [ ] 优先接入 Metal 原生 RT；Vulkan 根据设备的 acceleration structure／ray query 能力选择原生路径，软件路径回退。
- [ ] 对照原始 PFM、primitive ID、距离和 barycentric，验收玉龙／海洋与两个大场景。
- [ ] 根据实测决定 Wavefront 拆分，避免队列开销抵消收益。

## 3. 体积 BDPT 与折射焦散采样

- [ ] 先补齐体积顶点、介质自由程密度、边界及 roulette 的策略密度和连接 MIS；旧表面 BDPT 继续作为回归。
- [ ] 增加有限太阳／HDR 光源子路径，覆盖水面／海床与玉石体积。
- [ ] 用固定光源的高采样原始 PFM 验收，禁止靠扩大太阳验证真实太阳收敛。
- [ ] 在 BDPT 基线上评估 VCM 光子合并和 SMS 折射约束求解；两者是补充策略，不能假定普通 BDPT 能解决所有镜面链。
- [ ] 单独记录有偏预览的光子半径、光子预算、误差与耗时。

## 4. 非均匀材质与交互预览

- [ ] 玉石内部的 σa／σs 使用物体空间三维场，支持色根、云絮和杂质。
- [ ] 局部 majorant、delta tracking 与透射率估计，验证常量场退化和异质场解析／数值参考。
- [ ] 扩展分层材质、微表面过滤；光谱／色散作为后续独立阶段。
- [ ] 再评估 ReSTIR GI／PT 的空间／时间复用；透明、镜面链、介质和重投影失效需要专门处理。

参考：[粗糙介电 BSDF](https://pbr-book.org/4ed/Reflection_Models/Dielectric_BSDF)、[GPU Wavefront](https://www.pbr-book.org/4ed/Wavefront_Rendering_on_GPUs/Mapping_Path_Tracing_to_the_GPU)、[VCM](https://www.iliyan.com/publications/ImplementingVCM/)、[SMS](https://rgl.epfl.ch/publications/Zeltner2020Specular)、[非均匀介质](https://www.pbr-book.org/4ed/Volume_Scattering/Media)。


### 掠射闭包进展（2026-10-06）

已在 CPU／Metal／Vulkan 分离原 diffuse normal 与受反射保护的 glossy／translucent normal，并加入 smooth-normal bump shadowing。独立积分与采样检查采用共同的几何余弦测度；对照闭包暂时禁用 radiance cache 尾项，BDPT 伴随策略待验收。复现与有限样本结果见 [掠射法线验收](blender-path-tracing.md#掠射法线与闭包遮蔽)。薄叶透射仍有残差，原 Layer Weight／Glossy 分层和纹理过滤继续列为未完成。
