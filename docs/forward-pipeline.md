# Feature 实例与管线组合

公共入口是 `renderer/feature.hpp`、`renderer/frame.hpp`、`renderer/pipeline.hpp`，内置光栅 Feature 位于 `renderer/forward.hpp`。目标为 `renderlab_renderer`。

## 注册、创建、逐帧建图

```cpp
auto noise = std::make_shared<NoiseFeature>(
    noisePrograms, "Noise", 257, 131);
auto scene = std::make_shared<DemoSceneFeature>(
    rasterPrograms, geometry, "Noise");
auto readback = std::make_shared<ReadbackFeature>(
    "CameraColor", readbackBuffer, rowPitch);

// Consumers can be registered before producers.
auto pipeline = renderer::CreatePipeline(renderer::PipelineType::Custom)
    .AddFeature(readback)
    .AddFeature(scene)
    .AddFeature(noise)
    .Create();

// Each frame:
rendergraph::Graph graph;
graph.ImportTexture("CameraColor", target, importInfo);
graph.CreateTexture("CameraDepth", depthDesc);
pipeline.Build(graph, frame);
graph.Export("CameraColor", finalState);
graph.Compile();
auto submitted = graph.Execute(pool, swapchain);
```

AddFeature 只收集实例；Create 不调用 Build、不创建 GPU 对象、不访问本帧 Graph，而是检查依赖并生成冻结的组合。后续修改 Builder 不改变已经创建的 Pipeline。Pipeline 可以重复用于新帧、新 Graph 和新尺寸；Feature 不应保存旧图 handle。

RenderPipeline 持有 Feature 的 shared_ptr。Feature 的构造函数接收已准备的 Shader/PSO、配置或资源引用。唯一虚函数为：

```cpp
void Build(rendergraph::Graph&, const renderer::RenderFrame&) override;
```

RenderFrame 是本帧输入，包含准备好的 Opaque/Transparent DrawPacket 和 clearColor；没有图句柄或输出所有权。Build 仅声明资源与 Pass，Graph 的 Compile/Execute 负责具体图校验、资源分配、受限录制和提交。没有 Feature 层 Prepare/Execute/Submit。

## Create 的依赖语义

RenderFeature 构造时声明稳定唯一名称和所需 Feature 名称：

```cpp
class SceneFeature final : public renderer::RenderFeature
{
public:
    SceneFeature() : RenderFeature("Scene", {"Noise"}) {}
    void Build(rendergraph::Graph&, const renderer::RenderFrame&) override;
};
```

依赖表示“先构建所需 Feature，再构建本 Feature”。注册时拒绝空实例；Create 检查重名、缺失依赖、自依赖、循环，以及依赖与阶段顺序的冲突。多个无依赖 Feature 按名称确定顺序，不依赖注册先后；若顺序有意义必须显式声明依赖或阶段。

Feature 名称与 Graph 资源名称是两个命名空间。样例通常用主要输出名称作为 Feature 名称以减少配置，例如 NoiseFeature 的名称为 Noise、DemoSceneFeature 为 CameraColor，但并非自动从资源读取推断依赖。ReadbackFeature 的 producer 参数指定依赖的 Feature，省略时采用源资源同名约定；读取外部导入资源的自定义 Feature 可以不声明生产 Feature。

注册过程不提前执行 Feature 来猜测资源，因此缺少依赖不会通过注册顺序偶然变得可用。运行时缺失资源、错误版本、读写范围和用途仍由 Graph 检查；Feature 顺序不能替代资源访问声明。

每帧 Build 按 Create 的计划调用 Feature，并将顺序转为 Pass 硬约束。约束不保留无用 Pass；中间 Feature 被剔除也不会丢失前后存活工作的顺序。Graph 继续负责 RAW/WAR/WAW、剔除、屏障和最终执行计划。它不是第二次 Feature 依赖注册。

## Preset 与阶段

