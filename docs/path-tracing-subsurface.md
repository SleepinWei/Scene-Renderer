# 水体体积散射与 Stanford Jade Dragon

CPU、Metal 和 Vulkan PT 使用同一套均匀介质模型。水体沿折射后的实际路径进行 RGB 吸收、多次散射和 Henyey–Greenstein（HG）相位采样。玉石以支持 GGX 粗糙反射／折射的 dielectric 表面进入模型，在内部随机游走，再从其他表面位置出射，因此形成隐式随机游走 BSSRDF。

## 传输与材质

`SnapshotDraw` 的 `pathTracingIor`、`pathTracingAbsorption`、`pathTracingScattering`、`pathTracingAnisotropy`、`pathTracingRoughness` 保存独立于实时着色器的光学参数；`pathTracingKind=4` 表示有封闭边界的次表面材质。系数是每世界长度单位的逆长度；本例将两单位高的龙作为两米高的物体。玉石预设是展示用参数，未标定为实测矿物光谱。

| 材质 | IOR | RGB σa | RGB σs | HG g |
| --- | --- | --- | --- | --- |
| 原生水体默认 | 1.333 | (0.12, 0.04, 0.02) | (0.025, 0.05, 0.07) | 0.65 |
| Jade Dragon | 1.54 | (9, 0.7, 3.5) | (35, 45, 38) | 0.45 |

水体直接使用当前 `OceanSurfaceSettings` 的吸收、散射及各向异性，不使用实时的屏幕空间折射或浅／深水色代替输运。FFT 大波、短波、泡沫和时间冻结继续沿用原生捕获流程。

每段选择一个 RGB 通道，按 σt=σa+σs 采样自由程；碰撞 PDF 为三通道 `Tr×σt` 的均值，边界 PDF 为 `Tr` 的均值。权重分别为 `Tr×σs/pdf` 和 `Tr/pdf`。σs=0 保留确定性的 Beer 吸收路径。HG 的 PDF 与方向采样一致，`g>0` 表示沿传播方向前向散射。

体积顶点参与环境、面积光、有限太阳、局部灯光 NEE，与相位继续采样使用 power MIS；可见性遇到折射边界时停止，随后由随机游走实际穿出表面。不会用直线穿过折射界面代替其路径。经过表面时处理 Fresnel、全内反射及 radiance 的 η² 比例。

介质栈最多保存八种介质。水作为宿主，实心物体内部优先使用物体的参数；玉龙内部的水面不产生额外空气折射。初始相机介质通过朝上射线的有向穿越总数确定；同一扫描材质的重叠区域保留穿越计数（最多 64），内部界面不会重复折射。不同非水材质必须正确嵌套，非嵌套交叉退出明确报错。

GPU 材质 ABI 为 128 字节，参数块为 288 字节，包含相机介质 ID 和穿越计数。两个后端共用 `path-trace.comp` 经 GLSL→SPIR-V→MSL 编译的内核。`PackedMaterial.absorption.w` 保存介电 roughness，布局尺寸不变。JSON 保存 `media`（σa/σs/g/IOR/`dielectric_roughness`）、`volume_scattering_events`、`scattering_meshes` 和 `subsurface_meshes`，同时保留原始采样及降噪信息。

## 网格与场景

`dragon-jade` 使用完整 Stanford Dragon PLY。独立资源仍保留原扫描；仅玉石渲染的内存副本进行 10⁻⁶ 单位的位置焊接、退化面剔除与小孔封闭。修复后验证每条边有两个方向相反的邻面；拒绝非流形边、分支边界、过大的孔或退化封孔。三角扇遵循原边界的反向边，小到共线的扫描裂隙使用微小法线位移生成可相交的封孔面。全局有向体积用于检查朝向。

本机完整资产修复得到 13 个合并顶点、137 个剔除面、3 个封孔、0 条开放边，最终有效龙体为 871,286 个三角形。原始资产 SHA256 与资源声明见 [程序化 PT 说明](path-tracing-procedural.md) 和 [Stanford 使用条件](../samples/licenses/stanford-dragon.txt)。

`dragon-jade-ocean` 将龙足置于海面下：龙底部 y=0.03，静水位 y=0.2。海洋大波为 256²、64 m 周期，细波沿用 32 m FFT；展示域 512 m、1025² 网格，默认海床深度 20 m（y=-19.8）。`--pt-ocean-floor-depth 2.2` 可恢复旧图的 y=-2 浅色海床。大气烘焙为真实 HDR，加有限太阳；白天太阳输入为 (30,27,24)。该场景共有 2,968,440 个有效三角形。

旧联合图的“雾”混合了浅色海床的透射、四倍水体散射和 OIDN 对未收敛太阳焦散的云状平滑，场景没有空气雾。关闭水体散射的 512 spp 图仍出现云斑；CPU PCG 对照也出现，说明主要伪影不是介质散射本身。旧图未降噪 HDR 的单个 RGB 分量可超过 4,000，而中位数约 0.32；OIDN color-only 后全图平均 RGB 约为原图的 63%，因此不能拿该图验收能量或散射形状。

