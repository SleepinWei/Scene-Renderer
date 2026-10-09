# 山湖 90° 太阳侧光与空中透视

2026-10-09。本次开发工作区的 Metal 实景捕获提高大气空中透视的视觉占比，降低地面云影和固定颜色近地雾，并让山体／云遮挡参与空气太阳散射。本次发布记录展示图、参数和验证；源实现位于共享开发工作区，捕获 manifest 保存源码与二进制 hash，不把基准 Git HEAD 视为已提交全部实现的版本。

![90° 山湖侧光与较弱近地雾](../img/diagnostics/environment/aerial-side/mountain-lake-side-combined.png)

![空气太阳遮蔽关闭／开启／开启并叠加较弱高度雾](../img/diagnostics/environment/aerial-side/comparison.png)

左图关闭空气太阳遮蔽，中图开启，右图再叠加较弱高度雾。相机、太阳、曝光与模拟时间不变。所有图片由本项目渲染，Metal、960×540、RSM 关闭；这个侧向角度主要呈现宽空气阴影带，朝太阳看时 Mie 前向散射更强。

## 参数与视角

| 项目 | 捕获值 |
| --- | --- |
| 地面云影强度 | .3 |
| Aerial 光学距离缩放 | 2 |
| Aerial 起始／最大距离 | 100 m／32 km |
| 干空气／雨天高度雾消光 | .000015／.00005 m⁻¹ |
| 高度雾高度 | 180 m |
| 山体 CSM 阴影范围 | 8 km |
| 云量／模拟时间／曝光 | .55／8 s／2.4 |
| 相机位置 | (-2600, 800, 2600) m |
| 视线方向 | (0.706138, 0.052336, -0.706138) |
| 朝太阳方向 | (0.651806, 0.341551, 0.677120) |
| 视线与太阳夹角／太阳高度 | **90.0°**／约 **19.97°** |

开发版 Environment 面板提供 `Ground cloud shadow strength`、`Atmospheric aerial perspective`、`Aerial perspective distance scale` 和 `Atmospheric sun shadows / shafts`；高度雾与空气太阳遮蔽可独立切换。Aerial 比例通过加强大气散射、减弱固定颜色高度雾实现。

## 方法与验证

32³ 相机视锥 LUT 以 32 步积分 Rayleigh／Mie 散射与 RGB 透射率。单次太阳散射查询五级 CSM 和原始云太阳透射场；多次散射沿用各向同性近似。阴影降低散射亮度，不改变空气 RGB 消光。天空已有大气积分，只减去局部被遮挡的单次太阳贡献；云像素跳过该修正，避免重复遮挡。

| 科学 fixture | Metal／Vulkan 结果 |
| --- | --- |
| 散射下降的 froxel | 30720 |
| 遮蔽前后 RGB 透射率 | 逐位一致 |
| 产生局部太阳遮蔽的天空像素 | 26118 |
| 30% 地面云影插值最大 RGB 误差 | .000058 |
| AP 对独立 CPU 积分相对 L1 | .000326 |
| 水体空气捕获最大 RGB 误差 | .000053 |

Metal 完整数值、API 与 shader 验证使用同一套最终 shader，在最后一轮默认系数微调前完成，科学 fixture 显式指定介质系数。最终参数的二进制完成 Metal 实景图集和 Vulkan 完整数值检查；Vulkan 使用 MoltenVK，Khronos layer 不可用。CPU 环境／渲染图／RHI 契约 **4/4** 通过。一次额外 Metal API-only 复测等待已有 PT 管线的 Metal 编译请求超过 3 分钟后中止，未计为通过。

[Metal 数值](../img/diagnostics/environment/aerial-side/metal/validation.json) · [Vulkan 数值](../img/diagnostics/environment/aerial-side/vulkan/validation.json) · [捕获参数](../img/diagnostics/environment/aerial-side/mountain-lake-side-metrics.json) · [图像与参数汇总](../img/diagnostics/environment/aerial-side/summary.json) · [源码、产物 hash 与开发版命令](../img/diagnostics/environment/aerial-side/manifest.json) · [验证说明](../img/diagnostics/environment/aerial-side/verification-note.txt)。

## 限制

CSM 覆盖外和完全位于主视锥外的山体尚缺独立全域遮挡几何。低分辨率视锥 LUT 与 32 步积分适合宽光束，窄束和运动序列仍需完善。水体天空 fallback 暂不含局部天空太阳遮蔽修正；透明物体仍使用背景深度代理，多次散射保持各向同性近似。此次为静态视觉展示，不作 60 fps 或整帧性能验收。
