# 毛发与布料 Path Tracing

2026-10-09：CPU、Metal、Vulkan PT 新增 `hair` 和 `cloth` 闭包。CPU 与 GPU 编译同一份 `src/rhi/shaders/pt-fiber.glsl` 数学代码，接入既有表面求值、方向采样、PDF、NEE／MIS、Russian roulette、场景包及材质累积失效。此阶段为静态几何和 PT 材质，没有新增实时材质、动力学或 DCC 曲线导入。后续已增加 [针织材质、Bunny 绒毛与中长发](path-tracing-grooms.md)，原始毛球／布料验收图与记录保留。

## 毛发

采用圆形纤维的 R（表面反射）、TT（透射）、TRT（内部反射后透射）及剩余高阶项；纵向散射与绕纤维轴的方位分布分别求值并混合采样。公式参考 [PBRT 4e Hair](https://www.pbr-book.org/4ed/Reflection_Models/Scattering_from_Hair)，本仓库用 RGB 吸收和三角薄带接入，尚未支持椭圆截面、髓质或光谱色散。

```json
{
  "bsdf_model": "hair",
  "base_color": [0.75, 0.4, 0.16],
  "metallic": 0,
  "hair_beta_m": 0.3,
  "hair_beta_n": 0.3,
  "hair_ior": 1.55,
  "hair_tilt": 0.034906585,
  "two_sided": true
}
```

| 参数 | 默认／范围 | 含义 |
|---|---|---|
| `hair_beta_m` | 0.3／[0.1, 0.8] | 纵向散射宽度 |
| `hair_beta_n` | 0.3／[0.1, 0.8] | 方位散射宽度 |
| `hair_ior` | 1.55／(1, 2.5] | 纤维 Fresnel／折射计算，区别于场景介质 `ior` |
| `hair_tilt` | 0.034906585／[-0.2, 0.2] 弧度 | 毛鳞片倾角，默认约 2° |
| `absorption` | [0, 0, 0]／非负 RGB | 可选的纤维光学吸收，以归一化半径计量，不是世界单位介质系数 |

显式 `absorption` 任一通道大于零时使用整组 RGB；否则将既有 `base_color` 解码后的线性颜色 `c` 映射为 `sigma=-0.5*log(clamp(c, 0.0001, 1))`。它表达直径透射颜色，不是 PBRT 的经验反射色拟合或黑色素浓度参数。光学距离按归一化纤维半径计算，当前不随薄带世界宽度变化。

薄带 U 切线沿根到梢，优先采用 `PTTANG01` corner tangent；缺失时从 UV 构造，退化时采用确定性回退。`uv.y` 在带宽两端为 0／1，映射到截面偏移 `h=clamp(2*uv.y-1, -0.999, 0.999)`。普通网格换一个 `hair` 标签不能得到正确的毛发外观；需要正确的薄带、UV 和纤维方向。

样例为固定主相机朝向的锥形薄带，每根六个控制采样、五段、十个三角形。相交、遮挡与投影仍由三角形处理，**不是原生圆柱／曲线相交，也没有随每条光线重新朝向薄带**。侧向照明、背面视角和镜像纤维的截面映射尚需与真实曲线参考单独验证。多根纤维之间的散射来自显式路径反弹，未添加 dual scattering 或体积毛发近似。

## 布料

`cloth` 使用 [Oren–Nayar 粗糙漫反射](https://pbr-book.org/3ed-2018/Reflection_Models/Microfacet_Models)、有界的掠射 sheen 及薄片漫透射。sheen 的角度衰减借鉴 [Disney 2012](https://www.disneyanimation.com/publications/physically-based-shading-at-disney/) 的外观思路；这里是本仓库的能量分配与粗糙度映射，不是 Charlie、OpenPBR 的多次散射 sheen 或完整纱线模型。

```json
{
  "bsdf_model": "cloth",
  "base_color": [0.7, 0.2, 0.12],
  "metallic": 0,
  "roughness": 0.65,
  "sheen_weight": 0.45,
  "sheen_roughness": 0.65,
  "sheen_color": [0.9, 0.35, 0.2],
  "cloth_transmission": 0.2,
  "two_sided": true
}
```

| 参数 | 默认／范围 | 含义 |
|---|---|---|
| `roughness` | 既有材质参数／[0, 1] | Oren–Nayar 的 sigma=roughness×π/2 |
| `sheen_weight` | 0.5／[0, 1] | sheen 的能量份额；底层反射与透射按 1-weight 衰减 |
| `sheen_roughness` | 0.5／[0, 1] | 掠射衰减指数从 8 到 2，增大时光泽更宽 |
| `sheen_color` | [1, 1, 1]／各通道 [0, 1] | **线性 RGB**，与既有 gamma 编码的 `base_color` 区分 |
| `cloth_transmission` | 0／[0, 1] | 底层漫透射比例，不进入封闭体积介质 |
| `yarn_specular_weight` | cloth／knit 默认 0，yarn 默认 0.5／[0, 1] | 底层能量中分配给方向性介质纱线反光的比例，见 [纱线级针织](path-tracing-knitted-yarn.md) |

反射为 `((1-w)*(1-t)*c*OrenNayar + w*sheenColor*(1-dot(v,h))^exponent)/π`；背面透射为 `(1-w)*t*c/π`。反射／透射余弦混合的透射采样概率为 `(1-w)*t`，PDF 同时用于 MIS。有界分配不会简单把 sheen 叠加到原有 PBR 上；没有实现层间 Fresnel、粗糙漫反射多次散射能量补偿或 weave／satin 方向性。`roughness` 沿用既有 ORM／roughness 贴图输入；sheen／毛发宽度本阶段为标量参数。

## 公共输入与边界

`SnapshotDraw.pathTracingBsdfModel` 中 cloth=4、hair=5；`pathTracingFiber` 为 beta_m／beta_n／hair_ior／tilt，`pathTracingSheen` 为 weight／roughness／transmission／yarn specular weight，`pathTracingSheenColor` 为线性 RGB。新字段进入 scene identity，编辑后重置累积。初版 `PackedMaterial` 从 208 B 增为 256 B，后续针织扩展后为 **272 B**；CPU 和 shader 顺序一致，旧 shader 二进制必须重建。旧 JSON 缺省字段保持原有闭包和采样路径。上面的原始 cloth 公式对应 yarn specular weight=0；非零份额和同步更新的 PDF 见 [纱线级针织](path-tracing-knitted-yarn.md#材质输入)。

两个闭包使用朝向当前入射侧的几何法线；法线贴图不参与新闭包的 frame。既有 base color／coverage 输入仍可用。要求 metallic=0、场景 `ior`=0，不与共享 GGX override、各向异性 GGX 或体积 scattering 混合；不合法参数在导入／构造阶段拒绝。

毛发的反向／importance 输运尚未验证，**CPU BDPT 明确拒绝含毛发的场景**。现有 photon mapping 的闭包检查会拒绝 hair／cloth。普通 CPU／GPU 单向 PT 已验证；布料 BDPT 的场景级收敛和两种模型的 Blender GT 对照尚未验收。实时路径仍使用原有材质近似，本页不表示 PT／Raster 已等价。

## 自包含样例与复现

左为 3,500 根静态短毛，右为垂落褶皱布。生成器不需要外部资产；滤除退化三角形后，共 43,310 个有效三角形、6 个实例，几何／BVH 约 7.41 MiB。背后白色矩形是面积光，展示毛发边缘透射与布料薄片透射。**640×360、256 spp、8 次反弹、seed 1，未降噪。**

![Metal PT 毛发与垂落布料，原始 256 spp](../img/path-tracing/hair-cloth.png)

```sh
cmake --build build -j 6
python3 tools/create_pt_fiber_fixture.py --output build/pt-fiber-fixture
./build/Scene-Renderer --path-trace-gpu --backend Metal \
  --pt-scene-file build/pt-fiber-fixture/scene.json \
  --pt-size 640x360 --pt-samples 256 --pt-bounces 8 --pt-fixed \
  --pt-output build/pt-fiber-metal
# CPU 将 --path-trace-gpu 换为 --path-trace；Vulkan 将 --backend Metal 换为 --backend Vulkan。
ctest --test-dir build \
  -R '^(pt-cpu|pt-scene-package|pt-photon|pt-medium|pt-procedural|engine-environment)$' \
  --output-on-failure
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer --pt-self-test --backend Metal
MTL_DEBUG_LAYER=0 MTL_SHADER_VALIDATION=0 ./build/Scene-Renderer --pt-self-test --backend Vulkan
```

CPU 相关 CTest **6/6**，Metal／Vulkan 原生 PT 完整验证均通过。新增检查包含独立球面 PDF 积分、采样／求值对应、无吸收毛发白炉、染色吸收、极窄／宽散射与带边缘／掠射有限性、布料白炉与反射互易性、参数范围、场景包导入、累积失效与 packed ABI。GPU 对照共享参数、切线、两侧散射和 PDF；完整薄带场景另做三后端对照。

固定输入的原始线性 PFM 上，Metal／CPU relative RGB L1 为 **0.00037093（0.0371%）**，总 RGB 能量比 **0.99999553**；毛球矩形区域 L1 **0.2643%**，布料区域 **0.0106%**。区域包括边缘／背景，不是语义材质 mask。Metal／Vulkan 的 PFM 完全相同，三个报告均为零非有限样本。此对照验证本仓库后端一致性，不是独立模型准确度／真实纤维几何的证明。

[验收记录](../img/path-tracing/hair-cloth-validation.json) 保存参数、原始输出 hash、可执行文件／输入／共享方程 hash、三后端报告和检查日志。原始 PFM 可按命令重新生成。运行时间包含不同验证开关与并发负载，不用于比较三后端效率。

后续依次扩展原生曲线相交与一致截面坐标、独立 Blender／PBRT 参考、sheen 重要性采样及多次散射、织物方向性／纹理、毛发反向输运，再接入实时近似与场景资产编辑。
