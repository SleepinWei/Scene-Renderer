# Mountain Lake：山湖地形资源与接入

2026-10-04 接入 **ill_drakon** 的 [Mountain Lake](https://sketchfab.com/3d-models/mountain-lake-3043ead27ac74144950e634197a1490b)，使用新 RHI 的地形与水体渲染。资源采用 [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)，完整上游署名保存在 [许可文件](../samples/licenses/mountain-lake.txt)。效果图由本项目生成，不使用作者预览或生图作为引擎截图。

## 下载与转换

Sketchfab 官方下载需要登录。打开资源页的 **Download 3D Model**，分别获取 **Original format .fbx** 和 **Converted format .gltf** 两个 ZIP。前者下载文件名为 `mountain-lake.zip`，后者为 `mountain_lake.zip`；将它们复制为：

```text
samples/downloads/mountain-lake/original.zip
samples/downloads/mountain-lake/gltf.zip
```

原始包包含嵌套的 `source/wm_00.zip`（FBX 与 TGA）以及 `textures/wm_meters_TXTR.png`。glTF 包包含 `scene.gltf`、`scene.bin`、转换后的颜色图和上游 `license.txt`。转换脚本直接读取 ZIP，不运行资源中的代码，也不保存登录信息。归档 SHA-256 固定在 [资源清单](../samples/mountain-lake.json)，不匹配时停止。

在仓库根目录运行，需要 Python 3.11+、NumPy 与 Pillow：

```sh
python3 -m pip install numpy Pillow
python3 tools/prepare_mountain_lake.py
./build/Scene-Renderer --classic mountain-lake
./build/Scene-Renderer --render-gallery img/metal mountain-lake
```

也可通过 `--original`、`--gltf` 指定下载文件；`--output` 改变转换输出目录。内置场景默认读取 `samples/assets/terrain/mountain-lake/scene.json`，因此直接运行示例时保留默认输出路径。

## 几何、纹理与水体

- 原始场景包含 **2,098,112 个三角形**，其中地形为 2,097,152 个，水体盒为 960 个。地形是 **1025×1025** 的规则网格，glTF 为适配索引宽度将它拆成多个 primitive。
- 转换器校验所有网格样本、重复边界顶点的高度一致性与平面 UV，再重建 float32 高度场。采用右手系 `engine(X,Y,Z) = source(X,Z,-Y)`，保持山体与湖盆，不从颜色亮度猜测高度。
- 原始横向坐标是 `[-4000,4000]²`，高度约 `89.321–2109.844`；演示按米制解释为 **8×8 km**，保留源比例。它是程序生成地形，没有地理定位或实测尺度保证。相机远裁剪面为 16000，漫游速度为 150 场景单位/秒。
- 作者提供一张 **1025×1025** 地表颜色图，没有独立的法线、粗糙度、AO 或金属度贴图。使用原始 PNG，按转换后 glTF 的 UV 约定翻转图片行。几何法线由高度场差分计算；材质使用非金属、粗糙度 0.9、默认平面细节法线和 AO 1。
- 高度、材质都生成带 mip 和 apron 的 VT pack。由于现有 pack 要求 2 的幂尺寸，存储 extent 为 **2048**；这是重采样，**没有增加原始 1025 的细节精度**。高度 tiles 约 96.31 MiB、五层材质 tiles 约 120.39 MiB，运行时按现有固定物理缓存分页。
- 水位取源水体盒上表面 **432.236877**。原来的静态盒替换为同范围的 FFT 水面，主频谱 512²／512 m 周期、短波频谱 256²、网格 1025²，低波幅、不生成白沫。生图水域 mask 限制覆盖范围，深度遮挡补充湖岸约束，保留现有折射、吸收及近似单次散射。

湖面目前沿用深水 FFT 算法，没有湖泊边界条件、浅水波传播或独立的岸线流体模拟。反射读取天空，不包含山体的屏幕空间／平面反射；原始色图也没有分层近景 PBR 细节。不要将这个示例解释为完整湖泊物理模拟。后续近景清晰度需要额外的岩石、草地等细节材质，升采样不能替代这些数据。

新增可配置的 GPU 草丛，按水域、实际水位、坡度、海拔、距离和视锥限制放置；地面视角用 `--classic mountain-lake-ground` 打开。具体修复、mask 提示和验证见 [植被与 FFT 湖面说明](vegetation-and-lake-water.md)。

下载归档和转换生成的地形运行资源均被 Git 忽略；水域 mask 随仓库提供；仓库提供转换脚本、固定清单、许可证和实际截图。后续可从官方下载归档重新生成。

## 验证

```sh
python3 tests/test_prepare_mountain_lake.py
python3 tests/test_bake_terrain_vt.py
```

转换测试验证拆分及乱序网格重建、缺失样本拒绝、重叠高度冲突拒绝，以及镜像 UV 拒绝。离线 VT 测试验证过滤、边框、非方形重采样和非法输入。

![新 RHI／Metal 山湖场景](../img/metal/mountain-lake.png)