Custom 预先注入零个 Feature；Forward 注入 OpaqueFeature、TransparentFeature，以及配置启用的 ColorSnapshotFeature。两者使用相同 Builder、Create、阶段与 Build，不存在独立 ForwardPipeline 继承树或专用执行路径。

```cpp
auto builder = renderer::CreatePipeline(
    renderer::PipelineType::Forward,
    {.copyOpaqueColor = true, .copyAfterTransparentColor = true});

builder.AddFeature(customEffect, renderer::RenderStage::BeforeTransparent);
auto pipeline = builder.Create();
```

无阶段参数的 AddFeature 默认 AfterTransparent。Custom 没有内置锚点工作，但允许同样的阶段声明；同阶段按依赖排列，无依赖时按名称排列。

| 顺序 | Feature 或用户阶段 |
| --- | --- |
| 1 | 用户 BeforeOpaque |
| 2 | 内置 Forward.Opaque |
| 3 | 用户 AfterOpaque |
| 4 | 可选 OpaqueColor 快照，依赖 Forward.Opaque |
| 5 | 用户 BeforeTransparent |
| 6 | 内置 Forward.Transparent，依赖 Forward.Opaque |
| 7 | 用户 AfterTransparent |
| 8 | 可选 AfterTransparentColor 快照，依赖 Forward.Transparent |
| 9 | 用户 AfterRendering，例如阶段快照读回 |

内置与用户 Feature 都由同一 Create 处理。Opaque 无 Draw 时仍 Clear；Transparent 无 Draw 时不建 Pass，但该 Feature 的组合依赖仍存在。Forward+、Deferred 等尚未实现，不提供伪可用枚举或空实现，后续按同样机制注入 Feature。

## 名称连接与普通 Forward

Feature 之间按名称连接资源。DrawPacket 的 vertices/indices/indirect、TextureBinding.texture 和 BufferBinding.buffer 也都为本帧资源名称。Feature 内部在建图时解析成类型化 handle，执行回调保存解析结果和数据副本，不再查名称，也不依赖 Feature 裸 this 或临时 Frame 引用。

Opaque 需要关闭混合、开启深度测试和写入；Transparent 需要开启混合和深度测试、关闭深度写入。distance 是有限非负视空间距离，按从远到近稳定排序；不处理相交透明面或 OIT。支持普通、索引和间接绘制，Shader Buffer 绑定为 Uniform/ShaderRead；存储写入通过独立 Pass 声明。

| 资源名称 | 契约 |
| --- | --- |
| CameraColor | 调用者创建或导入的当前颜色附件，可用 SetTexture 重定向 |
| CameraDepth | 调用者创建或导入的同尺寸 D32 |
| OpaqueColor | AfterOpaque 用户 Feature 之后的独立真实颜色复制 |
| AfterTransparentColor | AfterTransparent 用户 Feature 之后的独立真实颜色复制 |

最终输出通过 Graph 名称查询或导出。快照无消费者且未 Export 时仍会剔除；保存旧 handle 不等于保存旧图像。当前没有 Sky，加入时须明确调整 OpaqueColor 取样点。

## Demo、生命周期与边界

GraphDemo 使用空 Custom 组合，注册 NoiseFeature、DemoSceneFeature、可选 ReadbackFeature。Feature 在前面初始化再传入，彼此不传输出 handle，也不使用 Forward 内置场景流程。当前 Demo 每帧组装实例以表达功能和读回变化；长期实例复用由 CPU 合约覆盖。

内置与样例回调按值拥有本帧解析结果，Pipeline/Feature 可以在建图后释放。自定义 Feature 同样必须按值保存必要输入，不能让延迟回调依赖已释放的 this。Feature 本身可保存长期配置；并发复用须由调用方保证 Build 与其状态的线程安全，当前仍为单线程单队列。

普通 Forward 当前接收准备好的绘制数据，场景、Material 适配、光源与 PBR/HDR 仍属后续阶段。DepthPrepass、Shadow、Sky、后处理将按需成为独立 Feature；高级管线、AS、TraceRays 与历史管理见 [阶段计划](../design/phaseplan.md)。
