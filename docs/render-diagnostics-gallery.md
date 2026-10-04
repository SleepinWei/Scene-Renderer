# VT、CSM 与 PCSS 效果捕获

本次更新将真实渲染效果和中间产物加入项目 README。2026-10-05 在 Apple M4 上以 Release／原生 Metal 捕获，使用新 RHI；没有使用生图模型或外部引擎渲染这些效果图。

## 入口与数据来源

```sh
cmake -S . -B build -DSCENERENDERER_RHI_BACKEND=Metal -DSCENERENDERER_LEGACY_METAL=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
./build/Scene-Renderer --render-gallery /tmp/scene-renderer-diagnostics diagnostics
python3 -m pip install numpy pillow
python3 tools/visualize_render_diagnostics.py /tmp/scene-renderer-diagnostics img/diagnostics
```

`diagnostics` 组包含 `terrain` 与新增的 `shadow-test`，均为程序几何，不需要下载模型。画面尺寸 960×720，固定时间 8 s，每张清空时域历史并运行 64 帧。CPU 源准备完成后使用异步快照的分页机制，与编辑器一样启用有界后台 IO；GPU 深度反馈使用实际渲染的抖动投影反变换，阴影视图预测使用未抖动的 CSM 投影。

原始 readback 只在显式诊断导出中同步读取，交互帧循环不会调用。`SceneAdapter.terrainVirtualTextures()` 返回只读缓存租约及 terrain model；`ForwardPbrRenderer.shadowParameters()` 返回渲染线程的参数值拷贝。导出代码位于 [GalleryDiagnostics.cpp](../src/renderer/rhi/GalleryDiagnostics.cpp)，排版与数据解码位于 [visualize_render_diagnostics.py](../tools/visualize_render_diagnostics.py)。

| 文件 | 实际数据来源及含义 |
| --- | --- |
| `terrain.png`、`terrain-wireframe.png` | 最终 LDR GPU 渲染输出，含地形、草和天空 |
| `terrain-vt-height-atlas-0.f32` | 高度 RGBA32Float 物理图集 |
| `terrain-vt-material-atlas-{0..4}.u8` | 底色、法线、金属度、粗糙度、AO 的 RGBA8 物理图集 |
| `terrain-vt-{height,material}-table.f32` | RGBA32Float 页表：槽位 x/y、mip、有效标志；尾部包含采样元数据 |
| `{scene}-positions.f32` | 同帧世界坐标及有效标志的 G-buffer |
| `{scene}-shadow.f32` | 实际 Depth32Float 阴影附件 |
| `{scene}-diagnostics.json` | 尺寸、相机 view、地形 model、级联分割／矩阵／rect、驻留和反馈计数 |
| `shadow-test{,-pcf,-pcss-wide}.png` | 默认 PCSS、PCF、放大方向光角半径 PCSS 的独立最终输出 |

浮点文件为本机 little-endian float32，像素行从上至下；尺寸与元数据一并导出。原始缓冲留在调用者指定的目录，避免将大批二进制 readback 提交到仓库。PNG、参数和数值摘要保存于 [img/diagnostics](../img/diagnostics)。

## VT 图应如何阅读

本场景使用 1024² 虚拟尺寸，64² 内容块，四边各 2 texel apron，物理 pitch 68；高度和材质分别使用 8×8 个缓存槽位，故物理图集均为 544²。含页表，高度约 4.52 MiB，五层材质约 5.65 MiB，合计约 10.18 MiB；这些值不含几何、反馈缓冲、草、阴影附件和完整 CPU 源。软件 VT 控制 GPU 驻留预算，本程序源仍保留完整 CPU 数组；磁盘 pack 可避免这一点。

![实际 GPU 物理缓存](../img/diagnostics/vt-cache.png)

图集由 readback 直接显示；高度做全场固定最小／最大值归一化，物理槽位边界加细线。空槽位显示深色。法线与粗糙度为该程序材质的常量，不能由这些常量图推断未接入 VT；地形几何法线仍由高度差分获得。金属度和 AO 也随其他三层导出，以便检查绑定。

![实际页表与首个祖先](../img/diagnostics/vt-residency.png)

