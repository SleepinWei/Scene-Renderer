# 经典场景与资源来源

这些场景用于演示本项目的原生 Metal 实时渲染。README 中的截图由本项目输出，不是上游示例图。Bunny 和 Helmet 的模型与纹理随仓库提供；Sponza 和 San Miguel 通过下载脚本获取，无需原始 `asset/` 资源包。

## Stanford Bunny

- 来源：[Stanford 3D Scanning Repository](https://graphics.stanford.edu/data/3Dscanrep/)。
- 数据提供方：Stanford University Computer Graphics Laboratory。
- 原文件：`bunny/reconstruction/bun_zipper.ply`，取自官方 `bunny.tar.gz`；原网格有 35,947 个顶点、69,451 个三角形。
- 本项目保留原始 PLY 文件，导入时居中、缩放，并生成用于常量材质的平面 UV。演示分别使用白色非金属、金色金属和蓝色非金属材质。
- 使用条件：上游允许研究使用和免费再分发，并要求注明来源；商业使用需要 Stanford 的许可。详细条件见上游页面。

## Damaged Helmet

- 来源：[Khronos glTF Sample Assets / DamagedHelmet](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/DamagedHelmet)。
- 原始模型：© 2016 **theblueturtle_**，采用 [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/)，包含署名和非商业使用要求。
- 重建及 glTF 转换：© 2018 **ctxwing**，采用 [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)。两项归属和许可均按[上游说明](https://github.com/KhronosGroup/glTF-Sample-Assets/blob/main/Models/DamagedHelmet/README.md)保留。
- 本项目未修改模型或纹理文件；运行时调整节点朝向、位置和尺度。实时示例使用底色、金属度／粗糙度、法线和 AO 纹理。自发光纹理随原资源保留，目前没有用于头盔着色。
- 上游修订号和每个文件的 SHA-256 保存在 [assets/manifest.json](assets/manifest.json)。

## Cornell Box 风格场景

- 灵感来源：[Cornell University Program of Computer Graphics 的 Cornell Box](https://bowers.cornell.edu/computer-graphics)。
- 房间、红绿侧墙、两个旋转箱体和顶灯面板由本项目代码自行生成，没有复制上游网格、纹理或测量数据。
- 使用点光源、弱前方补光、立方体阴影、SSAO、RSM 近似间接光照、PBR 与 HDR；顶灯面板仅显示自发光外观，照明由点光源提供。
- 这是经典布局的实时演示，不是原始 Cornell Box 的测量基准，也不提供面积光源或完整路径追踪光传输。

## Sponza（Crytek）

- 来源：[McGuire Computer Graphics Archive](https://casual-effects.com/g3d/data10/index.html)，[Sponza 资源信息](https://casual-effects.com/g3d/data10/common/model/crytek_sponza/info.js)。
- 模型与纹理：© 2010 Frank Meinl / Crytek，上游标注 [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/)。
- 选择 `sponza.obj`，不选择独立的 banner 网格。导入后为 25 个网格、262,267 个三角形。
- 保留上游文件，运行时居中并将整体高度缩放为 12 个场景单位；读取 MTL 底色、法线、透明遮罩及高度纹理，将传统高光参数近似转换为粗糙度，使用非金属 PBR 材质。灯光、相机和曝光由本项目设置。

## San Miguel

- 来源：[McGuire Computer Graphics Archive](https://casual-effects.com/g3d/data10/index.html)，[San Miguel 资源信息](https://casual-effects.com/g3d/data10/research/model/San_Miguel/info.js)。
- 建模与版权：**Guillermo M. Leal Llaguno / Evolucien Visual**；2017 版改进由 **Morgan McGuire、Guedis Cardenas、Michael Mara（Williams College）及 Nicholas Hull（NVIDIA）**完成，获得原作者许可。
- 采用压缩包中的 `san-miguel-low-poly.obj`；导入并三角化后为 281 个网格、5,617,451 个三角形。原说明将版本标记为 San Miguel 2.1。
- 归档网页的许可字段标注 CC BY 3.0，但压缩包内 `license.txt` 明确写明**注明来源的研究与教育使用**。本项目保留该说明，并按其使用条件提供演示，不据网页字段推断额外商业授权。原始说明见 [licenses/san-miguel.txt](licenses/san-miguel.txt)。
- 保留原始模型与纹理，运行时居中、缩放和材质转换；合并叶片底色与透明遮罩，支持双面绘制和一致的阴影裁切，高度图近似转换为法线。原 MTL 中已有贴图映射等问题，玻璃和水没有真实折射。

## 大型场景的获取与复现

```sh
python3 tools/fetch_gi_assets.py                  # 两个场景
python3 tools/fetch_gi_assets.py --scene sponza   # 仅 Sponza
python3 tools/fetch_gi_assets.py --scene san-miguel
```

下载源与压缩包 SHA-256 固定在 [gi-assets.json](gi-assets.json)。缓存位于 `samples/downloads/`，运行文件位于 `samples/assets/gi/`，两者均被 Git 忽略。脚本校验归档后，仅解压选定 OBJ、材质、图像和原始说明，省略高面数网格与建模文件。San Miguel 压缩包约 511 MiB，其实时版本 OBJ 仍约 598 MiB；下载、解压和首次导入需要一定时间。删除缓存后可重新获取，校验失败时脚本会停止。

## 运行与重新生成截图

在项目根目录执行，先完成 Metal 构建：

```sh
./build/Scene-Renderer --classic cornell
./build/Scene-Renderer --classic bunny
./build/Scene-Renderer --classic helmet
./build/Scene-Renderer --classic sponza
./build/Scene-Renderer --classic san-miguel
```

生成 960 × 720 离屏截图，输出目录会自动创建：

```sh
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer --render-gallery img/metal core
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer --render-gallery img/metal gi
```

`core` 生成三个基础场景，`gi` 生成两个大型场景及 RSM 开关对照。不传选择项时生成基础场景，并在两个 GI 模型都存在时追加 GI 场景；也可传入单个场景名。`*-direct.png` 关闭 RSM，但仍含天空环境光和 SSAO；相机、曝光及其他光照保持一致。RSM 是一次间接反弹的近似，不是完整 GI 基准解。

Bunny 与 Helmet 资源丢失时可执行：

```sh
python3 tools/fetch_classic_assets.py
```

基础下载脚本仅获取 Stanford 与 Khronos 官方资源，使用清单中固定的 Khronos 修订号；首次下载会记录当时的修订号。截图可以在不同硬件或工具链下略有差异。
