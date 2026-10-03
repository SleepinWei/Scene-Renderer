# RHI 重构计划

## 最新进度（2026-10-03）

Metal / Vulkan 的主要功能迁移已经落地：默认编辑器通过 SceneAdapter → Renderer → 原生 RHI 执行，默认 Metal 构建不再编译 GL 兼容桥。前向/延迟 PBR、特殊材质、阴影、SSAO、太阳/天空 RSM、大气、海洋、地形、草、计算细分、透明、TSAA、ImGui、Vulkan 窗口呈现及多帧生命周期均已有实际实现。

本轮 Metal/Vulkan 迁移与本机最终验收已完成：Metal 9/9、Vulkan/MoltenVK 9/9、OpenGL 4.1 7/7 通过；场景截图和多帧性能结果见文末。本机 OpenGL 4.1 的基础图形与状态恢复已经复验；4.3+ 的 compute、storage image、GPU 地形/草与 TSAA 按用户要求暂缓。下方各阶段记录保留当时的接口和验收状态，历史记录中的“尚未迁移”不代表当前实现回退。

## 目标与范围

让 SceneRenderer 的实时渲染器通过同一套 C++ RHI 使用原生 Metal、Vulkan 和 OpenGL。保留场景、材质公式与 CPU 路径追踪；逐步消除渲染层的 GL 类型、隐式绑定和后端条件编译。

目标平台：macOS 优先 Metal，Windows/Linux 优先 Vulkan，OpenGL 为桌面兼容路径。macOS OpenGL 4.1 为能力受限的路径，不能承诺运行 compute/SSBO 效果。Vulkan 已可作为 CMake 主后端，也可在启用 Vulkan 的 Metal 构建中通过 `--backend Vulkan` 运行完整编辑器。Windows/Linux 提供构建路径，尚未在这些系统上实机验证。

## 重构前结构与遗留迁移边界

- 旧渲染路径在 `src/metal/MetalBackend.mm` 中通过 GLAD 指针替换模拟 GL 接口与全局状态；新增 RHI 路径直接调用原生 Metal API。
- `Mesh`、`Texture`、buffer 包装和各 Pass 暴露 GL 资源标识及格式。
- `Shader` 通过字符串更新 uniform；Metal 通过反射重新打包参数。
- `RenderManager` 顺序调用 Pass；计算任务的依赖通过 GL barrier 位表达。
- Metal 每帧等待 GPU，资源生命周期目前依赖串行提交。

目标分层：场景/资产 → Renderer（DrawPacket、材质与效果 Pass）→ Pass 调度/资源依赖 → RHI → 各原生后端。RHI 不识别 shader 文件名，不承担阴影、海洋等效果算法。

## RHI 契约

1. 公共头文件只使用 C++、强类型句柄与独立枚举。GL/Metal/Vulkan 类型只存在于后端。
2. Buffer 以 usage 表达 uniform/storage/vertex/index/indirect/copy 用途；Texture 与 TextureView 分离存储与使用；本轮效果采用单 mip/layer 的 2D 纹理，cube/cascade 阴影通过 atlas 表达。通用 mip/array/cube/MSAA API 属于后续扩展。
3. Pipeline 显式包含 shader、绑定布局、顶点布局、附件格式与固定状态。缓存键使用布局而非网格/VAO 身份。
4. BindingSet 使用逻辑组：帧/灯光、材质、对象、Pass 资源。后端负责映射资源索引。
5. CommandList 显式表达 render/compute/copy 操作。OpenGL 可以在渲染线程按序执行，不保证原生命令录制或异步计算。
6. 资源依赖包含访问阶段、读写方式和子资源范围。Vulkan 转换 layout/barrier，Metal 按跟踪策略同步，OpenGL 生成必要的 memory barrier。FFT 的相邻 dispatch 也需要依赖。
7. FrameContext、完成标记、上传与读回、延迟释放先进入接口，再开启多帧并行。
8. 能力查询包括功能、资源数量、对齐与逐格式 usage 支持；不可用功能选择明确变体或报告不可用，不静默成功。

## Shader 与效果迁移

先保留 GLSL，规范化显式资源布局，经 SPIR-V/反射生成 Vulkan SPIR-V、Metal metallib 和目标版本 OpenGL GLSL。验证 CPU/GPU offset、stride、矩阵与数组布局，逐步替换运行时字符串 uniform。

统一定义深度范围、屏幕/纹理原点、绕序与 cube face 方向，并建立验证场景。迁移期间保持既有坐标与布局，避免同时修改渲染数学。

阴影通用路径显式循环 cube face/cascade slice；geometry shader 作为可选优化。细分由渲染层选择实现变体：本轮 Metal/Vulkan 共用计算细分生成 indexed mesh 和 indirect draw；整个网格使用公共整数等级保持边缘一致，不依赖 native patch/tessellation stage。OpenGL 高版本细分/计算变体暂缓。间接参数布局必须有跨后端契约和验证。

## 实施阶段与验收

| 阶段 | 工作 | 验收 |
| --- | --- | --- |
| 0 | 记录现有场景、截图和 GPU 读回 | 建立回归基线，注明硬件与未验证路径 |
| 1A（已完成） | Device、Buffer、能力限制、帧生命周期；迁移相机/灯光/大气/阴影矩阵缓冲 | Metal/OpenGL 构建；真实 GPU 更新与读回；既有 Metal 场景回归 |
| 1B（已完成） | Texture/View、Sampler、Pipeline、BindingSet、CommandList；Vulkan 最小原型 | 三后端纹理三角形；先证实抽象可用于 Vulkan |
| 2（已迁移） | GpuMesh、材质参数块、前向 PBR/HDR | 上层不依赖 VAO 与纹理单元 |
| 3（已迁移） | Pass 附件/依赖、compute/indirect 与完整 Ocean | G-buffer→光照、完整 2D FFT、GPU 数据与依赖验证 |
| 4（已迁移） | 阴影、SSAO、RSM、透明物体 | 全部主要 Pass 通过 RHI |
| 5（已迁移） | 细分、地形、草、ImGui | 能力分级与效果变体验证 |
| 6（已完成本机验收） | Vulkan 主后端、默认 Metal 移除兼容桥、多帧并行 | 本机 Metal/Vulkan 与 GL 4.1 验收；性能测量 |

