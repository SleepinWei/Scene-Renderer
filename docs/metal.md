# Metal 迁移说明（历史兼容层记录）

2026-10-03：默认实现已迁入独立 `src/rhi/MetalDevice.mm`，本文记录此前 GL 接口兼容层方案。当前分层、功能与验收见 [RHI 重构计划](rhi-refactor-plan.md)。`SCENERENDERER_LEGACY_METAL=ON` 才编译本文中的旧桥、ShaderMetal 与旧测试；旧 gallery 入口改为 `--legacy-gallery`，常规 `--render-gallery` 使用新 RHI。

macOS 构建默认启用 `SCENERENDERER_METAL=ON`。程序创建 GLFW `GLFW_NO_API` 窗口，并通过 `CAMetalLayer` 显示画面。GPU 缓冲区、纹理、光栅化、计算、曲面细分、间接命令、界面绘制和画面呈现均由原生 Metal 执行。Metal 构建不创建 OpenGL 上下文，也不链接 OpenGL 框架。

迁移保留了现有场景、组件及其采用 OpenGL 风格的资源接口。`src/metal/MetalBackend.mm` 使用 Metal 实现这些接口；`ShaderMetal.cpp` 加载带有资源反射信息的 Metal 程序，替代运行时 GLSL 编译。因此，场景代码中仍有 GL 标识符，但 GPU 后端已改为 Metal。旧 OpenGL 后端仍可通过构建选项选择，用于其他平台或兼容性检查。

## 构建与运行

需要 Xcode（含 Metal Toolchain）、CMake、Python 3.9 或更高版本，以及以下 Homebrew 依赖：

```sh
brew install glfw assimp yaml-cpp glslang spirv-cross
cmake -S . -B build -DSCENERENDERER_METAL=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
./build/Scene-Renderer
```

请在项目根目录执行运行命令，以便正确读取 `config.json` 和相对路径引用的资产。当前工作副本缺少原始 `asset/` 资源包；如果配置指定的场景不存在，程序会加载自动生成的功能演示场景。

显式选择演示场景：

```sh
./build/Scene-Renderer --demo
```

渲染三帧后退出，适合窗口验证：

```sh
./build/Scene-Renderer --demo --frames 3
```

如需构建旧 OpenGL 后端，可使用独立构建目录：

```sh
cmake -S . -B build/opengl -DSCENERENDERER_METAL=OFF
cmake --build build/opengl -j 8
```

`tools/compile_metal_shaders.py` 在构建阶段使用 glslang 和 SPIRV-Cross 转换原有效果源码，再用 Xcode 编译生成的 Metal 着色语言（MSL）代码。各着色器阶段的 `.metallib` 和 JSON 资源反射信息保存在 `build/metal/shaders/`。这些转换工具仅用于构建；程序运行时直接加载 `.metallib`，不调用 GLSL 编译器。

## 经典场景示例

仓库已包含 Stanford Bunny 和 Khronos Damaged Helmet 的原始模型资源，以及自行构建的 Cornell Box 风格场景。详细来源和许可见 [场景资源说明](../samples/README.md)。

```sh
./build/Scene-Renderer --classic bunny
./build/Scene-Renderer --classic helmet
./build/Scene-Renderer --classic cornell
./build/Scene-Renderer --render-gallery img/metal
```

默认画廊以 960 × 720 离屏渲染基础场景；两个 GI 模型均已下载时，也会加入 Sponza 和 San Miguel。可在输出目录后指定 `core`、`gi` 或单个场景名。测试也覆盖多个场景连续创建时的天空 LUT 初始化。

### Sponza 与 San Miguel

```sh
python3 tools/fetch_gi_assets.py
./build/Scene-Renderer --classic sponza
./build/Scene-Renderer --classic san-miguel
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer --render-gallery img/metal gi
```

大型资源从 McGuire 归档下载，校验值保存在 `samples/gi-assets.json`，不直接提交到 Git。San Miguel 使用上游低面数 OBJ，三角化后仍有约 562 万个三角形，导入需要较长时间和较多内存。

新增 OBJ/MTL 导入保持几何与 UV，读取底色、法线、高度图和透明遮罩，将传统材质参数近似转换为 PBR。植物使用透明裁切和双面绘制，G-buffer、RSM 与两种阴影路径遵循相同裁切规则；阴影顶点阶段传递 UV，Metal 分层阴影适配同步保留这些阶段接口。法线重建对退化 UV 导数使用几何法线，避免产生非有限值。

画廊分别输出 `<场景>-direct.png` 与 `<场景>.png`，仅切换 RSM，相机、曝光、直接光照、天空 IBL 与 SSAO 一致。当前 RSM 以太阳方向的正交投影记录表面，将太阳照射与大气漫反射天空 LUT 合为每纹素反射功率，再通过 G-buffer 全屏采样合成一次漫反射反弹；无太阳和大气时支持聚光灯回退。还输出 `-indirect.png`、`-sun-indirect.png`、`-sky-indirect.png` 用于区分贡献。不具备完整的间接可见性、多次反弹和焦散；对照图不是完整 GI 参考解。具体能量公式、修复内容与捕获步骤见 [RSM 实现与验证](rsm.md)。模型归属、材质转换和上游使用条件见 [场景资源说明](../samples/README.md)。

### 高清海洋

`--classic ocean` 和 `--classic ocean-clear` 使用 1024×1024 主频谱、256×256 短波频谱与 513×513 网格。画廊命令输出 1920×1080 截图及短波、散射或透明开关对照。综合演示使用 512×512 主频谱。具体问题、修复、27 项新增 GPU 数值测试及实时折射／散射限制见 [海洋修复记录](ocean-fft-and-rendering-review.md)。

