# 湖岸沙滩与草地距离密度

2026-10-04：Mountain Lake 使用新 RHI 增加沙滩 PBR 材质，并将草从固定地形格采样改为世界空间距离密度。截图由 Metal 实际渲染生成。

## 原来的问题

草的数量取决于地形格子的尺寸和 `samplesPerCell`。在 8 km 地形上，即使最细一级每格也约 6.25 m，固定 8×8 采样仍然只有约 1.64 丛/平方米；提高地形 LOD 也不能直接表达近景草地密度。实例预算满时统一抽样，远处草会占用近景需要的容量。原地表仅有大范围色图，湖岸没有独立沙地材质。

## 草的分布

`Grass` 组件默认启用世界空间间距；Mountain Lake 使用以下参数：

| 参数 | 值 | 行为 |
| --- | --- | --- |
| `nearSpacing` | 0.15 m | 10 m 内保持密集草丛 |
| `denseRadius` | 10 m | 近景密度区半径 |
| `farSpacing` | 3.5 m | 到 70 m 时变为稀疏分布 |
| `sparseRadius` | 70 m | 用 smoothstep 插值近远间距 |
| `density` | 0.95 | 近区目标约 42 丛/m²，每丛四片弯叶，约 169 片/m² |
| `fadeStart` / `distance` | 120 / 180 m | 远景逐渐淡出并最终剔除 |
| `capacity` | 65536 | 有界 GPU 实例缓冲 |

每个地形格根据世界空间宽度和相机距离分配采样格，逐候选按实际间距修正概率。复用原有共享角点与三角形附着，保持根部贴合 morph 后的地形。计数阶段分别统计近区和其他候选，生成阶段优先保留近区，剩余容量才分配给远区；近区本身超过容量时仍进行有界抽样。水域、海拔、坡度和视锥过滤继续生效。

间距单位沿用场景单位，Mountain Lake 按米解释。低层 `VegetationSettings` 默认 `nearSpacing=0` 保留固定格采样，便于旧的直接 RHI 调用；`Grass` 组件设置为 0.15。单格最多 256×256 候选，极大的粗格可能达不到目标密度；超出 `maxLod` 的格不生成草。提高容量不能绕过这些约束。LOD／采样格变化仍可能改变根的位置，草继续使用 TSAA reactive 标记；尚未实现稳定的植被 clipmap 或草叶前帧速度。

## 沙滩资源与混合

采用 [Poly Haven Aerial Beach 01](https://polyhaven.com/a/aerial_beach_01)，作者 Rob Tuytel，CC0。仓库提供由 1K 原图生成的底色、OpenGL 法线及 ARM（AO、roughness、metallic=0）三张 1024×2048 mip atlas，世界坐标平铺周期 30 m。出处和处理说明见 [许可](../samples/licenses/aerial-beach-01.txt)，原始 URL、尺寸、SHA-256 见 [清单](../samples/beach-material.json)。

底色 mip 在线性光下平均；法线 mip 重新归一化。着色器根据像素导数选择 mip，层内手动双线性 wrap，层间插值，避免 atlas 层边缘渗色。

转换器从真实高度场生成岸线距离数据：距水体 40 m 内保留完整接近度，40–100 m 平滑衰减；再结合相对水位的 24 m 高度带和坡度混合沙地。水边湿沙更暗，粗糙度更低。草使用同一覆盖函数按权重排除，形成沙地到草地的过渡。深水下不继续铺满沙层。高度场派生的岸线图与既有生图水域 mask 分别用于材质、植被过滤和水面裁剪。

`ShorelineSettings` 经主线程组件配置、不可变快照传到渲染线程；图片在资产 worker 解码，经现有 GPU image cache 上传，纳入地形上传统计。沿用现有材质描述符，VT 底色与页表继续使用原绑定；其余四槽保存沙地法线、底色、ARM、岸线数据。此预设的非沙地部分采用 VT 底色、几何法线、常量粗糙度和 AO，适用于上游只有色图的 Mountain Lake；尚未支持同时混合完整的岩石／草地五层 PBR VT。前向、延迟、透明及 RSM 共用采样代码。没有改变湖盆几何，也没有增加沙粒位移或岸线流体模拟。

## 使用与重建

先按 [山湖资源说明](mountain-lake.md) 下载两个官方地形归档，再运行：

```sh
# 仓库已提供处理后的沙滩贴图；下面这行仅在需要重建时执行
python3 tools/prepare_beach_material.py --download
python3 tools/prepare_mountain_lake.py
./build/Scene-Renderer --classic mountain-lake-ground
./build/Scene-Renderer --classic mountain-lake-beach
./build/Scene-Renderer --render-gallery img/metal mountain-lake-ground
./build/Scene-Renderer --render-gallery img/metal mountain-lake-beach
```

Python 工具需要 NumPy、Pillow。转换后的 `scene.json` 保存 `shoreline` 配置；支持 `seaLevel`、`heightRange`、`wetBelow`、`wetAbove`、`textureLength`、`normalStrength`、`slopeMin`、`slopeMax` 和四个 `textures` 路径。

## 验证

在独立 HEAD 副本叠加本次改动构建，以保留同目录的其他 PT 工作。Apple M4 / Metal：

- 离线测试验证底色 mip 的线性光能量、法线长度、岸线距离的局部性和单调衰减；山湖网格转换测试通过。
- GPU 统计测试：2 m 内约 **44.48 丛/m²**，6–8 m 环带约 **0.432 丛/m²**（测试密度为 1、稀疏间距为 1.5 m）；数量按水平投影面积计算，关闭视锥过滤，测试地形近似平面。
- GPU 还验证容量不足时近区优先、沙滩不生成草，以及湿沙底色、粗糙度、AO 和法线长度。
- Metal 磁盘管线缓存持久化检查在地形测试前失败；临时设置 `SCENERENDERER_DISABLE_PIPELINE_DISK_CACHE=1` 后，在 API／Shader Validation 下完整 CTest **15/15** 通过，包括 GPU 自检和双线程／单线程编辑器。此结果不包含磁盘缓存持久化验收。

```sh
python3 tests/test_prepare_beach_material.py
python3 tests/test_prepare_mountain_lake.py
SCENERENDERER_DISABLE_PIPELINE_DISK_CACHE=1 ./build/Scene-Renderer --rhi-self-test
```

![湖岸密集草丛](../img/metal/mountain-lake-ground.png)
![湖岸沙滩 PBR 材质](../img/metal/mountain-lake-beach.png)
