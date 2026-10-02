# 太阳与天空 RSM：实现与验证

RSM（Reflective Shadow Map）将光源视角下的表面片元当作虚拟点光源（VPL），估计一次漫反射反弹。本项目沿用这一思想，增加太阳方向光与大气天空输入，修正光通量和采样归一化，并在原生 Metal 上验证。算法出处为 Dachsbacher 与 Stamminger 的 *Reflective Shadow Maps*（I3D 2005），见[作者所在研究组的出版列表](https://cg.ivd.kit.edu/english/publications.php)。

## 原实现为什么影响小

旧版本只处理一盏聚光灯，Sponza 和 San Miguel 的主要照明来自太阳与天空，因此大部分入射能量没有进入 RSM。旧画廊同视角读回的平均 RGB 增量分别约为 1.12% 和 2.16%。这些旧值含额外聚光灯，不能与新版本直接视为同光照条件下的性能或精度比较。

实现还存在以下问题：

- 生成通道按对象名 `S0` 选择光源，采样通道使用第一盏聚光灯，且投影角固定为 100°，可能不匹配实际光源和锥角。
- 光通量缺少纹素对应的世界表面面积；聚光灯能量未包含距离衰减和锥角，接收表面缺少漫反射底色与金属度响应。
- 采样权重没有明确的 PDF／纹素数量归一化，固定乘以 0.4 并截断间接光至 1，无法保持能量尺度。
- 越界采样重复边缘纹素，线性过滤混合不连续的位置／法线；合成阶段重绘网格，未直接使用可见表面的 G-buffer。

单独修正这些公式并不保证画面更亮。扩大能量来源与投影覆盖范围，才让太阳／天空照亮的表面参与反弹。

## 生成与合成

`RSMPass::renderGbuffer()` 选择第一盏启用的方向光作为太阳。以相机前方为中心建立正交投影，默认半径 20 世界单位、1024 × 1024 纹素，中心在光源平面按纹素尺寸对齐。无方向光但有大气时使用默认向下投影；关闭太阳／天空模式，或两种来源都不存在时，回退到第一盏启用的聚光灯，其投影与实际外锥角一致。生成与采样共享缓存的投影矩阵。

三个 RGBA32F 附件保存世界位置、世界法线和反射功率，使用最近邻采样。位置 alpha 标记有效 VPL；背景清零。网格材质、法线贴图、植物透明裁切与双面处理仍用于生成通道。

每个有效纹素的反射功率为：

```text
A       = |dFdx(position) × dFdy(position)|
E_sun   = sunColor × max(dot(N, -sunDirection), 0)
E_sky   = π × skyDiffuseLUT(N)
Φ       = albedo_linear × (1 - metallic) × (E_sun + E_sky) × A
```

太阳颜色遵循既有方向光的辐照度约定。天空复用延迟 IBL 的大气卷积 LUT；既有光照直接用底色乘此 LUT，所以这里按 `E/π` 处理，使反弹与原有 IBL 尺度一致。它是现有大气卷积的近似，并非独立的物理标定天空。天空 IBL 直接照亮接收表面，RSM 则传播其他表面反射的天空能量，两者对应不同路径。

`RSMPass::render()` 只绘制全屏四边形，从 G-buffer 读取接收位置、法线、线性底色与金属度。对投影 UV 附近的圆盘使用固定的 R2 低差异序列，默认半径 0.3、128 次采样。令 `ω` 从 VPL 指向接收点，距离为 `d`：

```text
G           = max(dot(N_vpl, ω), 0) × max(dot(N_receiver, -ω), 0)
              / max(d², minDistance²)
E_indirect  ≈ (π R² × width × height / sampleCount) × Σ(Φ × G / π)
L_indirect  = intensity × albedo_receiver × (1 - metallic_receiver) × E_indirect / π
HDR_output  = HDR_base + L_indirect
```

圆盘 PDF 为 `1/(πR²)`；纹素数将反射功率求和转换为 UV 采样估计。越界／无效样本记零，不重新归一化。距离正则默认 0.1 世界单位，用于抑制点光源奇异性；间接 HDR 不截断至 1。没有有效 G-buffer 法线的天空与纯前向像素保留原 HDR。

## 运行与调试

```sh
./build/Scene-Renderer --classic sponza
./build/Scene-Renderer --classic san-miguel
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer --render-gallery img/metal gi
```

ImGui 的渲染选项包含 RSM 开关、`Sun and sky RSM`、太阳／天空反弹独立开关、世界覆盖半径、UV 采样半径、采样数、强度和纯间接光显示。独立关闭某一反弹来源不改变直接太阳光或天空 IBL。默认强度为 1；调整覆盖半径也会改变纹素密度及局部采样的世界范围。

画廊输出如下，每张图保持相机与曝光一致：

| 文件 | 内容 |
| --- | --- |
| `<scene>-direct.png` | RSM 关闭，仍含直接光、天空 IBL 和 SSAO |
| `<scene>.png` | 同一基础光照加太阳与天空 RSM |
| `<scene>-indirect.png` | 仅太阳＋天空一次反弹 |
| `<scene>-sun-indirect.png` | 仅太阳一次反弹 |
| `<scene>-sky-indirect.png` | 仅天空一次反弹 |

太阳／天空贡献图经过常规曝光与色调映射，仅用于观察；定量比较使用色调映射前的浮点 HDR 读回。

## 数值与场景验证

Apple M4，960 × 720，默认参数，固定相机与曝光，两场景均移除了旧的辅助 RSM 聚光灯。以下数值在新版画廊第 16 帧读取 TSAA 之前的线性 HDR；最终展示图片经过 16 帧 TSAA 累积：

| 场景 | RSM 关闭时平均 RGB | 合成后平均 RGB 增量 | 相对增量 | 纯太阳反弹均值 | 纯天空反弹均值 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Sponza | 0.0686943 | 0.0063504 | 9.24% | 0.00489216 | 0.00148099 |
| San Miguel | 0.0768595 | 0.0023032 | 3.00% | 0.00166885 | 0.000659628 |

数值是全图线性 RGB 算术均值，包括背景；纯贡献的和与 HDR 增量存在 RGBA16F 舍入差异。它们证明两种来源都有可测贡献，不代表物理准确度或运行帧率。截图见 [README 对照](../README.md#场景与效果)。

`MetalRSMTests.cpp` 在 GPU 上执行 15 项检查：11 项具有解析结果的常量 VPL 积分，覆盖采样数／分辨率不改变能量尺度、光源功率线性、黑色／金属／背向接收表面、无效 VPL、背景保留、HDR 合成、不截断高亮和越界样本；另 4 项直接渲染 2 × 2 世界单位的白色平面，验证太阳反射功率为 8、天空功率为 `2π`、二者相加以及背向太阳为零。

```sh
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer --metal-self-test
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ctest --test-dir build --output-on-failure
```

此次隔离构建通过 `metal-gpu`、`metal-classic-gallery`、`metal-gi-gallery` 三个测试；渲染读回无 NaN／Inf，Metal API 与着色器校验通过。隔离构建使这次 RSM 提交与同目录的 RHI 重构分别验证和提交。

## Metal GPU 捕获

使用 Xcode 自带 Metal 调试器与 `MTLCaptureManager`，无需额外安装工具。程序捕获接口参考 [Apple 官方说明](https://developer.apple.com/documentation/xcode/capturing-a-metal-workload-programmatically)。

```sh
mkdir -p build/rsm-capture
MTL_CAPTURE_ENABLED=1 MTL_DEBUG_LAYER=1 \
  SR_METAL_CAPTURE_PATH="$PWD/build/rsm-capture/sponza.gputrace" \
  ./build/Scene-Renderer --render-gallery build/rsm-capture sponza
```

用 Xcode 打开 `.gputrace` 并 Replay，在命令列表筛选 `rsm`。命令编码器分别以 `./src/shader/rsm/lightSpace.fs` 和 `./src/shader/rsm/rsm.fs` 标记；转换后的 Metal 函数名为 `main0`。此次回放确认生成通道有三个 1024 × 1024 RGBA32Float 颜色附件及深度，`skyIrradiance` 绑定 200 × 100 RGBA32Float LUT；合成通道以一个四顶点全屏 draw 读取 RSM、采样序列及五张 960 × 720 HDR／G-buffer 纹理后输出 HDR。Xcode 还提示后端可合并编码器、减少附件带宽，尚未据此完成性能优化。捕获需关闭 `MTL_SHADER_VALIDATION`，当前工具会报告捕获 Shader Validation 不受支持；着色器校验在独立测试运行中启用。

## 当前边界

单个太阳投影仅记录最近表面，太阳视角之外或被遮挡的表面无法成为 VPL。天空入射未计算局部遮蔽，还复用太阳投影记录的表面集合，因而不能覆盖所有天空可见面。VPL 与接收点之间没有完整可见性测试，可能穿墙漏光。局部圆盘与有限采样会缺失远处贡献并产生噪声，当前没有面向间接光的独立空间／时间降噪；最终 HDR 由 [TSAA](../docs/tsaa.md) 做时域抗锯齿和历史裁剪。仅支持漫反射一次反弹；程序地形尚未写入 RSM 成为 VPL，前向对象不在全屏 G-buffer 接收路径内。这些限制需要多投影、间接可见性或其他 GI 方法解决，不能仅靠提高强度补齐。
