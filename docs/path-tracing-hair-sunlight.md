# 强日光头发渲染展示

2026-10-09：使用 [Cem Yuksel HAIR Models](https://www.cemyuksel.com/research/hairmodels/) 的直发与波浪发，每套保留全部 **50,000 根发丝**、原始控制点与宽度。配套头模由 **Murat Afshar** 提供，采用中性 Lambert 漫反射。见[资产署名与使用条件](../samples/licenses/yuksel-hair.txt)。

毛发采用 Chiang／PBRT 风格圆形介质纤维散射，包含表面反射 R、透射 TT、内部反射 TRT 与高阶残余项。几何是固定参考相机的三角形薄带，尚无原生曲线求交或动力学。

| 强日光直发 | 强日光波浪发 |
|---|---|
| ![强日光直发](../img/path-tracing/yuksel-straight-sunlit.png) | ![强日光波浪发](../img/path-tracing/yuksel-wavy-sunlit.png) |

[直发未降噪图](../img/path-tracing/yuksel-straight-sunlit-raw.png) · [波浪发未降噪图](../img/path-tracing/yuksel-wavy-sunlit-raw.png)

| 设置 | 本次取值 |
|---|---|
| 渲染 | Metal GPU PT，512×512，1024 spp，12 次反弹，seed 1 |
| 展示 | exposure 0.6，OIDN 2.5.1 CPU 离线降噪 |
| 天空 | 实时大气捕获的 512×256 线性 HDR，太阳高度 10° |
| 照明变化 | 天空和有限太阳同步旋转 160°，源照明同步乘以 10 |
| 太阳 | 角半径约 0.28648°，透射后 RGB irradiance `[21.39672,13.78802,6.65373]` |
| 毛发 | `beta_m=0.12`，`beta_n=0.2`，IOR 1.55，cuticle tilt 2° |
| RGB 吸收 | `[0.2,0.45,0.8]` |

天空不含太阳盘，有限太阳单独参与路径积分。暖色高光来自实际纤维散射与太阳照明；本次同时调整照明和粗糙度，不能用这组图单独衡量灯光变化。头模没有皮肤 BSSRDF 或源纹理。OIDN 可能平滑细发丝，因此验收使用未降噪线性 HDR。

[验收 JSON](../img/path-tracing/yuksel-hair-sunlit-validation.json)保存输入、输出与程序字节的 SHA256。[照明、渲染和降噪记录](../img/diagnostics/materials/hair-sunlit/)包含源天空 HDR、太阳参数及运行日志。两个正式场景均无非有限样本。

128×128、256 spp、深度 12 的波浪发检查中，Metal 与 Vulkan 原始 HDR 完全一致。相同相机、几何、材质及天空下，仅关闭有限太阳作为对照，发丝掩码内开启太阳后的平均线性亮度为关闭时的 **6.14 倍**。掩码取首个表面 albedo 的最小 RGB 分量大于 0.99；该对照衡量太阳总照明贡献，包含 R／TT／TRT，不是独立的 R 分量输出。

渲染使用开发工作区中的独立程序与成套 shader；记录冻结了实际执行文件及场景输入。构建期间共享源码曾变化，[源码审计](../img/diagnostics/materials/hair-sunlit/runtime-source-audit.json)保留了这一信息，构建前源码快照不代表已确认的完整编译源版本。本次提交保存展示与验收产物。
