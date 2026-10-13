# Path Tracing 材质外观扩展

2026-10-08：以丰富实际材质外观为核心，OpenUSD／MaterialX 作为后续可选交换入口；不以引入文件格式代替 BSDF。现有 CPU／Metal／Vulkan 的玻璃、玉石介质、水体、薄叶和 Lambert／GGX 对照闭包继续保留。新增能力按独立数值和实际图像验收，不宣称完整 OpenPBR 或 Cycles Principled 兼容。

## 分阶段路线

M1／M1-R 的实现记录保留在本文。2026-10-09 新增 [毛发与布料 PT](path-tracing-fibers.md)：CPU／Metal／Vulkan 的圆形纤维散射、粗糙漫反射与有界 sheen／薄片透射已验收，属于 M3 的首个子集；多次散射 sheen、实时闭包和独立参考仍待实现。M2／M4／M5 保持后续计划。更详细的共享输入、实时辅助视图、自然材质、分阶段验收与性能约束见 [PT 与实时共享材质计划](shared-material-roadmap.md)。面向自然环境应用的优先级见 [自然环境模拟路线](natural-environment-roadmap.md)。

| 阶段 | 目标与实际外观 | 验收重点 |
|---|---|---|
| M1：方向性 GGX（PT／实时已验收） | 拉丝金属、方向性塑料高光；各向异性和纹理切线旋转 | 求值／VNDF／PDF、白炉、互易、镜像／非均匀缩放、CPU／双后端及 BDPT |
| M2：coat 分层 | 清漆、车漆、上釉陶瓷；独立 coat IOR／roughness／normal，底层衰减 | 层间能量、coat=0 退化、混合采样／正反密度，禁止简单加高光 |
| M3：sheen 与粗糙漫反射（PT 初版已验收） | 静态布料的粗糙漫反射、掠射光泽和薄片透射；毛发另有纤维闭包 | 已有白炉／PDF／CPU 与双后端；多次散射、分层和独立参考待补齐 |
| M4：微表面能量与透射 | 多次散射补偿、粗糙玻璃方向性、导体复折射率 | 白炉／LUT 收敛、透射 Jacobian、介质和 radiance／importance |
| M5：细节与空间变化 | anisotropy／coat 贴图、纹理过滤，玉石非均匀密度；后续薄膜／色散 | footprint 与走样、空间介质 majorant，光谱工作独立验收 |

先使用稳定参数结构和共用语义，CPU 保留可读参考，Metal／Vulkan 使用显式打包 ABI。增加层级时分离材质输入求值与闭包采样；逐步建立内部材质表示，不要求先实现任意节点图或 USD/Hydra。实时预览的近似要单独实现和标注，不能默认宣称与 PT 等价。

## M1 参数与范围

M1 支持默认 `bsdf_model` PBR 和 `ggx_add` 的反射 GGX。场景包新增两个可选字段：

```json
{
  "base_color": [0.98, 0.82, 0.55],
  "metallic": 1,
  "roughness": 0.4,
  "specular_anisotropy": 0.8,
  "specular_rotation": 1.57079632679
}
```

- `specular_anisotropy` 为 [-0.9, 0.9]，默认 0；正负交换两个粗糙度轴。
- `specular_rotation` 为弧度，支持 [-2π, 2π]，默认 0；相对于局部 U／V 切线旋转。
- 旧 `anisotropy` 仍是体积 HG 相函数 g，两者互不替代。
- `alpha=roughness²`，`aspect=sqrt(1-0.9×abs(specular_anisotropy))`，两轴为 alpha/aspect 与 alpha×aspect；负值交换两轴。这是本渲染器的参数映射，不声称 OpenPBR 参数完全等价。
- 优先采用插值的 corner tangent／handedness；用 model 变换切线方向，在最终 shading normal 上投影正交化。未提供时从 UV 导数构造；退化 UV 使用确定性 frame。切线投影退化时也回退，避免 NaN。
- CPU／GPU 的椭圆 GGX distribution、方向性 Smith masking 和 visible-normal 采样对应同一 PDF。GPU 在命中阶段预先计算旋转切线与两轴，避免各次求值重复归一化和三角函数。
- 当前是单次微表面反射；没有加入多次散射补偿。Lambert／薄叶／介电边界上的非零 specular_anisotropy 明确拒绝，粗糙玻璃仍各向同性。实时 Raster 已接入下述共享 GGX 反射模型；本阶段没有新增材质 GUI。

