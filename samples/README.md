# 经典场景与资源来源

这些场景用于演示本项目的原生 Metal 实时渲染。README 中的截图由本项目输出，不是上游示例图。Bunny 和 Helmet 的模型与纹理随仓库提供；Sponza、San Miguel 及新增扫描模型／Sibenik 通过下载脚本获取，无需原始 `asset/` 资源包。

## 新增实时测试场景

`python3 tools/fetch_benchmark_assets.py` 获取 Stanford Dragon、Happy Buddha、Armadillo 与 Sibenik Cathedral。前三者使用 Stanford 原格式重建 PLY；Buddha 和 Armadillo 来自 Alec Jacobson 的固定修订镜像，原始 Stanford 来源与使用条件保留。Sibenik 为 Marko Dabrovic 建模，Kenzie Lamar／Vicarious Visions 修复洞，Morgan McGuire 制作高清贴图；上游标注 CC BY-NC。说明文件随解压保留，大型模型不提交 Git。

运行 `--classic dragon`、`buddha`、`armadillo`、`sibenik`，或使用 `--render-gallery img/metal benchmarks`。完整来源与校验值见 [benchmark-assets.json](benchmark-assets.json)，转换、场景规模和验收见 [经典测试场景说明](../docs/classic-benchmarks.md)。Dragon 实时演示使用金色金属，与下方离线玻璃 PT 的材质设置不同。

## Blender 离线 PT 测试场景

`python3 tools/prepare_blender_scenes.py --blender /path/to/blender` 获取官方 Classroom（Christophe Seux，CC0）和 Barcelona Pavilion（eMirage / Hamza Cheggour，CC-BY），保留链接资源，导出冻结的相机、共享几何、材质颜色图集、灯光及线性 world。源归档与 SHA256 见 [blender-assets.json](blender-assets.json)，署名及转换变更见 [资源声明](licenses/blender-scenes.txt)。原始归档和导出包不随 Git 提交。

使用 `--path-trace`／`--path-trace-gpu blender-classroom` 或 `blender-barcelona`。默认 Barcelona 保留 512／20,622 粒子实例；`--particle-limit -1` 可导出全部植被，CPU／Metal／Vulkan 的共享 BLAS/TLAS 已支持完整包。Classroom 集合实例全部保留。当前转换未完整实现 Cycles 材质闭包和 compositor，具体限制、实际图片与复现见 [Blender PT 说明](../docs/blender-path-tracing.md)。

导出 revision 5 增加可选 `tangents.bin` corner MikkTSpace 帧与独立薄叶透射图。现有 revision 4 包仍可读取；使用上方导出命令重新生成才能获得新材质，README 的完整 Barcelona 图使用 revision 5。小型无外部资源材质对照由 `tests/PT/BlenderAppearanceFixture.py` 生成。

## Stanford Bunny

- 来源：[Stanford 3D Scanning Repository](https://graphics.stanford.edu/data/3Dscanrep/)。
- 数据提供方：Stanford University Computer Graphics Laboratory。
- 原文件：`bunny/reconstruction/bun_zipper.ply`，取自官方 `bunny.tar.gz`；原网格有 35,947 个顶点、69,451 个三角形。
- 本项目保留原始 PLY 文件，导入时居中、缩放，并生成用于常量材质的平面 UV。演示分别使用白色非金属、金色金属和蓝色非金属材质。
- 使用条件：上游允许研究使用和免费再分发，并要求注明来源；商业使用需要 Stanford 的许可。详细条件见上游页面。

## Stanford Dragon

- 数据提供方：[Stanford University Computer Graphics Laboratory](https://graphics.stanford.edu/data/3Dscanrep/)。官方完整重建 PLY 有 437,645 个顶点、871,414 个三角形。
- `python3 tools/fetch_dragon.py` 下载固定 SHA-256 的官方重建归档；模型保存在忽略的 `samples/assets/pt/dragon/`，不随仓库提交。
- 运行时居中、缩放、生成平滑法线及覆盖为 IOR 1.5 玻璃，用本项目 CPU BDPT 输出透明和焦散渲染。保留原扫描的小孔，未做闭合修复；不是严格闭合玻璃基准。
- 上游允许研究使用和免费再分发，要求注明来源；商业使用需要 Stanford 的许可。完整来源和校验值见 [资源声明](licenses/stanford-dragon.txt)，复现见 [Dragon／程序化 PT](../docs/path-tracing-procedural.md)。

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

### Mountain Lake：山地与湖泊

- 作者：[ill_drakon](https://sketchfab.com/ill_drakon)，来源：[Mountain Lake](https://sketchfab.com/3d-models/mountain-lake-3043ead27ac74144950e634197a1490b)，采用 [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)。完整署名见 [licenses/mountain-lake.txt](licenses/mountain-lake.txt)。
- 从官方页面登录下载原始 FBX 和转换 glTF 归档，放入 `samples/downloads/mountain-lake/` 后执行 `python3 tools/prepare_mountain_lake.py`。文件名、校验值和操作见 [山湖地形说明](../docs/mountain-lake.md) 与 [资源清单](mountain-lake.json)。
- 保留原生 1025×1025 高度和地表色图，转换坐标、UV，并烘焙高度／材质 VT。内置 `--classic mountain-lake` 使用源水位和低风速 FFT 湖面。原始坐标范围按米制解释为 8×8 km，属于演示尺度。
- 下载与转换输出不随仓库提交；截图、清单和转换工具随仓库提供。转换结果与作者原始模型的修改点见上面的许可文件和说明。

### Sponza 与 San Miguel

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

`core` 生成三个基础场景，`gi` 生成两个大型场景及 RSM 开关对照。不传选择项时生成基础场景，并在两个 GI 模型都存在时追加 GI 场景；也可传入单个场景名。`*-direct.png` 关闭 RSM，但仍含天空环境光和 SSAO；相机、曝光及其他光照保持一致。两个 GI 场景的 RSM 使用太阳与天空，不再添加额外聚光灯；同时输出 `*-indirect.png`、`*-sun-indirect.png`、`*-sky-indirect.png`。RSM 是一次间接反弹的近似，不是完整 GI 基准解，详见[实现与验证](../docs/rsm.md)。

Bunny 与 Helmet 资源丢失时可执行：

```sh
python3 tools/fetch_classic_assets.py
```

基础下载脚本仅获取 Stanford 与 Khronos 官方资源，使用清单中固定的 Khronos 修订号；首次下载会记录当时的修订号。截图可以在不同硬件或工具链下略有差异。
