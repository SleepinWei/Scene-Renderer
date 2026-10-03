# 天空与太阳渲染审查及新 RHI 修复（2026-10-03）

已在原生 RHI 路径修复太阳盘、太阳状态同步、球面采样和多次散射，并重新生成 README 的天空、海洋及经典场景图片。实现基于 `035bb0e` 的 RHI 重构，着色器通过同一源码构建 Metal／Vulkan，未重写旧 GL 兼容着色器。以下先保留 `5eeb4db` 的历史审查证据，再记录修复与验收。

## 审查范围与实测

审查已提交的 `5eeb4db` Metal 路径，在独立源码快照中添加离屏诊断，避免修改工作目录中进行的 RHI 重构。Apple M4，开启 `MTL_DEBUG_LAYER=1` 与 `MTL_SHADER_VALIDATION=1`，天空单独渲染到 `960×720`，相机朝向太阳，垂直 FOV 为 `20°`，曝光为 `1`，每组重置 TSAA 并累积 16 帧。数值来自色调映射前最后一帧的原始 HDR；判断太阳盘和方向同步不依赖截图曝光。

| 对照 | 设置 | 原始 HDR 最大 RGB 分量 | 与 10° 基准的最大像素差 |
| --- | --- | --- | --- |
| 基准 | `sunAngle=10°`、角半径 `0.005 rad`、`mie_g=0.8` | 4.625 | 0 |
| 改变场景方向光 | 将方向光改为 `normalize(1,-0.1,0)`，天空参数不变 | 4.625 | **0** |
| 缩小太阳 | 角半径 `0.0025 rad`，其余同基准 | 4.625 | **0** |
| 放大太阳 | 角半径 `0.01 rad`，其余同基准 | 4.625 | **0** |
| 高太阳 | `sunAngle=60°`，相机同步朝向 60° | 0.729004 | 4.36865 |
| 日落 | `sunAngle=0°`，相机朝向地平线 | 3.58984 | 4.56491 |
| 太阳低于地平线 | `sunAngle=-10°`，相机朝向地平线 | 0.00187111 | 4.62443 |

正常参数下，上述 HDR、SkyView LUT 和 Multi LUT 的非有限值数为零。`mie_g=1`、`sunAngle=0°` 则产生 **1 个 SkyView 纹素的 RGB NaN**，在最终原始 HDR 中传播至 **216 个像素的 RGB**。Metal API／Shader Validation 没有报告 API 错误；数学 NaN 由纹理回读检测发现。夜间帧因近似均匀而触发通用截图工具的空图检查，诊断跳过其 PNG 导出并继续回读数值。

本地诊断图片、RGBA32F 回读和独立诊断补丁保存在忽略目录 `build/sky-audit/`；运行日志为 `/private/tmp/sr-sky-audit.log`。补丁只作用于独立源码快照，不属于正式渲染功能。

## 发现与建议

### P1：太阳盘没有绘制

`src/shader/sky/skyview.comp:324` 只合成 Rayleigh 和 Mie 散射；`src/shader/sky/skyRender.fs:29` 只读取天空 LUT 并混合 cubemap。没有太阳盘覆盖判断，也没有沿视线透射后的太阳直射辐亮度。现在的亮斑是大气前向散射，缺少可辨识的太阳圆盘。角半径在散射积分中用于太阳被地平线遮挡的平滑过渡，不能替代太阳盘绘制；上述 10° 对照中将半径从 0.0025 调到 0.01，整幅可见 HDR 完全相同。

