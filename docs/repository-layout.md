# 仓库结构与文件约定

本项目是独立的 C++17／CMake 渲染器，构建入口统一为 `CMakeLists.txt`。构建与运行命令从仓库根目录执行；根目录保留 `config.json`，避免改变运行时资产路径。

```text
Scene-Renderer/
├── CMakeLists.txt          # 当前跨平台构建与 CTest 注册
├── config.json            # 默认场景配置
├── include/               # 项目头文件，按模块分组
├── src/                   # C++／Objective-C++ 实现与 shader 源码
│   ├── engine/            # 任务、快照与渲染运行时
│   ├── rhi/               # 后端与统一 shader
│   ├── renderer/          # 场景与效果调度
│   ├── PT/                # CPU 路径追踪
│   ├── metal/             # 退役的 Metal GL 兼容桥
│   └── shader/            # 旧 OpenGL／Metal 兼容 shader
├── tests/                 # 当前 CMake／Python 测试
│   └── legacy/            # 历史反射实验源码，不属于当前 CTest 集合
├── tools/                 # shader 编译、资源下载与地形分页工具
├── docs/                  # 技术说明与文档索引
│   └── archive/           # 原始架构与开发计划
├── samples/               # 示例资产、下载清单与许可说明
├── img/                   # README 与技术说明使用的展示图
├── external/              # 随仓库保留的第三方源码与头文件
└── lib/                   # Windows CMake 构建依赖的预编译 .lib
```

## 新文件放置

- 头文件和实现分别放入 `include/<模块>/` 与 `src/<模块>/`；保持现有命名，新增 `.cpp` 纳入 CMake 的源码扫描，无需维护独立的 IDE 工程列表。
- 新 RHI shader 放入 `src/rhi/shaders/`，编译产物由 `tools/compile_rhi_shaders.py` 写入构建目录；旧兼容 shader 保留在 `src/shader/`。
- 测试放入 `tests/<模块>/` 并在 `CMakeLists.txt` 注册；`tests/legacy/` 仅保留历史反射实验源码。
- 技术说明放入 `docs/` 并在 [文档索引](README.md) 添加入口；历史资料放入 `docs/archive/`。
- 公开展示截图放入 `img/`，有明确来源与使用条件的示例资产放入 `samples/assets/` 并更新 [场景资源说明](../samples/README.md)。

## 本地文件与依赖

`.gitignore` 排除构建目录、IDE 个人配置、`imgui.ini`、运行生成的 `out.ppm`／`output.txt`、GPU 捕获、Python 缓存，以及本地资源与下载目录。新实验输出建议写入 `build/`；需要随文档展示的结果再选择放入 `img/`。

`asset/` 是历史本地资源包；`samples/downloads/` 和 `samples/assets/gi/` 由下载工具生成。大型 GI 模型的获取与校验见 [场景资源说明](../samples/README.md)。

原 Visual Studio 解决方案、工程文件、个人配置与旧 NuGet 清单已移除。Visual Studio 可直接打开仓库文件夹使用 CMake；需要 IDE 工程时，由 CMake 在构建目录生成。生成的 `.sln`、`.vcxproj` 与 `.filters` 文件不进入新提交。

`lib/*.lib` 仍被 `CMakeLists.txt` 的 Windows OpenGL 分支直接引用，继续随仓库保留；该目录的调试符号与导出中间文件不再跟踪。当前未统一 Windows 第三方依赖获取流程，替换这些库应作为单独的构建迁移。

此前整理已停止跟踪本地生成文件与 NuGet 缓存，保留本机副本；`packages/` 中的历史缓存无需恢复，也不进入新提交。旧提交中的工程文件与缓存仍存在于 Git 历史中。
