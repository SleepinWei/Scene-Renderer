# 文档索引

运行、截图与命令行入口见 [项目 README](../README.md)。文件放置与依赖管理见 [仓库结构说明](repository-layout.md)，测试入口见 [测试说明](../tests/README.md)，示例资源来源与使用条件见 [场景资源说明](../samples/README.md)。

## 渲染与效果

| 文档 | 内容 |
| --- | --- |
| [RHI 重构计划](rhi-refactor-plan.md) | 后端分层、迁移进度、验证与平台限制 |
| [新增经典测试场景](classic-benchmarks.md) | Dragon、Buddha、Armadillo、Sibenik 的资源、固定视角、导入修复与真实截图 |
| [天空与太阳](sky-and-sun-review.md) | 大气散射、太阳能量与 GPU 回归 |
| [RSM 实现与验证](rsm.md) | 太阳／天空间接光、算法边界与捕获方法 |
| [FFT 海洋与透明水体](ocean-fft-and-rendering-review.md) | 频谱、高清波纹、折射与散射 |
| [地形与 Virtual Texture](terrain-virtual-texture.md) | 四叉树、分页、预算与离线工具 |
| [草地植被与 FFT 湖面](vegetation-and-lake-water.md) | GPU 放置过滤、共享角点、生图水域 mask、FFT 周期与显示范围分离及验收 |
| [Mountain Lake 山湖场景](mountain-lake.md) | 官方资源、网格转高度场、材质 VT 与湖面 |
| [TSAA](tsaa.md) | 运动向量、重投影、历史裁剪与截图复现 |
| [Dragon、地形与 FFT 水面的离线 PT](path-tracing-procedural.md) | 官方透明龙／BDPT 焦散、固定时间捕获、岸线／草丛／沙滩与水下吸收 |
| [水体 BSSRDF／Jade Dragon](path-tracing-subsurface.md) | 均匀介质、HG、随机游走次表面和双后端验证 |
| [CPU 路径追踪](path-tracing-cpu.md) | 实时场景转换、材质、采样与输出验证 |
| [OIDN 路径追踪降噪](path-tracing-denoising.md) | 现成 HDR denoiser、辅助 AOV、设备选择与离线处理 |
| [收敛优化与 BDPT 焦散](path-tracing-convergence.md) | GPU Guiding、Radiance Cache、平滑玻璃、CPU BDPT 与误差对照 |
| [GPU 路径追踪与采样优化](path-tracing-gpu.md) | Sobol/VNDF、自适应采样、Metal/Vulkan compute、性能与误差对照 |

## Engine 与资源管理

| 文档 | 内容 |
| --- | --- |
| [后续计划实施与验收](engine-runtime-completion.md) | 联合加载、增量图片、固定时钟、纹理子资源、图复用、地形反馈与降级 |
| [设计审查](engine-design-review.md) | 架构评价、实施状态与后续工作 |
| [多线程与渲染分离](engine-multithreading.md) | 线程归属、快照、任务与帧队列 |
| [组件与世界命令](engine-world-commands.md) | 组件注册、后台命令与资源配额 |
| [数据边界与资产移交](engine-data-boundaries.md) | 可变数据封装、版本失效与发布 |
| [GPU 帧发布](engine-gpu-publication.md) | 内存压力、事务回滚与成功画面保留 |
| [分段上传与管线缓存](engine-streaming-and-pipeline-cache.md) | 上传预算、共享 native 管线与 LRU |
| [后续修复记录](engine-followup-fixes.md) | 场景访问、图片共享与生命周期修复 |

## 历史资料

- [旧 Metal GL 兼容层迁移](metal.md)：默认构建已转入新 RHI；旧桥需显式启用。
- [原始架构笔记](archive/original-architecture.md)：早期课程项目结构。
- [原始开发计划](archive/original-progress.md)：早期进度与问题清单。

各技术文档中的验收结果对应其记录的代码与日期；当前工作副本有进一步修改时，应重新运行相关测试。