页表按各 mip 原始网格解码。下半图在完整 UV 域固定请求 mip 0，自细到粗查找首个有效祖先。它不是 shader 输出 AOV，未执行边缘缺页的 smoothstep 覆盖与多层权重累积，也未采用材质屏幕导数选择请求 mip；最终渲染仍执行这些过滤。因高度使用 `(u,v)`、材质使用 `(u,1-v)`，两套细页分布上下翻转。

根页始终固定，缺失的细页可回退。截图记录的是第 64 帧快照，不是完全停止后台 IO 后的状态；可有 pending 页，驻留槽位、页地址和反馈样本数会随 GPU 进度变化。复现应验证结构、预算和回退逻辑，而非要求 PNG 或页地址逐字节一致。当前实际计数保存在 [capture-summary.json](../img/diagnostics/capture-summary.json)。

## CSM 与 PCSS 图应如何阅读

`--classic shadow-test` 可交互查看。近处三根立柱高 4／10／16 m，远处布置七根遮挡物；方向光、接收平面、相机、曝光固定。相机 far=500 m，阴影距离=300 m，五级远端约为 18.37／37.74／62.55／114.35／300 m。

![级联分区](../img/diagnostics/csm-cascades.png)

伪彩色从同帧 world-position G-buffer 变换至 camera-view 深度，再按实际分割选择 C0–C4。颜色在各级末段按 10% overlap 平滑插值，末级最后 10% 渐变为灰色，距离外也为灰色。无效背景为深色。它是 CPU 根据真实 GPU 数据作出的分类图，展示级联分配，不能据此断言每个接收点都落入有效 shadow rect，也不是 PCSS 的可见度缓冲。

![阴影深度附件](../img/diagnostics/csm-atlas.png)

图集 1792²，5 个 tile 在 3×3 网格中，每 tile 597²，剩余位置清除为 1。所有 tile 共用实际有效深度的全局灰度范围；白色也可能是范围内最远深度，需结合原始数据区分。彩框标出 rect，逐级图使用同一映射，不能用逐 tile 归一化的灰度跨级比较。

![软阴影对照](../img/diagnostics/pcss-comparison.png)

三列分别为 PCF、PCSS 角半径 0.00465 rad、PCSS 角半径 0.04 rad；第三列显式放大方向光发光体，仅用于展示半影变化。下排对三张原始画面取同一像素矩形并缩放，无局部锐化、重绘或人工修改阴影。`--crop LEFT TOP RIGHT BOTTOM` 可更改显示区域。

相机、灯光方向、材质与曝光保持不变；PCF／PCSS 自身会选用不同的投影 guard，以保护各自的最大过滤范围，因此不把整图差分等同于独立过滤核误差。数值摘要记录平面像素的 8-bit RGB 差分，用于确认对照实际改变输出，不以此度量物理准确度。当前截图只展示方向光，点光源跨 cubemap face 的连续过滤仍未完成。

此前算法修复和 GPU 测试见 [VT／CSM／PCSS 修复记录](vt-csm-pcss-fixes.md)。

## 本次验证

- 独立源码快照基于 `73d6ed2`，仅叠加本次诊断、测试场景与文档改动；保留同工作目录中的其他路径追踪修改。
- Metal Release 构建成功；画廊验证 HDR 有限，导出验证位置缓冲尺寸及有限值，脚本验证所有原始文件尺寸／有限值、驻留计数、物理槽位唯一性及根页有效。
- 五个级联均有可见接收面；PCF、默认 PCSS 和放大光源 PCSS 的实际输出不同，逐像素计数存入 JSON。
- `SCENERENDERER_DISABLE_PIPELINE_DISK_CACHE=1 ctest --test-dir build --output-on-failure`：**17/17 通过**，包含实际 VT 接缝、CSM、PCSS、地形、编辑器和引擎 GPU 回归。
- 首轮启用磁盘缓存时，已有的 `Native pipeline cache was not persisted` 检查未通过；本次关闭磁盘缓存后完成完整回归，未将其计为磁盘缓存持久化验收。未在本次重新执行 Vulkan 后端捕获。