README 新海面预览使用原生水体系数、20 m 海床、法线／反照率辅助降噪和角半径 2° 的软太阳。`--pt-sun-radius` 保持太阳辐照度、扩大光源立体角，降低稀有焦散样本的亮度和方差；它改变了光源，不能当作真实太阳焦散的同条件收敛结果。默认仍使用原大气太阳半径（约 0.27°），未钳制路径贡献。真实太阳经过折射界面的 NEE／体积 BDPT 尚未支持，仍需大量采样。

```sh
python3 tools/fetch_dragon.py

# 独立玉龙：CPU 也可执行此命令，将入口改为 --path-trace。
./build/pt/Scene-Renderer --path-trace-gpu dragon-jade \
  --pt-size 640x480 --pt-samples 512 --pt-bounces 96 --pt-fixed \
  --pt-no-sky --pt-sss-roughness 0 --pt-denoise-color-only \
  --pt-output build/path-tracing/subsurface/dragon-jade

# 玉龙、FFT 水体随机游走和海面反射／折射。
./build/pt/Scene-Renderer --path-trace-gpu dragon-jade-ocean \
  --pt-time 8 --pt-size 640x480 --pt-samples 2048 --pt-bounces 96 --pt-fixed \
  --pt-sun-radius 2 --pt-ocean-floor-depth 20 --pt-sss-roughness 0 --pt-denoise \
  --pt-output build/path-tracing/subsurface/dragon-jade-ocean-preview

# Vulkan 使用同一组参数与内核。
./build/pt-vulkan/Scene-Renderer --path-trace-gpu dragon-jade --backend Vulkan \
  --pt-size 320x240 --pt-samples 64 --pt-bounces 96 --pt-fixed --pt-no-sky \
  --pt-output build/path-tracing/subsurface/vulkan-jade
```

`--pt-sss-scale` 同时缩放玉石 σa、σs，调节平均自由程；`--pt-sss-scattering-scale` 仅缩放玉石 σs，设为 0 是保留吸收的有色玻璃对照。`--pt-water-scattering-scale` 缩放当前海洋 σs，设为 0 恢复吸收-only 对照。这些参数必须为有限非负数。两个玉龙场景默认深度 96；显式 `--pt-bounces` 可覆盖到 128，深度同时计入表面事件与体积碰撞。最大深度截断会丢弃长路径；较厚、高散射材质应提高深度并用固定 spp 验收。

`--pt-sun-radius DEG` 设置天空烘焙太阳的角半径，要求 0 < DEG < 5.73；`--pt-ocean-floor-depth METERS` 设置玉龙海面预设的海床深度，要求有限正数。太阳实际角半径与辐照度写入渲染 JSON。

新图、旧图的线性统计、CPU PCG 诊断、采样器修复前后解析积分和图片校验值另存于 [海面雾状伪影诊断](../img/path-tracing/ocean-fog-validation.json)。旧次表面验收记录仍对应修复前版本，保留用于追溯。

## 验证与限制

`pt-medium` 验证 HG 归一化和采样均值、RGB 碰撞／边界估计的解析期望、零消光通道与纯吸收退化、封孔拓扑、重叠扫描的初始穿越计数、保守介质能量、嵌套初始介质和非法参数拒绝。保守白环境随机游走的参考输出为 1；本机 30,000 条路径平均约 1.001。

`pt-native-sky` 对照 CPU 与 GPU 的实际 FFT 水体、水下散射、嵌套水／玉石及内部相机，并确认实际产生体积散射事件。另包含 +10 km 场景平移的面积光阴影段回归，验证按起点和终点坐标精度修正可见性距离，避免光源自身遮挡。完整龙模型在 320×240、128 spp 的 CPU/Vulkan 对照中，相对 L1 为 0.00413、总 RGB 能量比为 1.00011。实际渲染、测试结果、PNG SHA256 和原始报告保存于 [次表面验收记录](../img/path-tracing/subsurface-validation.json)。README 同时保留原始图与 OIDN color-only 降噪图。 本机最终完整回归为 Metal 17/17、Vulkan/MoltenVK 18/18；ASan/UBSan CPU、介质、程序化测试为 3/3，补充边界输入检查后的 CPU/介质/程序化/OIDN 为 4/4。

当前介质是 RGB 均匀模型；尚未包含非均匀玉石纹理、晶粒或光谱色散。粗糙折射边界已经实现，当前玉龙默认 roughness=0.22；本页历史预览命令显式设置 0 保留原图条件。粗糙水体默认 0，可用 `--pt-water-roughness` 覆盖；参数和新对照见 [粗糙介电说明](path-tracing-rough-dielectric.md)。海洋仍是有限捕获域内的开放界面，下方按均匀半空间处理，没有水体侧壁／底面；应将捕获域覆盖相机路径涉及的场景，域外水边界不适合作为封闭水槽验收。体积和玉石场景暂禁用 GPU guiding/cache；现有 surface BDPT 未实现体积策略密度及连接 MIS，含散射或水体的 BDPT 请求明确拒绝。已有透明玻璃龙 BDPT 焦散示例继续独立保留。

模型和公式参考：[PBRT 体积距离采样](https://pbr-book.org/3ed-2018/Light_Transport_II_Volume_Rendering/Sampling_Volume_Scattering)、[PBRT 次表面反射采样](https://www.pbr-book.org/3ed-2018/Light_Transport_II_Volume_Rendering/Sampling_Subsurface_Reflection_Functions)。
