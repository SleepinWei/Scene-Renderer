# 纱线级针织数据与毛衣材质

2026-10-09：接入 YarnSim 的公开蜂窝针织控制点，增加方向性纱线反光，在夕阳下渲染毛衣正面和三股纱线近景。CPU、Metal、Vulkan 共用材质公式。

## 专门用于编织的模型

普通服装网格提供轮廓、褶皱和 UV；针织模型还要表达针目连接、纱线交叠和接触。这些项目更符合后者：

| 项目 | 提供的内容 | 本次采用方式 |
|---|---|---|
| [Stitch Meshes，SIGGRAPH 2012](https://research.cs.cornell.edu/stitchmeshes/) | 从针目网格建立纱线，松弛后生成毛衣、手套等服装细节 | 完整服装拓扑的后续方向；本次未移植求解器或取得论文中的整件毛衣资产 |
| [Interactive Design of Periodic Yarn-Level Cloth Patterns，2018](https://graphics.stanford.edu/projects/yarnsim/) | 公开针织／机织花型和模拟纱线控制点数据 | 导入 `slip_stitch_honeycomb_1.225.txt`，8 个控制点块，周期位移为 X=15、Y=20 |
| [A Practical Ply-Based Appearance Modeling for Knitted Fabrics，2021](https://arxiv.org/abs/2105.02475) | 用 ply 结构与纤维方向、法线描述针织外观，包含反射和透射验证 | 多尺度材质的参考方向；当前闭包没有复现该论文的纤维输运模型 |

原数据来自 Jonathan Leaf、Rundong Wu、Eston Schweickart、Doug L. James、Steve Marschner。原站保留作者版权，禁止未经许可重发原作品。本仓库仅保存导入工具、署名、hash、自产渲染和诊断；下载包及其生成的纱线几何放在被忽略的 `samples/assets/pt/`、`build/` 中，没有为原数据声明 CC 或 MIT 许可。

## 连接与几何

导出数据分为多个控制点块，其中边界连接包含一个或两个重叠控制点。**先平移邻域、连接控制点链，再求均匀三次 B-spline**；如果先分别求每个块的曲线，会丢掉连接处的 spline span，产生断裂针目。导入器每段取 8 次细分，裁剪到目标矩形并移除重复段；检查内部没有开放断端，开放边缘只允许落在裁剪边界。均匀三次求值是本导入器的选择，尚未与作者 Mitsuba 参考图逐像素对照。

近景样片重复 5×3 个周期单元，纱线连接后生成三股绕线管和 16,000 根短绒毛，**没有不透明背板**，针目孔隙和投影来自真实几何。毛衣保留原程序化衣身、袖子、领口和下摆，正面替换为 20×14 个蜂窝周期单元和 32,000 根细绒毛。曲线映射到弯曲衣身后没有重新机械松弛；袖子与接缝仍采用程序化模型／UV 针目近似。

纱线暂用三角形管，绒毛用参考相机 ribbon。近景和毛衣分别约 110 万、421 万生成三角形，导入时会剔除退化面。真实曲线求交、纤维 LOD、完整服装 stitch mesh 和碰撞松弛仍是后续工作。

## 材质输入

新增 `bsdf_model: "yarn"`，沿用 cloth=4 的闭包编号。适用于有真实纱线几何和切线的模型；`knit` 的 UV 平针近似也可以设置同一个反光参数。

```json
{
  "bsdf_model": "yarn",
  "base_color": [0.78, 0.67, 0.52],
  "metallic": 0,
  "roughness": 0.65,
  "sheen_weight": 0.18,
  "sheen_roughness": 0.8,
  "sheen_color": [0.95, 0.88, 0.75],
  "cloth_transmission": 0.03,
  "yarn_specular_weight": 0.25,
  "two_sided": true
}
```

`yarn_specular_weight` 范围为 [0,1]；`yarn` 预设默认 0.5，旧 `cloth`／`knit` 默认为 0，保持原有外观。它占用 `pathTracingSheen.w` 原保留分量，**PackedMaterial 保持 272 B**；字段参与场景 identity，修改后会重置累积。只允许 cloth／knit／yarn 使用，普通 PBR 和 hair 上的非零值会被拒绝。CPU 与 shader 必须一起重建。

反光使用沿纱线切线设定窄轴的各向异性 GGX、Smith masking 和固定 IOR=1.55 的无色介质 Fresnel。两个粗糙度轴分别为 `max(0.08, 0.3*r²)`、`max(0.16, r²)`；真实几何使用输入切线，UV 针织使用 `fiberKnit` 的方向。它是纱线束的表面近似，未实现 ply 内部多次散射、独立纤维法线贴图或纱线体积输运。

设 sheen 份额为 w、纱线反光份额为 y、薄片透射比例为 t，则反射为：

`(1-w) * [(1-y)*(1-t)*color*OrenNayar/π + y*YarnGGX] + w*sheenColor*Sheen/π`

透射为 `(1-w)*(1-y)*t*color/π`。各项按份额分配能量；反光不会直接叠加到全强度漫反射上。透射采样概率同步改为 `(1-w)*(1-y)*t`，包括 y=1、t=1 的边界；反射暂用余弦 proposal，参与现有 NEE／MIS，较尖的反光还可以通过后续 GGX VNDF proposal 提高效率。

## 夕阳渲染与复现

采用既有实时 `clouds-sunset` 捕获的 HDR，5° 有限太阳，天空／太阳一起旋转 80°、亮度倍率 1.2、曝光 0.7。HDR 中的云参与背景和环境光；投影云阴影场尚未接入。展示图使用 **Metal PT、768×768、1024 spp、12 次反弹、seed 1、OIDN 2.5.1 CPU 降噪**。原始图保留用于检查细绒毛和针目细节。

| 毛衣正面蜂窝针织 | 三股纱线近景 |
|---|---|
| ![夕阳蜂窝针织毛衣](../img/path-tracing/knitted-sweater-sunset.png) | ![夕阳三股纱线蜂窝针织](../img/path-tracing/knitted-honeycomb-sunset.png) |

[毛衣未降噪图](../img/path-tracing/knitted-sweater-sunset-raw.png) · [近景未降噪图](../img/path-tracing/knitted-honeycomb-sunset-raw.png) · [输入、输出与后端验收记录](../img/path-tracing/knitted-yarn-validation.json)

```sh
mkdir -p samples/assets/pt/knit-patterns
curl -L --fail --output samples/assets/pt/knit-patterns/yarnsim-dataset.zip \
  https://graphics.stanford.edu/projects/yarnsim/assets/supplementalmaterial/dataset.zip
# 先按 path-tracing-grooms-sunlight.md 生成既有 refined sunset sweater 包。
PYTHONDONTWRITEBYTECODE=1 python3 tools/create_pt_knitted.py
cmake -S . -B build/pt-knit/runtime -DCMAKE_BUILD_TYPE=Release \
  -DSCENERENDERER_METAL=ON -DSCENERENDERER_VULKAN_PROTOTYPE=ON \
  -DSCENERENDERER_OIDN=OFF
# Vulkan 需要已配置 SDK；本机额外指定已有 MoltenVK 的 include/library 路径。
cmake --build build/pt-knit/runtime \
  --target pt-package-render pt-cpu-tests pt-scene-package-tests -j 6
ctest --test-dir build/pt-knit/runtime \
  -R '^pt-(cpu|knit-patterns|scene-package|groom)$' --output-on-failure
python3 tools/render_pt_knitted.py --preview
# 默认完整渲染包含 Metal/Vulkan/CPU 的 128²、64 spp 原始 HDR 对照。
# --oidn-renderer 指向启用了 OIDN 的主程序；新私有 runtime 本身不含 OIDN。
python3 tools/render_pt_knitted.py --oidn-renderer ./build/pt/Scene-Renderer
```

生成器固定 seed；重复生成的 geometry、tangents、groom、scene、environment 已检查 hash 完全一致。CPU 单元测试覆盖互易性、白炉能量积分、反光方向旋转、极限权重采样、场景 identity 和打包参数；导入器测试覆盖 B-spline 精度、周期拼接、重复段、边界裁剪和确定性。

Apple M4 上，4 项 CTest 全部通过（其中针织导入器包含 5 个测试）。128×128、64 spp、12 次反弹的**原始线性 HDR** 对照如下；渲染时间为 768×768、1024 spp 的 GPU 采样时间，不包含场景准备、首次驱动编译或 OIDN。

| 场景 | 有效三角形 | Metal／Vulkan | Metal／CPU 相对 RGB L1 | 高采样 Metal 时间 | 非有限样本 |
|---|---:|---|---:|---:|---:|
| 三股纱线近景 | 1,103,874 | 逐像素相同 | 0.0153% | 113.20 s | 0 |
| 毛衣蜂窝正面 | 4,206,234 | 逐像素相同 | 0.00440% | 86.89 s | 0 |

原始后端对照验证数值一致性，尚不能替代独立 Mitsuba／Blender GT。OIDN 可能平滑细绒毛；更高物理真实性的下一步是 ply/fiber appearance、完整针目拓扑与曲线加速结构。
