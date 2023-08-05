## 结构

## 1. Rendering

### 1.1 RenderManager

+ 存储所有的shader，其他程序获取shader均通过 getShader() api。
  why：防止相同的shader被多次编译和创建
+ 存储渲染相关的Uniform Buffer（View, Perspective matrix; 
  Lighting related stuff) 
+ 渲染设置 ( enableHDR )
+ 保存各类RenderPass

### 1.2 RenderPass

+ 目前支持的：basePass (常规的渲染), postPass
+ why：主要是因为framebuffer的操作和常规的buffer不一样，因此特地区分开，这样能更有条理
+ 计划支持：PostprocessPass

### 1.3 RenderScene

+ 要渲染的场景
+ 存储各种Object，Lights，terrain，skybox和main_camera

## 2. Resource (Assets)

### 2.1 ResourceManager

+ 存储所有的Textures
  why：防止相同的texture被多次导入

### 2.2 Texture

+ 存储 opengl 中的texture id
+ 存储路径，作为texture的唯一识别码
+ 存储texture的type，作为shader中绑定的内容

### 2.3 Material 

+ 存储texture数组
+ material用于给mesh renderer提供材质
+ 提供了 LoadPBR,loadTerrain,loadCubeMap等函数

## 3 Scene & GameObjects

### 3.1 GameObject

+ 场景内的物体，用来包含其他Component
+ 在 AddComponent 时，会给 Component添加反向链接的指针，因此，Component能够调用自己所属GO的其他组件

### 3.2 Component

+ 组件，解耦后的各种功能
+ 目前有的：Transform, Lights, MeshRenderer, MeshFilter

### 3.3 Transform 

+ 物体的位置，旋转，scale信息，transform用于生成一个物体的 model matrix 

### 3.4 Lights 

+ 灯光，包含 Directional Light 和 Point Light
+ 注意，lights通过transform组件得到自己的位置，本身不包含position信息

### 3.5 MeshFilter

+ 实际存储 Mesh 的组件

### 3.6 MeshRenderer

+ 对Mesh做渲染的组件
+ Renderer通过GO拿到MeshFilter，然后再进行渲染
+ Renderer中包含一个指向shader的指针（不一定合适）
+ render() 函数对物体进行渲染

## 4 InputManager

+ 获取所有的

## 5 Meta Register

+ 反射、序列化 in progress