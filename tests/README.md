# 测试说明

当前跨平台测试由根目录 `CMakeLists.txt` 注册；构建选项与平台限制见 [构建与运行](../docs/getting-started.md#快速运行)。

## CMake／CTest

先完成对应后端的配置与构建，再运行：

```sh
ctest --test-dir build --output-on-failure
# 独立 Vulkan 构建
ctest --test-dir build/vulkan --output-on-failure
```

| 路径 | 覆盖范围 |
| --- | --- |
| `PT/` | CPU PT／BDPT、材质、采样、输出、多线程确定性、OIDN、地形／沙滩／FFT 水体冻结与 Beer/Fresnel |
| `engine/` | 任务系统、帧队列与并发契约 |
| `rhi/` | 设备与图形契约、图像解码、OpenGL 状态与 Vulkan 验证 |
| `test_bake_terrain_vt.py` | 地形离线分页、mip、边框与输入校验 |

其他 GPU 自检入口保留在 `src/rhi/` 和 `src/renderer/rhi/`，由渲染器命令行及 CTest 调用。测试集合随平台与构建选项变化；使用 `ctest --test-dir build -N` 查看当前注册项。

Python 地形测试需要 NumPy 与 Pillow：

```sh
python3 tests/test_bake_terrain_vt.py
```

## 历史反射实验

`legacy/test.cpp` 与 `legacy/pch.*` 保留早期 GoogleTest 反射实验源码，供历史参考；它们未注册到当前 CMake／CTest 测试集合。独立 Visual Studio 工程与 NuGet 清单已移除，当前构建与回归统一使用 CMake，无需恢复旧 NuGet 缓存。

程序化 PT 数值测试为 `pt-procedural`；`pt-native-sky` 另验证设备线程 FFT 捕获、时刻变化、异常回传及水面／水下相机的 CPU/GPU 能量一致性。覆盖范围与实际场景图见 [程序化 PT](../docs/path-tracing-procedural.md)。