应在天空片元路径按视线与太阳方向的夹角绘制太阳盘，使用真实角半径和像素覆盖抗锯齿，再乘观察点到太阳的大气透射率；遮挡由地球边界及现有场景深度决定。盘内辐亮度与太阳辐照度应有明确关系，而非将散射光晕增亮冒充太阳。Bruneton 的参考实现将太阳辐亮度单独定义为 `solar_irradiance / (π · angular_radius²)`：[官方实现](https://github.com/ebruneton/precomputed_atmospheric_scattering/blob/master/atmosphere/model.cc)。

当前半径 `0.005 rad` 对应角直径约 `0.573°`，而 SkyView LUT 的经度采样间距为 `360° / 200 = 1.8°`。将太阳直接写进这张低分辨率 LUT 容易丢失或闪烁，优先使用解析片元太阳盘，并让间接环境光 LUT 保留散射光，避免重复计入方向光。

### P1：天空太阳与照明太阳分离

`src/shader/sky/skyview.comp:309` 将太阳固定在 X=0 的平面，方向为 `(0,sin(sunAngle),-cos(sunAngle))`；阴影、PBR、RSM 和海面高光使用场景 `DirectionLight`。两套方向没有同步，天空使用固定辐照度 20，方向光颜色和强度也独立设置。

按各预设当前方向计算，天空朝向太阳的方向与 `-normalize(directionLight.direction)` 的夹角分别为：综合 Demo **91.257°**，Sponza／San Miguel **91.239°**，Bunny **100.691°**，Helmet **113.015°**。两种海洋预设在工厂函数中手工匹配，所以初始化时方向一致；运行中修改 `sunAngle` 仍会解除一致性。独立转动方向光的天空 HDR 差为零，实测确认两者脱离。

应确定统一的太阳状态，包含方位、仰角、角半径及辐照度；天空、阴影、RSM 和海面从该状态获取数据。太阳接近或低于地平线时，直接光照还应按相同的大气透射率与地球遮挡衰减。

### P1：GUI 允许的 Mie 参数会生成 NaN

`include/GUI.h:187` 允许 `mie_g=1`，而 `src/shader/sky/skyview.comp:271` 的相函数在 `g=1, nu=1` 时变成 `0/0`。已实际复现纹理 NaN 和 HDR 污染。应限制 `abs(g)<1`，对输入余弦和分母做数值保护，并增加太阳对齐、日落及极端参数 GPU 回归。并行 RHI 代码已有参数域检查；本条针对当前已提交并运行的路径。

### P2：天空与环境光的纬度映射不同

SkyView 生成和可见天空使用 `π/2 + 0.2`，卷积着色器与延迟 IBL 使用 `π/2 + 0.0001`；后者采样原始 SkyView 时会读取错误方向。以纬度 10° 为例，卷积采样实际读到约 **11.27°**，60° 则读到约 **67.64°**。这会使太阳附近的环境高光、天空漫反射和 RSM 的天空贡献与可见天空不一致。海面直接采样的纬度编码已使用 0.2，和可见天空一致。

此外，多处经度计算采用除法后再 `atan`，在 +Z 轴以及天顶附近存在错误象限或退化输入；应统一采用 `atan(direction.x,-direction.z)`，约定极点的经度，并保证经度纹理接缝的周期采样。生成与采样的纹素中心也需统一。

### P2：多次散射计算未接入，且内核有错误

`Atmosphere::computeDrawCall()` 每帧计算 Multi LUT，但 SkyView 着色器没有采样它，所以当前天空仍是单次散射。`src/shader/sky/multi.comp:220` 的 `i/8` 和 `j/8` 是整数除法，对 0～7 全部得到 0，64 条积分方向退化成同一个天顶方向；Mie 和臭氧密度的部分调用还误用行星半径而非离地高度。直接接入当前 Multi LUT 不能得到可靠的多次散射。

应先修复方向采样与密度坐标，再接入 SkyView；按大气参数变化缓存 Transmittance／Multi LUT，按太阳与观察高度变化更新 SkyView，避免每帧重算固定数据。模型参考：[Hillaire 2020 原论文](https://sebh.github.io/publications/egsr2020.pdf)。

### P2：观察高度固定为 1 km

大气参数以 km 为单位，`skyview.comp:301` 始终使用 `bottom_radius + 1`，未读取世界相机高度。海面上方几米的相机因此得到 1 km 高度的天空，尤其影响地平线、日落透射和地球遮挡角。应明确世界米制与大气 km 的换算及海平面基准，让 SkyView 和太阳盘使用同一观察高度。

## 原修复建议（现已实现于新 RHI）

1. 统一太阳状态与观察高度，绘制解析太阳盘，并修复 `mie_g` 的数值域。
2. 统一天空／IBL／RSM 的球面映射、纹素中心和接缝。
3. 修复并接入多次散射，明确 Mie 散射、吸收和总消光系数的含义及太阳的能量标定。
4. 增加太阳大小、方向同步、低高度遮挡与极限参数回归，重新生成受影响的 README 图片。


## 新 RHI 实现

### 统一太阳与坐标

`SceneAdapter` 以第一盏启用的 `DirectionLight` 作为太阳：光线传播方向的负值是朝向太阳的方向，颜色是大气顶层的线性 RGB 辐照度。初次进入场景保留作者设置的方向并同步大气面板；此后编辑 `Sun elevation`／`Sun azimuth` 会旋转同一盏灯，外部灯光编辑也会回到面板。其余方向光仍是独立的人工光源。没有方向光时，天空使用大气的标量辐照度与面板方向；不额外创造场景直接光，海面也不再使用虚构的默认白色灯。RSM 的天空源投影跟随该面板方位。

`ForwardPbrRenderer` 从该状态生成天空 LUT、显示参数和有效场景方向光。方向光乘太阳路径的大气透射及地球遮挡，然后同一份有效光照用于 PBR、阴影／RSM 和水面光照；原组件颜色不被衰减结果覆盖，避免每帧重复衰减。关闭直接方向光照不会清除天空的太阳。太阳或大气参数改变时失效 TSAA 历史，普通相机运动保留重投影。

世界位置以米计，大气积分以 km 计：

```text
observerHeightKm = (camera.y - seaLevelMeters) × 0.001
```

在大气内部限制观察高度，最低 1 m，避免单精度行星半径边界退化；默认海平面为世界 Y=0。海拔不再固定为 1 km。图形坐标为 Y 向上，方位 0° 朝向 -Z、90° 朝向 +X。

### 解析太阳盘

`sky-display.glsl` 在最终背景片元根据视线与太阳夹角计算圆盘覆盖，使用 `fwidth` 做边缘抗锯齿。角半径默认 `0.005 rad`，不受 200×100 SkyView LUT 分辨率限制。盘内辐亮度为：

```text
L_sun = E_top × T_observer_to_sun / (π × sin²(angularRadius))
mu_horizon = -sqrt(1 - (bottomRadius / observerRadius)²)
```

对均匀圆盘，`π sin²(radius)` 是投影立体角；小角度时退化为 Bruneton 参考中的 `π radius²`。地球遮挡逐像素裁掉盘的下缘；场景不透明几何通过 G-buffer 背景判定与深度挡住太阳，前向场景也保留此背景路径。太阳盘不写入天空辐亮度／漫反射环境 LUT，直接 BRDF 的方向光已承担太阳能量，避免 IBL 重复计算。

当前场景 HDR 附件为 RGBA16F，显示天空分量限制到 65000，防止极小太阳半径造成半精度溢出。正常角半径未触发该限制；触发时不能保证太阳总能量守恒。没有自动曝光或额外 bloom，亮度／光晕由现有曝光、色调映射和大气散射决定。

### 透射、散射和环境卷积

四个 `src/rhi/shaders/atmosphere-*.comp` 重新组织为共享 `atmosphere-common.glsl`：透射率采用 96 段非均匀中点积分，SkyView 48 段，Multi 64 条均匀立体角方向、每条 32 段。分段使用 Beer–Lambert 解析积分权重，密度始终读取离地高度。Mie 总消光为 `0.0044 km⁻¹`，已含 `0.003996 km⁻¹` 散射，不再将散射重复相加。

Multi 使用 Hillaire 的各向同性高阶散射近似，输出每单位太阳辐照度的辐亮度，再按太阳 RGB 和 `Multiple scattering` 强度接入 SkyView。它同时估计大气反馈及 Lambert 地面反射，按 `L2 / (1 - feedback)` 汇总高阶项；不是逐阶全方向散射的精确解。`Ground albedo` 默认为 0.2。

卷积以 64 个余弦加权半球方向估计 `E/π`，PBR 直接乘底色，RSM 在功率计算中乘回 π。大气显示、PBR、透明材质、RSM 与海面共用 `sky-mapping.glsl`：`atan(x,-z)` 经度、平方纬度编码、明确的极点与纹素中心；经度周期采样，纬度限制到端点纹素中心，避免接缝与错误极区混合。

调度顺序为 `Transmittance → Multi → SkyView → Irradiance`。物理参数变化更新透射率；物理参数／地面反照率变化更新 Multi；太阳方向、能量、观察高度或多次散射强度变化更新 SkyView／卷积。固定相机与光照不再每帧重算所有 LUT。`ATMOS` 保持 96 字节，LUT 参数块为 64 字节，显示 `SkyData` 扩为 128 字节，C++ 布局与 shader 反射共同校验。

### 参数域与数值保护

GUI 将 `mie_g` 最大值限制为 0.99；RHI 拒绝 `abs(g) >= 1`、非有限值、零太阳方向、无效大气半径／尺度、负系数以及消光小于散射。相函数另外保护余弦、g 和分母，双重防止太阳对齐时的 NaN。太阳角半径 GUI 以度显示，内部存 rad。

## 自动验证与结果

Apple M4／macOS：最终 Metal CTest **8/8**，启用 API 与 Shader Validation；Vulkan／MoltenVK CTest **9/9**，包括独立 GPU 验证程序。本机没有 Khronos validation layer，MoltenVK 运行关闭 MetalTools 的已知阻塞组合。两后端均运行新增太阳测试，所有 LUT／HDR 回读为有限值，包含太阳对齐且 `g=0.99` 的日落。

`AtmosphereValidation.cpp` 使用零散射／零吸收的大气隔离圆盘，256×256、10° FOV、太阳仰角 30°、RGB 辐照度 `(1,0.8,0.6)`；还检查普通大气的多次散射、观察高度、缓存及透射率 CPU 参考。Metal 数值如下：

| 检查 | 实测／判定 |
| --- | --- |
| 太阳半径 0.005 → 0.01 rad | 有效红色光斑像素 208 → 732；最大盘内辐亮度降为约 1/4 |
| 改变半径后的离散总能量比 | 1.00617；允许误差 15%，实测约 0.62% |
| 默认半径盘内红色辐亮度 | 12728；解析值约 12732.5，误差小于 0.2% |
| 地平线中心、观察高度 2 m | 圆盘能量为无遮挡的 0.59246，符合部分地球遮挡 |
| 太阳降到 -5° | 零散射测试中太阳能量为零 |
| RGB 与方向 | 盘内 G/R≈0.8、B/R≈0.6；修改方向光后固定相机内的盘消失 |
| 场景遮挡 | 不透明前景分别在前向／延迟场景中遮住太阳 |
| CPU／GPU 透射率 | 天顶逐通道绝对差小于 0.005；CPU 使用独立 double 256 段积分 |
| Multi 开／关 | 固定输入累计额外 RGB 辐亮度 11943，确认高阶项进入 SkyView |
| 参数与缓存 | g=1 被拒绝；g=0.99 有限；太阳旋转仅更新 SkyView／卷积，固定状态不重复调度 |
| 场景控制 | 作者方向初始优先；面板旋转同一盏灯；外部灯编辑同步回面板；保留作者颜色 |
| 极小太阳／零能量 | 半径 0.0005 rad 不溢出；零太阳辐照度无残留圆盘 |

上述像素面积受角度抗锯齿与阈值影响，不作为精确面积测量；能量对比在原始 HDR 中进行。多次散射累计值是指定测试 LUT 的增量，不是整个天空的物理辐射功率。尚未对任意倾角的透射率和完整天空进行光谱／参考积分器误差标定。直接太阳的透射率以相机观察高度统一估计，并在整个圆盘使用中心光线的透射；大范围高差场景、逐表面路径、盘内日落透射梯度和地表到相机的 aerial perspective 尚未实现。

## 复现及新图片

```sh
cmake -S . -B build -DSCENERENDERER_RHI_BACKEND=Metal -DSCENERENDERER_LEGACY_METAL=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ctest --test-dir build --output-on-failure
./build/Scene-Renderer --classic sky
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer --render-gallery img/metal sky
./build/Scene-Renderer --render-gallery img/metal ocean
./build/Scene-Renderer --render-gallery img/metal ocean-clear
./build/Scene-Renderer --render-gallery img/metal core
./build/Scene-Renderer --render-gallery img/metal gi
```

`sky` 画廊输出 10° 太阳、30° 太阳特写、45° 白天、0° 日落、-5° 暮光。每张图片独立重置 TSAA，累积 16 帧，最终回读 HDR 检查有限值。基础／GI／天空为 960×720，海洋为 1920×1080，模拟时间固定 8 s。场景 RGB 辐照度是项目的线性 HDR 参数，尚未进行绝对光度标定。

| 太阳特写 | 地平线日落 |
| --- | --- |
| ![原生 RHI 解析太阳盘](../img/metal/sky-sun-closeup.png) | ![原生 RHI 日落](../img/metal/sky-sunset.png) |

最新图片及对照数值替换 README 的历史实时截图。无地面几何的 `sky` 示例在地球遮挡部分呈暗色，不额外绘制地表贴图；未包含云、夜空星体、完整场景 IBL 遮挡或体积雾。