## 已迁移的实时渲染功能

| 功能 | Metal 实现 |
| --- | --- |
| 延迟渲染、前向渲染与 HDR | 四个 G-buffer 颜色附件、深度附件、光照通道和色调映射 |
| PBR、各向异性、清漆层、次表面散射（SSS） | 转换原有着色公式，通过反射信息绑定材质资源，并提供前后表面深度通道 |
| PBR 曲面细分与位移 | 通过计算着色器执行索引顶点处理、生成控制点和细分因子，再绘制原生 Metal 三角形曲面片 |
| 级联方向光阴影与 PCSS 软阴影 | 分别绘制深度纹理数组的五个切片，保留原有 PCSS 光照算法 |
| 点光源阴影 | 分别绘制立方体纹理的六个面，并写入径向片元深度 |
| 反射阴影贴图（RSM） | 太阳正交投影记录位置、法线和太阳＋天空反射功率，显式 PDF 采样并按 G-buffer 材质全屏合成 |
| TSAA 时域超采样抗锯齿 | 在延迟 HDR 合成后执行 Halton 抖动、深度重投影、海洋运动信息、邻域历史裁剪和时域积累 |
| 屏幕空间环境遮蔽（SSAO） | 保留原有采样核、噪声纹理和遮蔽计算通道 |
| 大气与基于图像的光照（IBL） | 通过计算着色器生成透射率、天空视图、多重散射和辐照度查找表（LUT） |
| FFT 海洋与透明水体 | 共轭 Phillips 频谱、归一化二维 IFFT、1024² 主波与 256² 短波叠加、周期位移／法线／泡沫；场景太阳与 HDR 天空、深度折射、RGB 消光与近似单次散射 |
| 地形 | 保留四叉树队列、间接调度、LOD 图、曲面片拼接和间接绘制 |
| 草 | 在 GPU 上生成草的分布与姿态数据，并进行间接实例化绘制 |
| ImGui | GLFW 处理输入，原生 Metal 渲染器处理字体、管线和绘制列表 |

源码中的 `Cloud` 类仅声明了方法，没有对应实现或体积云着色器，因此当前没有可迁移的既有体积云效果。自动生成的天空、水面及材质场景用于验证迁移结果；原始资产场景仍需补齐资源包。CPU 路径追踪保持独立运行，未迁移为 Metal 路径追踪。

## 正确性与验证

开启 Metal API 和着色器校验，执行 GPU 自检：

```sh
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer --metal-self-test
```

验证真实窗口与界面绘制：

```sh
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer --demo --frames 3
```

自检还执行 15 项 RSM GPU 数值测试，验证太阳／天空功率、采样归一化、接收材质及 HDR 合成。自检会加载全部 `.metallib`，执行海洋高斯随机数计算并读回结果。随后渲染包含五种材质、地形、草、大气和 SSAO 的场景，并切换阴影与 RSM；另行验证仅含海洋的地形配置，以及带有 SSS 前后表面深度的独立前向渲染路径。

浮点纹理读回检查会确认 G-buffer、HDR、天空视图、海洋位移和前向深度结果均为有限值且具有非零输出。截图保存在 `build/metal-*.png`。也可通过以下命令运行同一 GPU 测试：

```sh
ctest --test-dir build --output-on-failure
```

基础测试只依赖仓库内资源。下载完成后可单独启用大型 GI 测试：

```sh
cmake -S . -B build -DSCENERENDERER_METAL=ON -DSCENERENDERER_GI_TESTS=ON
cmake --build build -j 8
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ctest --test-dir build -R metal-gi-gallery --output-on-failure
```

此测试连续导入 Sponza 与 San Miguel，输出 RSM 开关对照，并检查 HDR、法线、RSM 与天空 LUT 的有限值及非空输出。GPU 测试需要访问桌面 GPU；受限的进程沙箱可能无法获取 Metal 设备。

目前已在 Apple M4 上开启 Metal API 和着色器校验完成验证，未报告 GPU 越界或资源绑定错误。真实窗口测试覆盖了 Retina 尺寸处理及 ImGui 绘制，旧 OpenGL 后端也已通过编译。原始场景的视觉对照仍需要缺失的资产包；其他 GPU 型号的兼容性及性能指标尚未测量。

迁移过程中还修复了延迟与前向 HDR 附件绑定顺序、统一缓冲区（UBO）对齐、着色器阶段之间的显式位置匹配、细分阶段的相机缓冲区绑定、大气臭氧参数布局、草的顶点数量、RSM 背景保留及随屏幕尺寸变化的采样坐标。连续创建天空实例时，每个实例都会设置自身的 LUT 尺寸。没有天空的场景会清除环境纹理绑定；RSM 使用一致的光源投影和固定的采样序列。环境遮蔽读取正确的 G-buffer AO 通道；前向 PBR 的光照方向与延迟路径一致，并限制掠射角处 BRDF 分母的下界，避免产生非有限值。

## TSAA 与新版效果图

延迟路径默认启用 TSAA，可通过 GUI 的 `Enable TSAA` 开关控制。当前基础、GI 和海洋画廊在累积 16 帧后导出，开关对照分别清空历史。新增 25 项 GPU 数值与状态测试，具体实现、复现命令及运动向量的覆盖范围见 [TSAA 实现说明](tsaa.md)。

## 当前性能限制

初版后端使用单一命令队列，并等待每帧完成，以保证资源生命周期和读回结果可确定。已经实现管线缓存，但尚未优化为多帧并行提交。后续性能优化需要结合原始资产场景进行分析和测量。