每阶段保持可运行，不一次重写所有组件。迁移期间共用同一 Device、资源表和命令队列，禁止新旧路径各建独立设备或乱序提交。

## 阶段 1A 实施约束（历史记录）

第一步迁移真实渲染所用的参数缓冲，不先建立空的全套接口。Device 的 uniform 绑定暂时写入现有 shader 绑定表；它是待 BindingSet 替换的过渡入口，不是完整命令系统。

Metal 直接创建 MTLBuffer 并通过现有队列上传，OpenGL 直接创建/更新原生 buffer。句柄具有进程内唯一身份；检查用途、边界、对齐与设备关闭状态。关闭时等待工作完成并回收资源；之后的 RAII 析构安全失效。同步 readback 仅用于验证，后续增加异步 readback。

保留 `SCENERENDERER_METAL` 兼容构建选项，增加后端选择配置；本阶段每个 executable 只编译一个后端。Vulkan 选择明确报错，后续实现工厂后开放。运行时切换不在本阶段范围内。

## 验证策略

- GPU 无关测试：句柄设备归属/失效、越界、用途、uniform range 对齐、关闭与资源释放。
- Metal 真实 GPU：RHI 上传后读回比对，原有 `--metal-self-test` 与经典场景 gallery。
- OpenGL：编译以及有可用 context 时的 RHI GPU 读回。macOS 4.1 不能作为完整 compute 场景验收环境。
- 跨后端图像比较已加入；后续补 Vulkan validation/synchronization validation 环境。
- 不将编译通过描述成运行通过，不将接口占位描述成已支持后端。

## 实施记录

阶段 1A 已实现并通过当前机器验收：

- `include/rhi/Device.h` / `src/rhi/Device.cpp`：强类型 BufferHandle、用途/范围/对齐校验、设备归属、关闭与资源回收。当前接口在单个渲染线程使用；Device 必须在原生 context/设备销毁前显式关闭。
- `src/rhi/OpenGLDevice.cpp` 和 `MetalBackend.mm` 中的 MetalDevice：直接调用原生 buffer API。Metal 与旧入口共用现有设备、资源表和队列；OpenGL 上传使用独立 copy binding 并恢复其原值。
- `UniformBuffer` 不再暴露 UBO 标识或隐式 bind/unbind；相机、三类灯光、大气与级联阴影矩阵通过 RHI 更新/绑定。大气 vec3 上传只读取实际 CPU 数据的 12 字节，保留 std140 的 16 字节槽位与既有 offset。
- 主循环的 beginFrame/present/shutdown 使用 RHI。`SCENERENDERER_RHI_BACKEND` 明确选择 Metal/OpenGL；AUTO 沿用旧选项，Vulkan 选择会失败并说明尚未实现。
- `--rhi-self-test` 验证原生 GPU 初始上传、局部更新、未修改区间和同步读回。独立契约测试覆盖 foreign/stale handle、越界、usage、对齐与关闭后的行为。
- 此阶段曾向旧 Windows 工程同步加入 RHI 源文件；该工程现已移除，构建入口统一为 CMake，未在 Windows 机器运行构建。

2026-10-03，在 Apple M4/macOS 上：Metal/OpenGL Release 构建通过；Metal CTest 4/4 通过（旧 GPU 自检、经典 gallery、RHI 契约与隐藏窗口 GPU 测试），OpenGL CTest 2/2 通过（契约与 GPU 测试）。Metal 测试与 `--demo --frames 3` 开启 API/Shader Validation，通过且未报告校验错误。OpenGL 运行验证使用 macOS 4.1，仅证明本次 buffer 路径。

重构前已运行旧 Metal 自检并将 PNG 复制到 `build/rhi-baseline/`。首次对照显示少量像素差异，图像已目视检查；海洋依赖运行时间，不能要求逐字节一致。后续工作区同时出现其他材质/shader/场景导入修改，最终测试反映当前工作区整体状态，不将全部图像变化归因于 RHI。

以上为阶段 1A 完成时的记录；阶段 1B 的实际接口、后端和边界见下文。

### 构建与验证命令

在仓库根目录执行：

```sh
cmake -S . -B build -DSCENERENDERER_RHI_BACKEND=Metal -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ctest --test-dir build --output-on-failure
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer --demo --frames 3

cmake -S . -B build/opengl -DSCENERENDERER_RHI_BACKEND=OpenGL -DCMAKE_BUILD_TYPE=Release
cmake --build build/opengl -j 8
ctest --test-dir build/opengl --output-on-failure
```

GPU 测试需要桌面 GPU/context 访问。纯契约测试可以单独运行：`ctest --test-dir build -R '^rhi-contract$' --output-on-failure`。`BUILD_TESTING=OFF` 可不构建 RHI 测试程序。

### 阶段 1B：显式图形资源与 Vulkan 离屏原型

已实现以下可执行路径，公共接口见 `include/rhi/GraphicsDevice.h`：