SnapshotDraw 保留 `pathTracingSpecularAnisotropy`／`pathTracingSpecularRotation` 兼容入口；启用共享 `extension.specularShape` 时以共享参数为准。新参数进入 scene identity、CPU mesh material 和 GPU `PackedMaterial.specularShape`；材质修改会失效 PT 累积。PackedMaterial 从 192 增为 208 B，旧场景 JSON 仍可读取；旧 shader 二进制须随源码重新构建。默认零参数保留旧采样分支，原有介质和薄叶 shader 不使用新帧。

公式和采样参考：[PBRT 微表面理论](https://www.pbr-book.org/4ed/Reflection_Models/Roughness_Using_Microfacet_Theory)。未来分层模型参考 [OpenPBR](https://github.com/AcademySoftwareFoundation/OpenPBR)，M1 为本仓库 GGX 扩展。

## 自包含渲染样例

三球从左到右：各向同性、各向异性 0.8／旋转 0、各向异性 0.8／旋转 π/2。共享金属颜色、roughness=0.4、几何和灯光；480×160、256 spp、深度 8、seed 1，未降噪。

```sh
python3 tools/create_pt_material_fixture.py \
  --output build/path-tracing/materials/anisotropic
MTL_DEBUG_LAYER=0 MTL_SHADER_VALIDATION=0 build/pt/Scene-Renderer \
  --path-trace-gpu --pt-scene-file build/path-tracing/materials/anisotropic/scene.json \
  --pt-size 480x160 --pt-samples 256 --pt-fixed --pt-bounces 8 \
  --pt-output build/path-tracing/materials/anisotropic/metal
# CPU 用 --path-trace；Vulkan 用 build/pt-vulkan/Scene-Renderer 并加 --backend Vulkan。
```

验收包含独立半球方向积分与采样估计、PDF 接受频率、正常／掠射白炉、互易性、正负轴与 90 度旋转、零参数退化、镜像 UV frame、参数打包和无效参数拒绝。CPU BDPT 的面积光连接能量与 PT 对照；GPU 回归包含 normal map、corner tangent、旋转及镜像／非均匀模型的 PBR／ggx_add。完整 CPU 相关测试 6/6（2.6 s），Metal／Vulkan 原生 CTest 各 1/1（26.58／27.09 s）。三球样例 CPU→GPU relative RGB L1 为 **7.867×10⁻⁵**，总 RGB 能量比 **0.9999609**；Metal／Vulkan raw PFM 完全相同，三份报告的非有限样本均为 0。原始 PFM／报告在复现输出目录；[验收记录](../img/path-tracing/anisotropic-ggx-validation.json) 保存输入／可执行文件 hash 和三后端报告。

![各向同性、各向异性与旋转方向：Metal PT，未降噪，256 spp](../img/path-tracing/anisotropic-ggx.png)

首次 Metal 扩展 shader 的原生 CTest 超过原 150 s 期限，诊断观察到 MTLCompilerService 持续执行；随后直接运行完整回归通过。未放宽标准 CTest 期限。最终版本改为命中阶段缓存切线／粗糙度轴；首次完整原生验证为 Metal 150.44 s、Vulkan 76.80 s，包含编译及全部验证；随后标准 CTest 为 26.58／27.09 s。首次 native 管线编译仍是独立成本，不能把这些总时间当作准确管线时间，或把热执行时间解释为冷启动上限。后续增加 coat／sheen 时继续评估 shader 变体与编译复杂度。


## M1-R：实时渲染与 PT 共用材质输入

2026-10-08：Metal／Vulkan 的 Forward、Deferred、Scene deferred、Scene forward 和透明表面的 GGX 反射接入同一参数约定。默认 PBR 使用椭圆 GGX D、方向性 Smith G1×G1 和 Schlick Fresnel；`ggx_add` 使用相关 Smith G 与固定 F0。旧材质的 legacy 分支保留；本节只统一已实现的反射闭包，不表示 Raster 已具备 PT 的完整光传输。

逻辑层材质可以直接设置属性，无需 USD：

```cpp
auto p = material->properties();
p.matchedGgx = true; // 也可在各向同性条件下启用与 PT 对应的 Smith 模型。
p.metallicFactor = 1.f;
p.roughnessFactor = .4f;
p.specularAnisotropy = .8f;
p.specularRotation = glm::radians(90.f);
material->setProperties(p);
```

`Material::loadFromJson` 对应 `matchedGgx`、`specular_anisotropy`、`specular_rotation`。非零强度或旋转也会启用新模型。属性复制、范围检查、Snapshot 捕获和材质缓存均覆盖这些参数。修改共享参数会重置 PT 累积及实时 TAA 历史。现有场景包导入会为 PBR／GGX-add 自动填入共享参数，各向同性也使用匹配的遮蔽模型。

`MaterialExtension.specularShape` 的 x／y 是强度／弧度，z 为 0 legacy、1 PT PBR、2 PT GGX-add，w 保留；扩展块从 80 增为 **96 B**，两端 std140 布局一致，shader 资产需重新构建。Scene deferred 在已有 material-effects／tangent 两张 G-buffer 中传递模型、各向异性与旋转后切线；基础 Deferred 从四张扩展为六张，每像素额外 16 B（720p 约 14.1 MiB）。Scene 已有这两张 attachment，数量不增加。

原生主视图支持源 corner tangent／handedness；切线使用 model（包括 instance pose）变换，法线贴图帧使用 inverse transpose。镜像与非均匀缩放保留旋转方向。缺失或退化切线回退到 UV 导数／确定性帧；旋转在最终 shading normal 上完成。网格顶点仍为 32 B，显式切线使用独立 stream，每顶点 16 B 加 16 B metadata。异步上传沿用每帧／每块预算，切线完成后才发布；实例共享基础 stream。

实时环境反射保留八方向天空 LUT 采样，将 footprint 按两个粗糙度轴拉伸。这是固定预算的环境近似，不是 PT 环境积分。水面反射／折射捕获复用新 GGX 着色器，但辅助捕获中的帧仍由 UV 导数构建；显式 corner frame 的特殊资产应进一步扩展该捕获路径。`ggx_add` 的 PT bump-shadowing／反射法线修正、实时阴影／RSM／AO／TAA 和水体／玉石体积输运仍有各自近似。旧 clearcoat／SSS 扩展尚未成为共享 PT 分层闭包。后续 coat／sheen 要同时补齐两条路径的参数与求值验收。

OpenGL 4.1 保留 UV 导数路径，原生显式 frame stream 依赖顶点 storage buffer；本轮未验收 OpenGL 4.1 之后的功能。

实时样例：同一金属从左到右为各向同性、各向异性 0.8、旋转 π/2。480×160，解析方向光加常量环境，实时 tone mapping；与上方 PT 图的照明不同，不能用两张展示图作像素对照。

![Metal Raster GGX 方向与旋转](../img/path-tracing/realtime-anisotropic-ggx.png)

复现运行 `build/pt/Scene-Renderer --pt-self-test` 或 `build/pt-vulkan/Scene-Renderer --pt-self-test --backend Vulkan`。测试在构建的 `rhi/` 目录保存 `metal-realtime-ggx.png`／`vulkan-realtime-ggx.png` 和同名 `.png.json` 报告。数值验收使用独立 64×64 场景、关闭环境／AO／阴影／TAA，将线性 HDR 与 CPU `evaluateBsdf × cosine × light radiance` 对照，覆盖 1,728 个通道、金属／非金属、零／正负各向异性、旋转、UV／显式帧、法线贴图、镜像／非均匀缩放、普通实例和动态材质缓存；附带有界切线上传检查。报告定义误差分母最小为 0.02，允许误差为 0.004 + 1.2%×参考值，包含半精度 MRT 量化及受控法线场景的近似。

最终结果：两后端均对照 1,728 个通道，最大相对误差 **0.00117015（约 0.117%）**；实时 PNG 完全一致。CPU 相关 CTest 6/6，Metal PT native／RHI native、Vulkan PT native／headless RHI validation 均通过。Metal PT native 31.89 s、RHI 37.26 s，Vulkan PT native 38.50 s。Vulkan 首次完整 PT 回归在 150.04 s 超时；随后缓存重跑在原 150 s 期限内通过，冷启动编译成本仍单独保留，不能把热执行时长解释为冷启动上限。这些是回归总时长，不是材质性能基准。详见 [M1-R 验收记录](../img/path-tracing/realtime-anisotropic-ggx-validation.json)。
