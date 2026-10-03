# 路径追踪收敛与 BDPT 焦散实施计划

目标：在 Metal/Vulkan compute PT 上学习并复用间接光，以 Sponza 和 San Miguel 做包含训练时间的对照；增加可验证的玻璃焦散 BDPT 参考路径。

## 交付顺序

1. GPU Path Guiding：空间/法线分区方向表，独立训练后冻结；方向分布与原 BSDF 混合，更新 continuation、NEE 和发光面/环境命中的 PDF。未训练区域保留原采样，避免丢失路径支持。
2. 可选漫反射 Radiance Cache：缓存训练阶段的深层延续贡献，设置最小记录数，只在粗糙、非金属表面复用；明确记录有偏近似和训练、命中率。
3. CPU BDPT 参考积分器：光源和相机子路径、连接策略、MIS、相机投影及 splat；加入平滑玻璃 Fresnel 反射/折射与 radiance/importance 传输模式，生成确定性的焦散测试场景。
4. 验证：方向 PDF 归一化、训练表空缺/碰撞回退、常量环境能量、两后端一致性、玻璃 Fresnel/TIR/折射、BDPT 与普通 PT 基础能量对照、焦散独立图及无玻璃对照。包含训练/缓存准备的同耗时测量，保留未降噪的 PFM 和 JSON。
5. 文档和 README：给出实际运行命令、图像、误差、性能及尚未覆盖的光源/材质类型。默认入口保留普通 PT，优化与 BDPT 显式启用。

## 验收与边界

- Guiding 本身只改变提议分布，正确的混合 PDF 保留普通积分目标；自适应停止仍是已有的有偏启发式。
- Cache 为有偏的预览路径，不能拿“更平滑”代替误差测量；空间复用可能过度平滑或漏光。
- BDPT 首版作为 CPU 数学参考；GPU PT 保持 Metal/Vulkan。焦散依靠真实光路连接，不能用加亮贴图或 photon density splat 冒充 BDPT。
- BDPT 先验收有限面积光源、针孔相机、基础 PBR 与闭合平滑玻璃。若其他光源尚未支持，应明确拒绝或报告，不能静默丢失能量。
- 本机验证优先 Metal 和 MoltenVK；OpenGL 保持当前范围。

参考：[Practical Path Guiding](https://cgl.ethz.ch/publications/papers/paperMue17a.php)、[BDPT](https://www.pbr-book.org/3ed-2018/Light_Transport_III_Bidirectional_Methods/Bidirectional_Path_Tracing)。实现进度和实测结论见 [实现与验证](path-tracing-convergence.md)。

## 执行结果

- [x] Metal/Vulkan 冻结 Path Guiding：BSDF 与可见天空方向表、混合 PDF 和 MIS。
- [x] 可选有偏 Radiance Cache，实际命中与空表回退验证。
- [x] CPU BDPT、平滑玻璃、真实焦散 AOV、无玻璃对照和 640×480 输出。
- [x] 两个大场景的包含训练开销的近似同耗时测量，保留线性 PFM/JSON。
- [x] 接入现成 Open Image Denoise、HDR/AOV、CPU/自动 GPU 设备与已有 PFM 离线降噪。
- [x] README、命令、图像、限制与 Metal/Vulkan 回归更新。

实测 Guiding/Cache 尚未稳定改善两个场景的阴影误差，继续显式启用；首次 BDPT 为有限面积光源的 CPU 参考。后续 GPU BDPT、HDR/太阳 BDPT 端点、自适应 guiding 分区属于扩展工作，本轮未宣称完成。