- Texture/View、Sampler、GraphicsPipeline、BindingSet 与 CommandList 均有独立强类型句柄。检查设备归属、失效资源、纹理用途与尺寸、顶点/索引字节范围、uniform 对齐、布局兼容、附件格式和采样反馈。销毁 Texture 前必须先释放 View；View/Sampler 被 BindingSet 引用时拒绝提前销毁。提交前验证整份命令列表，失效引用不会执行部分绘制。
- Metal 直接使用 MTLTexture、MTLSamplerState、MTLRenderPipelineState 和 render/blit encoder；与旧渲染器共享已有设备和队列。OpenGL 直接使用纹理、sampler、program/VAO 和 FBO；执行后恢复旧绑定、视口、深度/混合/裁剪/像素存储等状态。
- `src/rhi/VulkanDevice.cpp` 创建独立 Vulkan 1.1 设备、buffer/image/memory、image view、sampler、pipeline、descriptor sets、render pass、framebuffer、command buffer 和 fence。记录 image layout 转换，以及 attachment→sampled、transfer→host 和连续 Load 的内存依赖。录制失败且尚未提交时回滚 layout 记录。
- `tools/compile_rhi_shaders.py` 将显式布局 GLSL 450 编译为 SPIR-V、反射、GLSL 410 和可选 metallib。Pipeline 创建时检查资源反射、uniform block 最小大小和顶点输入。Vulkan 保留逻辑 set/binding，Metal/OpenGL 映射到 `group * 8 + binding`；Metal 顶点流使用 buffer slot 16。
- GPU 验证来自同一份 `GraphicsValidation.cpp`：2×2 RGBA 纹理上传、纹理三角形、同深度遮挡、索引全屏绘制、两个绑定组、attachment 作为下一 Pass 输入、alpha 混合、Load/Store 与全图回读比对。输出保存至各构建目录的 `rhi/*-triangle.ppm`。

阶段 1B 完成时的契约限定为单线程、同步提交、一个颜色附件与可选 Depth32、单采样 2D 纹理、单 mip/layer、一个顶点流和三角形列表。纹理/读回第零行在顶部，clip 深度为 0..1，NDC 的 Y 向上；OpenGL cooked vertex shader 翻转 Y 并转换深度，Vulkan 使用负高度 viewport，Metal 使用原生约定。颜色 RGBA8/RGBA16F 的支持通过 usage 查询；阶段 1B 的上传和读回只提供 RGBA8，阶段 2 增加 RGBA16F 回读。Store/Load 的 Discard 内容未定义，OpenGL 4.1 允许保留其数据而不承诺 discard 后的像素。索引检查覆盖读取字节范围；索引值加 baseVertex 指向有效顶点的责任仍在调用者。

BindingSet 在创建时验证，并在提交时重新验证引用资源；命令列表保持 Device 存活，不拥有资源的延迟释放副本。可在提交前销毁资源，但这样的列表会被拒绝。Device 必须在原生 context 销毁前关闭；Vulkan 原型的析构也会关闭自有设备。过渡 `bindUniformBuffer` 仅服务 Metal/OpenGL 的旧 shader 路径，Vulkan 明确拒绝该入口，绘制一律通过 BindingSet。

Vulkan 原型单独编译，不把旧 GL 渲染器链接到 Vulkan。启用 `SCENERENDERER_VULKAN_PROTOTYPE=ON` 并提供 Vulkan SDK：

```sh
cmake -S . -B build/vulkan -DSCENERENDERER_RHI_BACKEND=OpenGL \
  -DSCENERENDERER_VULKAN_PROTOTYPE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build/vulkan --target rhi-vulkan-validation -j 8
ctest --test-dir build/vulkan -R '^rhi-vulkan-gpu$' --output-on-failure
```

macOS 可显式传入 `-DVulkan_INCLUDE_DIR=/absolute/path/MoltenVK/include` 与 `-DVulkan_LIBRARY=/absolute/path/libMoltenVK.dylib`。本机使用临时下载到被忽略的 `build/_deps/` 的官方 MoltenVK 1.4.2，无全局安装要求。Windows/Linux 使用对应 Vulkan SDK 与驱动；这些平台尚未实际运行。创建 instance 时若存在 `VK_LAYER_KHRONOS_validation` 会启用它，否则输出 unavailable；本次本机未安装该层，不能称为通过 Vulkan validation/synchronization validation。

CMake 构建需要 Python、glslangValidator 和 spirv-cross；Metal 还需要 Xcode Metal compiler。项目构建与 shader cook 统一由 CMake 管理；Visual Studio 工程可由 CMake 在构建目录生成，运行自检时从仓库根目录启动。Windows 构建尚未实机验证。

2026-10-03，Apple M4/macOS 上的最终阶段 1B 验收：

| 构建/路径 | 结果 |
| --- | --- |
| Metal Release，API + Shader Validation | CTest 5/5；包括经典场景回归与 RHI GPU/契约测试 |
| OpenGL 4.1 Release | CTest 4/4；包括纹理绘制及旧状态共存/恢复测试，退出前无 GL error |
| Vulkan 1.1 / MoltenVK 1.4.2 | 离屏 CTest 1/1；同一资源/命令/像素验证通过 |
| 三后端图像比较 | 64×64 RGB 输出逐字节一致，最大通道差为 0 |

GPU 无关图形契约测试另覆盖 foreign/stale 资源、使用中的 View/Sampler 销毁、uniform 范围/对齐、错误布局/附件、顶点和索引读取范围、采样反馈、整份提交的原子验证、重复提交与关闭回收。OpenGL 共存测试主动设置非默认像素存储、PBO、UBO range、纹理/sampler、线框、深度范围、混合公式、颜色/深度写掩码，以及裁剪/stencil/discard 等状态，再验证绘制结果与状态恢复。测试修复了 OpenGL 4.1 下 sampler 查询必须依据当前活动纹理单元的问题。

