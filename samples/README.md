# 经典场景与资源来源

这些场景用于演示本项目的原生 Metal 实时渲染。README 中的截图由本项目输出，不是上游示例图。模型和纹理随仓库提供，无需原始 `asset/` 资源包。

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

## 运行与重新生成截图

在项目根目录执行，先完成 Metal 构建：

```sh
./build/Scene-Renderer --classic cornell
./build/Scene-Renderer --classic bunny
./build/Scene-Renderer --classic helmet
```

生成全部 960 × 720 离屏截图，输出目录会自动创建：

```sh
MTL_DEBUG_LAYER=1 MTL_SHADER_VALIDATION=1 ./build/Scene-Renderer --render-gallery img/metal
```

只生成一个场景：

```sh
./build/Scene-Renderer --render-gallery build/gallery helmet
```

如果资源丢失，可以重新下载：

```sh
python3 tools/fetch_classic_assets.py
```

下载脚本只获取 Stanford 与 Khronos 的官方资源。已有清单时会继续使用其中固定的 Khronos 修订号；首次下载时记录当时的修订号。截图可以在不同硬件或工具链下略有差异。
