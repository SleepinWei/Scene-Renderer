# 新增经典测试场景

本批次在新 RHI 实时入口中加入四个可运行场景，使用原始模型、固定相机及实际 Metal 截图。Dragon 此前已有独立离线 PT 演示，本次增加的是实时 PBR 入口，不复用玻璃／玉石 PT 材质。

| 入口 | 导入规模 | 用途 |
| --- | --- | --- |
| `dragon` | 1 网格，871,414 三角形 | 金色金属、曲面高光、复杂轮廓、阴影 |
| `buddha` | 1 网格，1,087,716 三角形 | 浅色非金属、扫描曲面、接触遮蔽 |
| `armadillo` | 1 网格，345,944 三角形 | 粗糙金属、壳面细节、法线与阴影 |
| `sibenik` | 15 网格，75,284 三角形 | 中殿、拱顶、石材纹理、遮挡与 RSM 对照 |

## 获取和运行

```sh
python3 tools/fetch_benchmark_assets.py
# 也可仅下载一个：--scene dragon / buddha / armadillo / sibenik
./build/Scene-Renderer --classic dragon
./build/Scene-Renderer --classic buddha
./build/Scene-Renderer --classic armadillo
./build/Scene-Renderer --classic sibenik
./build/Scene-Renderer --render-gallery img/metal benchmarks
```

单个场景也可传给 `--render-gallery`。批量入口生成三个扫描模型截图，以及 Sibenik 的默认、RSM 关闭、纯间接光、太阳反弹和天空反弹图，共八张 960×720 PNG。每次捕获重置 TSAA 历史，累积 16 帧，时间固定为 8 s。前三个场景曝光 1.2、FOV 48°；教堂曝光 1.8、FOV 58°。相机和灯光在 `ClassicScenes.cpp` 中明确配置。

扫描模型保持原拓扑，运行时平移、等比缩放、调整朝向并覆盖为 PBR 常量材质；缺少原始彩色纹理，不把本项目材质当作扫描物的真实外观。Sibenik 保留上游纹理与 MTL，并沿用传统材质到 PBR 的近似转换。

缓存位于 `samples/downloads/`，运行资源位于忽略的 `samples/assets/benchmarks/`。脚本校验归档 SHA-256 和解压网格 SHA-256，只提取选定重建网格、材质、贴图与说明；原始扫描及其他 LOD 不进入运行目录。下载完成前写临时文件，校验成功才替换缓存；缓存损坏会在解压前拒绝。解压不接受绝对路径、`..` 或盘符。

## 来源与使用条件

- 三个扫描模型来自 [Stanford 3D Scanning Repository](https://graphics.stanford.edu/data/3Dscanrep/)，署名 Stanford University Computer Graphics Laboratory。上游允许研究使用和免费再分发，商业用途需要许可。
- Dragon 使用官方 `dragon_recon.tar.gz`；Buddha 和 Armadillo 使用 [Alec Jacobson 的原格式镜像](https://github.com/alecjacobson/common-3d-test-models)，固定修订 `8a4f8642acaf43f9cd7b67858a1502e1055ef202`。清单保留官方原始地址，不用简化 OBJ 替代 PLY。
- Sibenik 来自 [McGuire Computer Graphics Archive](https://casual-effects.com/data/)。建模 Marko Dabrovic，洞修复 Kenzie Lamar／Vicarious Visions，高清纹理与 bump Morgan McGuire；上游 [资源信息](https://casual-effects.com/g3d/data10/research/model/sibenik/info.js) 标注 CC BY-NC，未指定版本。原 `copyright.txt` 保留在运行目录。

完整来源、固定 URL 和哈希见 [下载清单](../samples/benchmark-assets.json)，署名见 [资源说明](../samples/README.md)。

## 导入修复和验收

Sibenik 与扫描网格包含退化面或无法生成有效法线的顶点。此前对零向量直接 normalize 会得到 NaN，随后被网格数据边界拒绝。现在保存有效法线，仅对无效法线使用面积加权几何法线重建；完全退化、无有效邻面的顶点使用有限单位法线。最后重建切线基。有效法线的模型不分配重建临时数组，不修改源模型。

新增 OBJ 回归同时包含零法线、有效三角形和完全退化面，验证有效几何保留、法线有限且单位长度，并实际采用几何法线。Metal 开启 API／Shader Validation，完整 CTest **16/16** 通过；四个场景与八张画廊输出均检查 HDR 有限性。验证关闭磁盘 pipeline cache，构建基于 `6621418` 加本批次改动，隔离正在进行的 PT 工作。截图哈希、HDR 统计和场景规模见 [验收清单](../img/metal/benchmarks-validation.json)。

Sibenik 使用太阳／天空 RSM，另有内部点光补光；补光不进入当前太阳／天空 RSM。直接图仍包含天空环境光和 SSAO。RSM 是局部一次反弹近似，不能作为教堂完整 GI 参考解；本批次没有新增实时透射或路径追踪积分算法。Vulkan 共享加载和场景代码，本批次截图与场景运行验收使用 Metal。

Sibenik 另通过默认双线程原生编辑器 16 帧运行：分段网格上传剩余 0 项，GPU 发布拒绝和回退帧均为 0。此短运行用于加载及生命周期验证，不作为帧率基准。