额外尝试同时为 MoltenVK 开启 `MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1` 时，测试停在 `vkDeviceWaitIdle` 内的 MetalTools `ResourceUsageTable::realloc`，尚未进入纹理绘制。进程采样保存在 `build/rhi-vulkan-validation.sample.txt`；已终止该次测试，常规配置立即复跑通过。该调试组合仍未验证通过，不能用原生 Metal 后端通过校验的结果代替它。GPU 测试已设置 60 秒超时。

多数场景 Draw/Texture/Compute 仍走兼容桥。阶段 1B 完成时没有 Vulkan swapchain/场景渲染、compute/indirect 公共接口、cube/array/mip/MSAA、异步上传或多帧并行。阶段 1B 结束时的后续目标是迁移 GpuMesh、材质参数块与前向 PBR/HDR，阶段 2 的实施见下文；阶段 6 才移除兼容桥。

参考：[Vulkan 同步示例](https://docs.vulkan.org/guide/latest/synchronization_examples.html)、[Metal 资源同步](https://developer.apple.com/documentation/metal/resource-synchronization)、[SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross)。


### 阶段 2：GpuMesh、材质参数块与前向 PBR/HDR

已增加 `include/renderer/rhi/` 与 `src/renderer/rhi/`：

- `GpuMesh` 拥有独立的 vertex/index Buffer，使用 32 字节 position/normal/UV 顶点布局；上传前校验索引值、三角形数量与非有限数据。DrawPacket 只持有 GpuMesh、GpuMaterial 与 model matrix，不包含 VAO、纹理单元或 GL 类型。
- `GpuMaterial` 将 albedo、normal、metallic、roughness、AO 与 48 字节 MaterialParameters 组成显式 BindingSet。参数使用三个 vec4；alpha cutoff、metallic/roughness/AO factor、normal strength 和 emissive 均通过参数块读取。缺失贴图有明确默认值；不会对空纹理静默采样。albedo 按原有公式手动进行 gamma 解码，数据贴图保持线性。
- `ForwardPbrRenderer` 使用 CameraVertex、ObjectData、SceneLighting 参数块。每个 DrawPacket 使用独立对象缓冲，避免整份列表读到最后一次 model 更新。CPU 计算逆转置 normal matrix，拒绝奇异变换；C++ ABI 的 size/offset 与 SPIR-V 反射逐项核对。支持方向、点与 spot 灯，最多共 30 个；方向向量归一化，point 沿用旧前向 PBR 的常量衰减，spot 增加锥角权重。
- 前向 Pass 写入 RGBA16F + Depth32，后续 tone map Pass 采样 HDR 写入 RGBA8。曲线保持旧 `hdr.fs` 的 `1-exp(-hdr*exposure)` 与 gamma 2.2。resize 先创建完整新附件，再替换旧附件；失败后原目标仍可绘制。新增三后端 RGBA16F 同步回读以验证 HDR 未提前截断。
- `SceneAdapter` 在渲染线程把现有 RenderScene 的 CPU Mesh/Material/Transform/Camera/Light 转成新帧数据。保持旧模型变换顺序，在边界将深度 -1..1 转为 0..1，并将旧 UV 原点转成 RHI 原点。按实际索引压紧 CPU 顶点，避开旧 sphere 工厂未引用的尾部数据。缓存几何/贴图，材料标量每帧更新；修改已缓存的几何或贴图须调用 `invalidateAssets()`。资源表移除未使用项目，DrawPacket 的 shared_ptr 保持本帧资源存活。
- ImageDecoder 使用独立静态 STB 实例，固定顶部为第零行；不改变旧加载器的全局翻转状态。适配器优先读取 `texture_path` 或 Texture 的文件名，也支持未释放的未压缩 CPU 像素。GPU-only、DDS 压缩或无 CPU 数据的匿名纹理明确报错。
- `Resources` 统一按 BindingSet → Pipeline → View → Sampler → Texture → Buffer 顺序回收；设备关闭后的上层析构安全失效。Metal/OpenGL 新增 display-ready RGBA8 到当前 backbuffer 的原生拷贝/呈现路径，仍由调用者负责 beginFrame/present；Vulkan 原型明确拒绝窗口呈现。

新入口直接运行现有 RenderScene/组件创建的球体与地面，通过新 DrawPacket、材质和 HDR 路径绘制。这个入口不会调用旧 Mesh::genVAO、Material::genTexture 或 Shader::set*：

```sh
./build/Scene-Renderer --rhi-forward
./build/opengl/Scene-Renderer --rhi-forward

# 有界运行与离屏图像输出，同时验证实际窗口 backbuffer copy/present。
./build/Scene-Renderer --rhi-forward --hidden --frames 3 --screenshot build/rhi/forward-scene.ppm
```

前向 RHI 入口当前使用内置场景、固定相机与 PBR 基础材质。SceneAdapter 将所收集的三角形视为基础 PBR，不保留旧 Shader 对象的自定义行为；只接受 filled triangles。默认 RenderManager 的 deferred/RSM、天空、地形、透明排序、阴影、IBL、细分、clearcoat/anisotropy/SSS 及 ImGui 尚未迁移。该入口是阶段 2 的真实场景路径，不能据此称为全仓库已脱离兼容桥。现有 CPU Mesh/Material 类型的旧 API 为其他 Pass 保留，新渲染核心不使用它们的 GL 资源标识。

GPU 验证增加两对象深度与 alpha hole、材料更新、法线贴图切线方向、exposure 更新、成功/失败 resize、图像解码行序、HDR 值 >1，以及 BRDF/映射曲线与 CPU 参考比对；同一份验证在 Metal/OpenGL/Vulkan 执行。窗口场景使用三个不同颜色/粗糙度/金属度的球和地面，并保存实际新前向输出用于目视检查。此阶段曾同步更新旧 Windows 工程，现已统一为 CMake，仍未在 Windows 机器上编译；Vulkan 仍以 macOS/MoltenVK 离屏验证为限。

阶段 2 最终验收（2026-10-03，Apple M4/macOS）：Metal Release 6/6（开启 API/Shader Validation）、OpenGL 4.1 Release 5/5、Vulkan/MoltenVK 离屏 1/1 通过。64×64 前向测试图在三后端逐字节一致，最大通道差为 0。真实 SceneAdapter 场景分别完成 3 帧与 backbuffer 呈现；截图保存在 `build/rhi/forward-scene.ppm`、`build/opengl/rhi/forward-scene.ppm`，Metal PNG 预览为 `build/rhi/forward-scene.png`。已目视检查三个球的颜色/金属高光和地面。验证针对本次基础 PBR 路径，Vulkan validation layer 与 Windows/Linux 实机验证仍未完成。

阶段 2 完成时的下一步是扩展附件与资源依赖模型，迁移 G-buffer→光照链路，再将 compute/indirect 纳入 RHI。阶段 3 的实施见下文，前向路径保留为回归场景。

### 阶段 3：MRT、Pass 依赖、延迟光照与 buffer compute/indirect 基础路径

2026-10-03 已实现以下可运行基础路径，前向路径继续作为对照：

- `GraphicsPipelineDesc.additionalColorFormats` 和 `RenderPassDesc.additionalColors` 扩展现有单附件接口。每个附件独立设置 Clear/Load/Discard、Store/Discard 和 clear color；按设备查询附件上限。共用层校验数量、格式、尺寸、操作、非有限清屏值、重复底层纹理和所有附件的采样反馈。Pipeline 创建时还检查 fragment 反射的输出数量、连续 location 和 vec4 类型。
- Metal 使用多个原生 color attachment；OpenGL 设置多附件 FBO/draw buffers，切换回单附件时解除旧附件；Vulkan 创建匹配的 attachment descriptions/references、blend states、framebuffer 和 clear values。OpenGL 额外恢复各 draw buffer 的颜色写掩码、blend enable/function/equation，以及 indirect buffer 绑定，避免覆盖旧路径的 indexed state。
- `ForwardPbrRenderer` 构造参数新增 `PbrPath::Deferred`。几何 Pass 输出四张 RGBA16F G-buffer：world position + validity、normal + roughness、linear albedo + metallic、emissive + AO；Depth32 保持遮挡与 alpha cutoff。光照 Pass 采样 G-buffer，写入 HDR，再沿用 tone map。`pbr-material.glsl` 与 `pbr-lighting.glsl` 在前向/延迟 shader 中共用，避免公式分叉。G-buffer 采用半精度世界坐标，当前面向有限尺度的演示场景；大型世界后续应改为相机相对坐标或从深度重建。
- `CommandList::dispatch` 录制一个独立 compute Pass，可与 render Pass 交错，必须在 render Pass 外调用。`ComputePipelineHandle` 独立于 graphics pipeline；反射校验固定 workgroup、资源名称/类型和 block size，检查 workgroup/dispatch/绑定数量、buffer 用途、offset 对齐、范围及设备归属。StorageRead/StorageWrite/StorageReadWrite 当前只供 compute buffer；同一 dispatch 中只读别名允许，涉及写入的重复底层 buffer 明确拒绝，即使两个范围不重叠。
- 提交前从 attachment、shader binding、vertex/index/indirect 用途推导 `ResourceAccess`、`ResourceStage` 和 `ResourceDependency`。依赖粒度当前为整个 buffer/texture，记录同一列表中的 write→read、write→write 和 read→write。Metal 使用跟踪资源及 encoder 边界；Vulkan 使用 Pass 前的 ALL_COMMANDS 内存依赖与 image layout 转换，结束时添加 shader write→host read；OpenGL compute context 在 Pass 边界和读回前使用 GL_ALL_BARRIER_BITS。后端同步目前保守覆盖依赖，尚未按记录裁剪 stage/access/range。同步提交和这些边界也覆盖连续列表；未来异步、多队列或子资源支持不能直接沿用这套简化模型。
- `drawIndirect` / `drawIndexedIndirect` 使用统一 16/20 字节参数 ABI，检查 Indirect 用途、4 字节 offset 对齐和参数字节范围。三个后端都调用原生 indirect API。初始跨后端契约要求 `firstInstance=0`，indexed indirect 要求 `indexOffset=0`；GPU 生成的 count、索引值、firstVertex/firstIndex/baseVertex 是否落在实际数据范围内由生产者保证，不通过 CPU 回读参数来模拟 indirect。
- Metal 与 Vulkan 实际运行 buffer compute。OpenGL 4.3+ 实现提供原生 compute/SSBO capability 查询与命令，并恢复 SSBO range 状态；本机 macOS OpenGL 4.1 返回 compute 不可用并拒绝创建，不自动转成 CPU。GLSL cook 增加 `.comp` → SPIR-V/反射/GLSL 430/metallib，graphics 继续 GLSL 410；共享 `.glsl` include 纳入构建依赖。

实际窗口入口与阶段 2 共用 SceneAdapter、GpuMesh、GpuMaterial、DrawPacket 及 present：

```sh
./build/Scene-Renderer --rhi-deferred
./build/opengl/Scene-Renderer --rhi-deferred
./build/Scene-Renderer --rhi-deferred --hidden --frames 3 --screenshot build/rhi/deferred-scene.ppm
```

验证增加以下项目，执行入口仍为 `--rhi-self-test` 和 `rhi-vulkan-validation`：

1. 四张 G-buffer 数据、背景 validity、两对象遮挡与 alpha hole、法线贴图、exposure 和成功/失败 resize。前向/延迟逐像素 HDR 比较容差为 `0.025 + 0.005 * abs(forward)`，tone mapped 通道差不超过 2，允许 G-buffer 半精度量化。
2. CPU 参数的 indirect 与 indexed indirect 图像一致，三个后端都运行此检查。compute 可用时，先清零顶点/参数，再执行 seed → generate → indirect draw；回读 64 个种子、顶点和参数，逐项比较，并再次提交验证跨列表依赖。
3. 8 点复数 radix-2 FFT，通过三个独立 butterfly dispatch 和 ping-pong buffer 实现，与 CPU 直接 DFT 比较实部/虚部（绝对误差 <0.0001）。每轮使用独立 stage uniform，避免录制时覆盖先前 dispatch 参数。
4. GPU 无关测试覆盖 MRT 重复纹理/尺寸/格式不匹配/采样反馈、compute foreign/stale pipeline、非法线程数/dispatch/offset、写入别名、indirect 用途/对齐/越界，以及自动依赖的访问方式与阶段。整份列表验证仍发生在原生执行前，关闭时按依赖顺序释放 compute pipeline。

本机验收：Metal 完整 CTest 7/7，API/Shader Validation 开启；新增 FFT 后 RHI 5/5 再次通过。OpenGL 4.1 完整 CTest 6/6；Vulkan/MoltenVK 离屏 1/1，包含真实 compute/FFT/indirect。三后端 64×64 延迟测试图逐字节一致，最大通道差 0。实际新延迟场景完成 3 帧并呈现；已目视检查 `build/rhi/deferred-scene.png` 的球体材质、高光与地面。Vulkan Khronos validation layer 仍不可用，OpenGL 4.3+、Windows/Linux 和 Vulkan 窗口呈现仍未实机验收。

阶段 3 当前完成基础验证路径。FFT 验证是小型 buffer 算法，旧 Ocean 的完整 2D FFT/image compute、RSM/SSAO/阴影/IBL 等默认 RenderManager Pass 尚未迁入；storage image、精确子资源依赖、异步提交和 compute→indexed-indirect 参数生成也仍待扩展。后续先补 storage texture 与 2D FFT，把现有 Ocean/RSM 逐个迁入新命令接口，再进入多帧资源生命周期与兼容桥移除。当前新入口可实际运行，但不会据此把整个仓库标为已完成 RHI 迁移。


### 持续迁移：Metal / Vulkan 优先（历史过程记录）

本轮范围为完成余下迁移；不把可运行的新演示入口等同于全仓库迁移完成。保留各阶段历史记录，后续进度只更新本节与文首。

已经实机验证：

- 新 storage image 契约支持 RGBA32F/16F/8 格式、读/写限定、compute 采样、float 上传/读回、别名检查和依赖；Metal 原生 texture binding，Vulkan storage image descriptor/GENERAL layout。
- 完整 Phillips spectrum → 横/纵 Stockham IFFT → displacement → normal/foam。N=8/16 对独立 CPU 2D IFFT、N=1024 对解析单频解；完整光谱对 CPU 参考，最大相对误差约 1.82e-6。种子、时间、零风与参数变更有验证。
- Vulkan GLFW surface、可呈现 graphics/compute queue、FIFO swapchain、acquire、copy/blit、present 与 resize 重建；实际延迟场景完成 3 帧窗口呈现。Khronos validation layer 仍未安装，本机结果不等于 Vulkan validation 验收。
- D32 采样/读回、depth-only pass、逐 Pass viewport/scissor、texture copy。阴影 atlas 按方向光 5 cascade、点光 6 face、spot 1 face 分配；alpha 裁剪参与深度与 RSM。PCF 使用接收平面导数补偿深度斜率，避免低分辨率 atlas 自阴影。
- 四张 G-buffer → SSAO → 阴影/环境/RSM 光照 → HDR/tone。RSM 捕获 flux/world position/normal，间接照明通过 atlas VPL 采样。GL 4.1 的 scissor box 与 copy framebuffer 状态恢复同时修复。
- 原有大气 transmittance/skyview/multiple-scattering/convolution 公式通过显式参数块与 storage image compute 执行；天空与环境照明进入延迟光照。海面保留原有双尺度位移、法线、泡沫、Snell 折射步进、吸收/散射、GGX 高光与 motion 输出；HDR opaque snapshot 和上一帧位移使用 CommandList copy。

Metal API + Shader Validation 与 Vulkan/MoltenVK 同一组 GPU 检查均通过：阴影遮挡 262 像素、SSAO 遮挡 595 像素、海面变化 380 像素，两个后端计数相同。天空 LUT 检查有限值/非负/透射率 0..1、太阳角度变化；海面检查 compute→vertex sampling、折射开关、零振幅稳定性、上一帧纹理 copy 和 resize。

新完整场景入口为 `--rhi-scene`，可配 `--sky`、`--ocean`；启用 Vulkan 的构建可加 `--backend Vulkan`。它仍是迁移验证入口，暂不替代默认编辑器。

地形/草与 TSAA 新增验收（同机 Metal / Vulkan）：

- `GpuTerrain` 保留旧 quadtree 的 5 根节点/轴、6 级遍历、距离 LOD 公式、邻居 LOD map 和边界拼缝。逐级队列使用原生 indirect dispatch，无 CPU 计数读回参与调度；完整顶点/索引输出经 indexed indirect draw 进入 G-buffer。测试产生 4,870 个叶节点，与独立 CPU quadtree 集合完全一致；远相机收缩到 25 个根叶节点。容量按完整树计算，取代旧队列的过小固定 buffer。
- 草从近处叶节点生成随机 pose，保持贴合 bilinear height，加入风弯曲。Vertex shader 只读 pose storage buffer，GPU 输出实例数量给 indexed indirect draw；容量检查阻止旧路径的写出界。GPU atomic 排序及容量截断、浮点随机函数会影响两个后端的被截断草分布，不以逐像素一致作为草的验收。分别检查实例数量、有效 pose、高度、实际像素及风变化。
- 旧 `tsaa.comp` resolve 数学迁入统一 compute shader，修正 0..1 深度和顶部纹理行约定。提供 HDR/depth ping-pong history、Halton jitter、逐对象 previous model/motion、遮挡变化/深度拒绝、scene/camera/enable/resize reset。草为 reactive surface，海面使用实际上一帧 displacement。Metal API/Shader Validation 与 Vulkan 均通过静态 HDR、移动 emissive 物体无拖影、resize 和 history reset 检查。
- 显式绑定组扩展至 3 组，每组 8 个槽位；Metal RHI 顶点流移至 slot 30。旧阶段记录中的 2 组/slot 16 为当时 ABI。这里只读 vertex storage 已开放，graphics storage 写入仍拒绝。

继续迁移特殊材质、透明/细分与编辑器入口；兼容桥移除、多帧并行仍未验收。


### 阶段 4–6：默认原生渲染器与功能迁移

- `src/rhi/MetalDevice.mm` 独立创建 MTLDevice、queue、buffer/texture/pipeline 与 encoder，不包含 GLAD 或 GL 状态模拟。`SCENERENDERER_LEGACY_METAL` 默认 OFF；旧桥、旧 shader cook 与旧 gallery 只在显式开启该历史选项时编译。常规 `--demo`、`--classic` 和 `--render-gallery` 均走新 RHI。`src/metal/` 保留供历史回归，原 OpenGL 的旧组件/Pass 也保留。
- 场景和 OBJ 导入已提取到 `FeatureScenes`、`ClassicScenes`、`FeatureGallery` 与 `SceneImport`；两后端共用材质球、Cornell、Bunny、Helmet、Sponza、San Miguel、深海与浅水入口，不创建旧 Shader/Framebuffer。CPU 资产的 GL 格式/绘制元数据只在 SceneAdapter 边界转换，新 Renderer 不使用旧资源 id。
- 原生 ImGui 上传字体 atlas 与顶点/索引，显式 clip/scissor、baseVertex 和 alpha blend，支持注册 RHI 纹理。编辑器保留相机、灯光、大气、地形、海洋与效果控制；前向/延迟、HDR、SSAO 半径、RSM 采样与源开关连接到新参数块。地形线框使用实际 Metal/Vulkan raster state，Vulkan 查询并启用 fillModeNonSolid。
- Clearcoat、anisotropy、SSS 与 unlit 参数进入 32 字节扩展块；特殊贴图打包成 coat/anisotropy/height/thickness。SSS 背面深度使用明确的 CCW 绕序与 front cull，已经加入闭合壳体深度测试。双面植被和草保留背向受光。真实场景局部灯光恢复原延迟路径的 `min(1,1/distance²)` 衰减，基础前向数学验证仍可选常量衰减。
- 透明表面按相机空间从远到近排序，保持 opaque depth，以独立附件 blend 状态覆盖 reactive motion；两层 RGB、遮挡、输入顺序不变性与 TSAA 均有 GPU 验证。海洋继续采用独立折射/散射路径。
- `GpuSubdivision` 使用 barycentric 计算细分和 height 位移，1–10 级经 CPU 插值、面积、绕序与实际 indirect draw 验证；可配置上限 32，按容量拒绝超出分配/dispatch 限制的网格。当前每个网格统一选择保守 LOD，重用同一生成网格进行 G-buffer、阴影和 motion；不宣称 native patch draw 或逐边连续自适应细分。
- RSM 从三类灯的 depth atlas 中分离出独立 1024×1024 的 position/normal/flux 捕获。室外选择太阳/大气，室内回退 spot；使用带纹素对齐的相机跟随正交投影，flux 保留贴图法线、金属度、表面面积和天空 irradiance。gather 保留 disk PDF、Lambert 两次 `/PI`、最小距离、采样半径/数目/强度与 indirect-only；随机图改为确定性的均匀盘采样。低分辨率阴影 atlas 与 8 tap 环境粗糙反射仍是当前实现的画质边界，不要求旧截图逐像素一致。
- DDS 通过 CPU 解码 BC1/BC2/BC3、DX10 BC1–3 与 RGBA32 首 mip，上传统一 RGBA8；拒绝不支持的格式、cube/array 与截断数据。新增 CPU 测试覆盖透明 palette、alpha 插值、边缘 block、通道 mask 和非法输入。CPU Texture/Sky/terrain height 数据的释放已明确，避免旧上传释放后再次析构；加载线程使用局部 STB flip 状态。
- Device 完成 token 包含设备身份与 serial，最多 3 个在途提交；upload 使用有序 staging copy，readback 返回异步 ticket，Resources 在 token 完成后按引用依赖顺序回收。Metal 使用 command buffer 状态，Vulkan 使用 fence 与保留的 descriptor pool/FBO；Vulkan presentation semaphore 按 swapchain image 保存并在重新 acquire 后复用，正常呈现不执行 queueWaitIdle。swapchain acquire 延迟到 backbuffer copy，无呈现的原生帧可独立 endFrame。
- 20 帧测试验证重复 UBO 更新、异步读回、临时顶点释放与有界 completion ring；CPU 契约验证未完成资源不会提前释放、foreign/future token 拒绝与 shutdown。编辑器测试加入真实窗口 resize、TSAA 附件重建与呈现。场景 destroy 清空 spot light 并更新 history revision，避免重新加载同一个 RenderScene 地址时保留旧 history。
- Shader cook 检查 include/工具版本 hash；Vulkan 保留 set/binding，Metal buffer 为 `group*8+binding`，texture/sampler 每个 stage 分别压紧映射，顶点流使用 slot 30。各 stage 的采样槽位超限会在 cook 时明确报错。OpenGL 4.1 的链接器可优化掉已经经 SPIR-V 验证的非活动绑定，后端允许它们没有 linked location。

### 最终验收（2026-10-03）

硬件为 Apple M4，系统为 macOS，构建均为 Release。默认 Metal 使用独立原生 `MetalDevice.mm`，`SCENERENDERER_LEGACY_METAL=OFF`；Vulkan 使用 MoltenVK 1.4.2，分别验证双后端构建和以 Vulkan 为主后端的完整程序。

| 构建 | CTest | 验证范围 |
| --- | --- | --- |
| `build`，Metal + Vulkan | 9/9 通过 | Metal API/Shader Validation 开启；完整 GPU 算法、资源生命周期、前向/延迟、编辑器 resize/UI、Vulkan 离屏回归 |
| `build/native-vulkan`，Vulkan 主后端 | 9/9 通过 | 同一 GPU 数值/像素测试、实际 GLFW swapchain 呈现、完整编辑器与 resize |
| `build/opengl`，OpenGL 4.1 | 7/7 通过 | CPU 契约、DDS 解码、基础图形/间接绘制、前向/延迟与旧 GL 状态恢复；compute 不可用 |

最终 GPU 测试包括方向/点/spot 接收面阴影、背向植被受光、局部灯光距离衰减、太阳/天空 RSM flux 开关与前向/延迟场景结果对照。20 帧测试验证在途资源更新、异步 readback、临时资源释放及 completion ring；CPU 测试检查未完成 token 的延迟释放。

经典场景已在两后端重新生成和目视检查，固定时间为 8 秒、每张图运行 16 帧。Bunny/Helmet 的平均 RGB 通道差分别约 0.00128/0.00096（8 位值），最大通道差为 1。Cornell 平均差约 0.00909，最大差为 87；少量阴影/几何边界像素有较大局部差异，不能宣称实际场景逐像素一致。图像分别位于 `build/rhi/gallery-metal/` 和 `build/rhi/gallery-vulkan/`。

Sponza 在 Metal/Vulkan 均生成完整光照、直接光、纯间接光及太阳/天空分别贡献图；25 个 mesh、262,267 个三角形完成实际导入。San Miguel 在两后端的默认编辑器完成 281 个 mesh、5,617,451 个三角形的导入与 8 帧运行。深海与透明浅水在 Metal/Vulkan 两后端均完成 1920×1080 输出；同一海洋数值与光学 GPU 验证也在两后端通过。固定时间截图的平均 RGB 通道差为：Sponza 约 0.00119、纯间接光约 0.00016、深海约 0.00091、浅水约 0.00025（8 位值）。已目视检查材质球、纹理、天空、海水透射、阴影、草及 ImGui 截图。

帧并行测量使用同一内置 demo、800×450、固定时间 8 秒、隐藏窗口并保留 ImGui、360 帧；关闭验证，依次运行以避免 GPU 测试互相争用。计时覆盖渲染循环、首次资源上传/PSO 创建和末尾 waitIdle，属于一次端到端吞吐样本，不能当作独立 GPU 时间或跨平台结论。

| 后端 | 1 个在途提交 | 3 个在途提交 | 总耗时下降 |
| --- | --- | --- | --- |
| Metal | 4.81457 秒，13.37 ms/帧 | 3.75636 秒，10.43 ms/帧 | 22.0% |
| Vulkan/MoltenVK | 6.54218 秒，18.17 ms/帧 | 5.44345 秒，15.12 ms/帧 | 16.8% |

复现入口（在仓库根目录运行，Vulkan SDK 路径按本机安装设置）：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSCENERENDERER_RHI_BACKEND=Metal -DSCENERENDERER_LEGACY_METAL=OFF -DSCENERENDERER_VULKAN_PROTOTYPE=ON
cmake --build build -j 8
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ctest --test-dir build --output-on-failure

cmake -S . -B build/native-vulkan -DCMAKE_BUILD_TYPE=Release -DSCENERENDERER_RHI_BACKEND=Vulkan
cmake --build build/native-vulkan -j 8
ctest --test-dir build/native-vulkan --output-on-failure

./build/Scene-Renderer --demo
./build/native-vulkan/Scene-Renderer --demo
./build/Scene-Renderer --classic cornell --forward --frames 8
./build/Scene-Renderer --render-gallery build/rhi/gallery-metal core
./build/Scene-Renderer --render-gallery build/rhi/gallery-vulkan core --backend Vulkan
./build/Scene-Renderer --render-gallery build/rhi/gallery-metal sponza
./build/Scene-Renderer --demo --hidden --size 800x450 --frames 360 --time 8 --frames-in-flight 1
./build/Scene-Renderer --demo --hidden --size 800x450 --frames 360 --time 8 --frames-in-flight 3
```

本轮迁移不再有待实现的 Metal/Vulkan 实时效果 Pass。保留旧 OpenGL 组件和可选历史 Metal 桥供回归；CPU 资产中的旧格式元数据仅在 SceneAdapter 边界转换。后续边界为 OpenGL 4.3+ 实机验证、Windows/Linux 构建/运行验收、Khronos validation/synchronization validation、通用 mip/array/cube/MSAA、逐边自适应细分与更高质量环境反射。已有 `Cloud` 声明、自动曝光和 GPU 路径追踪没有旧实现，不列作此次迁移遗漏。

本机 Khronos validation layer 不可用。MoltenVK 在继承 MetalTools API/Shader 插桩时曾阻塞，Vulkan CTest 在 macOS 显式关闭这两项；上述 Vulkan 通过结果证明实际运行与数值回归，不能代替 validation layer 结果。原生 Metal 的验证保持开启。可选 `SCENERENDERER_LEGACY_METAL=ON` 路径此次未重新验收。
