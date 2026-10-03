# Engine 可变数据边界与资产移交

2026-10-03，按 Engine 审查建议完成第一阶段：收紧核心世界数据与 CPU 资产的访问入口。主线程仍拥有可变世界，渲染线程消费不可变快照；这次把这条约定落实到 Transform、Light、Camera、Mesh、Material 和 MeshRenderer 的接口，而不是仅在文档中约定调用方自律。

## 原问题与修改

| 原问题 | 修改与结果 |
| --- | --- |
| Transform、灯光、Camera 参数可以绕过线程检查直接写入 | 核心参数改为私有；getter 和 setter 检查逻辑线程，封存期间禁止访问。灯光通过值副本修改并一次提交 |
| 几何与材质贴图容器可以直接改，调用方容易忘记 invalidate | Mesh 几何／MeshFilter 列表、Material 参数／贴图映射改为私有；setGeometry 和纹理槽修改自动更新内容版本 |
| 调整材质标量与更换图片共用失效入口 | 材质参数使用独立 parameterRevision，图片使用 contentRevision；快照每帧复制标量，粗糙度等修改复用原图片 payload |
| addTexture 使用 insert，已有槽位无法被新贴图替换 | 槽位明确覆盖；文件路径与内存纹理互斥，替换／移除／批量设置自动使图片缓存失效 |
| 基础形状 JSON 中的 material 没有挂到生成的网格 | 解析完成后为缺少材质的网格绑定候选材质，保留导入模型自带的材质 |
| 场景只移交组件，附带 Camera／Mesh／Material 仍属于生产者线程 | CPU 资产加入 LogicAsset；对象／世界封存时一并封存资产，经 future 移交后由接收线程接管 |
| Worker 直接持有可变 Material 对象 | 收集线程提取 MaterialData 值并深拷贝内存纹理字节，worker 解码独立数据，发布 const 图片 payload |
| 相机保存的裁剪范围与实际投影不一致 | GetPerspective 使用配置的 near／far；默认 far 设为 1000，保持原投影范围并统一深度／阴影元数据 |
| 旧聚光灯阴影朝向固定原点，方向灯竖直时基向量退化 | 旧阴影矩阵使用配置方向／宽高比，选择安全 up；灯光方向归一化并拒绝零向量 |
| Mesh 复制可能复用旧 GL handle，几何变化后旧 VAO 不更新 | Mesh 副本获得新 ID 与空 GL handle；旧 OpenGL 上传按内容版本重新建立 buffer／VAO |

## 修改接口

```cpp
transform->setTRS(position, rotation, scale);
camera->setClipPlanes(0.1f, 500.f);

auto light = directionLight->getData();
light.direction = {0.f, -1.f, 0.f};
directionLight->setData(light);

auto vertices = mesh->getVertices();
vertices[0].Position.x += 1.f;
mesh->setGeometry(std::move(vertices), mesh->getIndices());

material->setRoughnessFactor(0.3f);  // 只更新参数版本
material->addTexture(texture, "material.albedo"); // 更新图片内容版本
```

几何提交先验证索引范围与位置／法线／UV 的有限性，再替换数组。材质提交验证颜色与标量范围；JSON 参数与资源先准备后提交。灯光提交验证非负颜色、衰减参数、有效方向和锥角关系。相机验证有限参数、正宽高比、有效裁剪范围和非退化基向量。非法参数抛出异常，不部分写入这些提交的目标字段。

Mesh／Material 副本保留内容但使用新稳定 ID，复制入口也检查源线程；Camera／GameObject／Component 禁止复制。MeshRenderer 的 shader 类型、绘制模式与 polygon mode 通过检查接口修改；历史路径自定义 Shader 使用 setLegacyShader。

返回 const 引用仅阻止修改容器，不能把引用跨线程传递。调用方必须在所属线程复制数据，或者使用 SceneSnapshotBuilder 的脱离世界数据流程。只读 API 也检查线程；检查不是互斥锁。

## 封存与移交

生产者构建完整对象／世界，在静止边界调用 sealForTransfer，并通过 future 发布；封存后生产者禁止继续读写。GameObject 收集所引用的 Mesh、Material、地形材质与 sky 材质，RenderScene 额外处理 Camera。接收线程 bindScene／replaceWith 接管逻辑归属，随后才能调用资产接口。重复引用的资产在对象内去重处理。

移交必须覆盖共享的可变资产组。当前没有跨世界资产 lease 或共享可变资产的并发协调：如果两个世界共享一个 Material，封存其中一个世界会同时使该 Material 不可访问。不能在另一个线程继续编辑共享资产；需要转移整个资产组，或者先复制 Mesh／Material。Material 副本仍共享历史 Texture 指针，像素修改需要另行复制。

## 验证

在 Apple M4/macOS 上完成：

- Metal CTest **11/11**，开启 API／Shader Validation。
- Vulkan/MoltenVK CTest **12/12**；本机没有 Khronos validation layer，关闭已知阻塞的 MetalTools 组合。
- OpenGL 兼容路径 CTest **8/8**。
- CPU 并发测试通过 ThreadSanitizer；没有对完整图形应用及 AppKit／GLFW 做 TSan 验收。
- 5 个本次修改的历史 Metal 示例源文件通过语法检查；历史桥接后端没有完整运行验收。

新增验证覆盖：错误线程读取／修改／复制、非法参数保留旧值、Camera 裁剪矩阵与固定输入模式、灯光方向归一化与非法锥角、无效网格索引、资产封存／接管、Loader 中带材质的基础形状、材质标量复用图片 payload、贴图槽替换生成新 payload，以及先前快照保持原像素。

回归期间发现新增测试假定基础示例有 albedo 图片，读取空图片导致三个后端崩溃。测试现显式建立 4 像素初始贴图并检查旧／新图片，修复后上述全量回归通过。

## 剩余范围与下一阶段

本阶段没有声称全部历史对象均已私有化。Texture 的像素、路径、尺寸及 GL handle 仍是历史公开字段；材质只保护映射，不能拦截 Texture 指针的原地像素修改。此类修改必须在逻辑线程进行并调用 Material::invalidate，内存图的共享引用也需要调用方管理。Terrain／Ocean／Atmosphere 的部分效果配置、GameObject 名称与历史灯光 GPU 资源仍有公开兼容字段，应继续按主线程约定使用。

下一阶段仍按既定顺序推进：内存压力淘汰与降级、GPU 发布事务及失败回滚，然后分段上传／pipeline cache、完整资源图与 RHI 子资源、VT feedback／LOD 时间连续性。现有 RHI 配额超限仍通过错误传播退出；本次没有实现 GPU 回滚，也没有把逻辑负载统计当作实际 driver heap 统计。

设计全貌见 [Engine 设计审查](engine-design-review.md)，线程和快照协议见 [Engine 多线程说明](engine-multithreading.md)，前一阶段的值命令与配额见 [组件、命令与配额](engine-world-commands.md)。
