# Render Graph 与 Feature

M3 提供每帧构建、单 Graphics Queue、串行录制的 Render Graph。图内资源访问和屏障由图管理，RHI 保留显式执行契约。阶段结果见 [phaseplan](../design/phaseplan.md)，运行命令见 [构建手册](../manual/build.md#render-graph-与-feature)。

## 分层与每帧构建

RenderPipeline 选择功能和组织输入输出。Feature 是向图添加一组相关 Pass 的普通函数或具体对象；可以每帧根据配置改变 Pass 数量、资源尺寸和处理路径。Graph 负责验证、剔除、依赖、资源生命周期、屏障和提交。

样例的 `AddNoiseFeature` 添加初始化以及可选更新 Pass，`AddSceneFeature` 添加不透明和叠加绘制 Pass，`AddReadbackFeature` 添加复制和 HostRead 导出。Feature 使用类型化返回值传递 TextureHandle/BufferHandle，不通过全局单例或字符串查询本帧资源。名称只用于诊断，允许同名资源，句柄身份不依赖名称。

```cpp
rendergraph::Graph graph;
auto noise = AddNoiseFeature(graph, noisePrograms, width, height, settings.update);
auto target = graph.Import(backbuffer, {initialState, initialized, completion});
auto depth = graph.Create(depthDesc);
auto scene = AddSceneFeature(graph, programs, noise, target, depth, geometry);
graph.Export(scene.color, rhi::State::Present);
graph.Compile();
graph.Prepare(pool);
auto submission = graph.Submit(swapchain);
```

Feature 实现和配置可以长期存在，Graph、PassData 和句柄属于单次图实例。默认 Sandbox 每个可见帧重新创建图。M3 不添加 Unity 传统路径兼容层、全局 Feature 注册表、共同虚基类或完整 Opaque/Shadow 阶段调度框架；场景 Renderer 的阶段锚点留给 M4。

## 句柄、访问和版本

TextureHandle 与 BufferHandle 是不同类型，包含图身份、物理资源索引和逻辑版本。跨图句柄、无效版本、旧版本写入、Export 后再写入都会拒绝。旧版本读取可以合法存在，图通过 WAR 顺序把它安排在物理覆盖之前；它不会自动读到新版本。

每次 Write 返回新版本，同一物理资源仍原地存储。M3 不为每个版本分配另一份纹理：

```cpp
auto output = graph.Create(textureDesc); // v0，未初始化
output = builder.Write(output, Use::Storage, Coverage::Full); // v1
output = builder.Write(output, Use::Storage, Coverage::Preserve); // v2
```

`Full` 是作者对整资源完整覆盖的承诺，不保留旧内容依赖。`Preserve` 包含输入读取依赖，用于 ReadWrite、混合或部分写入；要求旧内容已经初始化。编译器不能证明任意 Shader 的覆盖范围，也不会把一次小范围复制推断成完整写入。使用错误的 Full 声明可能产生未初始化内容。

每个 Pass 对同一资源声明一个版本/用途/范围。重复的相同只读声明可以合并；冲突用途、不同范围或拆开的读写会拒绝，ReadWrite 使用一次 Preserve 写入。Buffer 范围校验偏移和长度，但依赖与屏障仍保守跟踪整 Buffer；命令拷贝、间接参数和绑定范围必须落在声明范围内。当前 Geometry 绑定要求声明整个顶点/索引 Buffer。

TextureRange 包含 mip/layer/aspect。当前 RHI 仅支持 mip 0、layer 0 和单一 Color 或 Depth aspect；超范围、错误 aspect 会明确失败。M3 没有实现或声称已验证多 mip/layer 的原生执行。

用途包括 Sampled、Uniform、ShaderRead、Storage、Color、Depth、CopySource、CopyDestination、Vertex、Index 和 Indirect。用途必须匹配资源、Pass 类型及原生 usage。只读 StructuredBuffer 使用 ShaderRead；RW Buffer/Texture 绑定按可写 Storage 声明，不能用只读声明隐藏可能的 UAV 写入。

## Setup、Prepare 与 Execute

`Graph::AddPass<Data>` 默认构造并拥有 Data。Setup 立即执行，可以捕获调用处变量来填入 Data；Prepare/Execute 是延迟回调，编译期要求无捕获。需要的数据应按值存入 Data：

```cpp
struct NoiseData
{
    rendergraph::TextureHandle output;
    std::shared_ptr<rhi::Pipeline> pipeline;
    std::shared_ptr<rhi::BindingGroup> bindings;
    uint32_t groupsX = 0, groupsY = 0;
};

graph.AddPass<NoiseData>("Noise", rendergraph::PassKind::Compute,
    [&](rendergraph::PassBuilder& builder, NoiseData& data)
    {
        output = builder.Write(output, rendergraph::Use::Storage);
        data.output = output;
        data.pipeline = pipeline;
        data.groupsX = groupsX;
        data.groupsY = groupsY;
    },
    [](const NoiseData& data, rendergraph::ExecuteContext& context)
    {
        context.Bind(data.pipeline);
        context.Bind(data.bindings);
        context.Dispatch(data.groupsX, data.groupsY);
    },
    [](NoiseData& data, rendergraph::PrepareContext& context)
    {
        data.bindings = context.Device().CreateGroup(
            data.pipeline->desc.layout, {{0, context.View(data.output)}});
    });
```

Data 仍必须遵守 C++ 生命周期规则，不保存 Setup 局部变量的指针、reference、span 或 string_view。无捕获要求能避免常见的闭包悬空，不能替代任意嵌套成员的所有权检查。

Compile 只处理 CPU 图结构。Prepare 在设备所有线程分配必要物理资源、建立 View 并调用各存活 Pass 的准备回调，可以创建绑定和获取已就绪的 PSO。被剔除的 Pass 不执行 Prepare/Execute。

PrepareContext 只允许解析当前 Pass 声明的资源版本。ExecuteContext 提供 Bind、Geometry、Draw、Dispatch、Copy 等受限命令，不公开原生命令列表、资源创建、手工屏障或提交入口。绑定组中的每个非采样器资源都检查是否在当前 Pass 按正确用途和范围声明。

M2 的程序和 Pipeline 可在准备阶段提供给 Pass；提前准备的材质快照中所有纹理/缓冲必须显式导入并声明。Graph 不依赖 Shader/Material 模块，也不会从某个全局材质状态猜测资源访问。

## Raster 附件

Raster Pass 通过 `builder.Color` 和可选 `builder.Depth` 指定附件。图自动录制 Begin/EndRendering：

- Clear 创建完整覆盖的新版本；深度默认清为 Reversed-Z 的 0。
- Load 创建 Preserve 版本，保留颜色、深度及其生产者依赖。
- M3 的图接口不接受 Discard，避免把部分绘制错误地视为已初始化的完整输出。

当前只有一个颜色附件、一个可选 D32 深度附件，Store 固定为 Store。RHI 已支持 Clear/Load/Discard；图的限制比底层更严格，后续真实用例出现时再扩展。Overlay 样例用 Load 保持前一 Pass 的颜色与深度，输出与原始单 Pass 绘制逐字节比较。

## 剔除、依赖与顺序

Export 和 SideEffect 是存活根。Import 只表达资源来自图外，不会保留所有使用它的 Pass；写入外部资源若没有 Export 或显式 SideEffect，也可以剔除。未初始化读取只对需要执行的 Pass 检查，死代码不会强迫创建资源。

依赖分开处理：

- 内容依赖使读取者保留对应版本的生产者；Full 覆盖不保留旧生产者。
- 存活工作之间仍建立 RAW、WAR、WAW，确保共用物理资源不会被错误覆盖。
- `graph.After(pass, predecessor)` / `Before(pass, successor)` 添加硬顺序约束，不自动保留无用 Pass，也不替代资源访问或选择新版本。

编译器采用稳定拓扑排序，无约束的 Pass 按注册顺序打破平局。冲突产生环时，诊断列出相关 Pass 名称和源位置。M3 只做单队列串行执行；依赖图尚未用于 CPU 并行录制或 GPU 多队列调度。

## 自动屏障与图外交接

图从 Import 的状态或资源池记录的真实起始状态生成屏障。发生写入依赖时，同状态也可能发出屏障，例如 Storage→Storage。RHI 将它们翻译为 DX12/Vulkan 原生依赖，不增加第二套推测状态的逻辑。附件之间不强制恢复默认状态。

ImportInfo 明确给出初始 state、内容是否初始化、同一个 Runtime Graphics Queue 的 completion。队列已提交的前序工作由队列顺序保证，不为每次导入执行 CPU Wait；未提交的未来完成点、其他 Runtime 的对象会被拒绝。跨 Runtime/跨队列完成点不在本阶段支持范围。

Export 仅接受当前最新版本和有效 final state。Submit 成功后返回 completion、导出物理资源与最终状态；调用方以此交给下一张图。应用在下一帧 CPU 读取读回缓冲或改写上传缓冲前，仍必须满足对应完成要求。图不会替应用把 Host 访问变成异步完成。

上传缓冲允许 HostWrite→GPU Read，以及不同 GPU Read 用途之间的显式依赖。读回缓冲允许 CopyDest/HostRead 交接和重复拷贝；它们仍使用各后端正确的 Host/Transfer 访问掩码。

## 有界资源池与生命周期

Pool 在 Runtime 所有线程创建和使用，Runtime 和 Pool 都必须比使用它们的 Graph 活得更久。资源按描述符兼容性复用，名称不参与匹配。默认最多 64 个资源、64MiB 估算内容大小；RHI 的原生分配预算继续独立生效，估算值不是显存实际占用。

仅在没有图租约、没有额外资源引用且提交已完成时复用。录制未提交的 Graph 仍持有租约；取消或销毁它不会发布虚假的最终状态。Submit 成功后才更新池中的状态和完成点，命令包继续保活 GPU 使用。

容量不足时淘汰已完成且空闲的缓存资源；全部在用时明确失败，由帧管理层释放引用并等待所需完成点。Trim 只释放已完成空闲项，不执行 DeviceIdle。M3 不在同一图中复用不同资源的存储，不做 Alias 或资源重命名。

## 诊断与验证

`Plan::ToJson` 导出 Pass 类型/源位置/存活原因、各访问的输入输出版本、Buffer 范围、RAW/WAR/WAW/显式顺序、稳定执行顺序、每个 Pass 和导出的屏障、资源估算大小和首次/最后使用，以及 CPU compile/prepare/record 时间。Record 之后导出才包含该次录制时间；这些时间不代表 GPU 成本。每个 Pass 另有 GPU Marker，便于后续 Capture。

`--graph-no-cull` 关闭剔除，仍使用同一个编译器和执行器。`--graph-no-update` 让 Noise Feature 省略更新 Pass。`--graph-dump` 导出首个窗口帧的图。没有额外的立即模式或并行执行路径。

CPU `rendergraph.contracts` 验证初始化、Full/Preserve、共享只读、WAR 重排、顺序冲突、旧句柄/版本、范围/aspect、用途、附件 Load/Clear、取消、完成点保护、池容量和越界命令。

GPU `gpu.{dx12,vulkan}.graph` 将六张图的输出与直接 RHI 基线及 CPU 整数噪声比较，覆盖功能切换、剔除开关、Clear/Load、索引/间接绘制和池复用。额外的缓冲图执行 Upload→Copy→两个依赖 Compute→Readback，验证 Uniform、Structured SRV/UAV 和复制屏障。默认窗口的 resize/minimize/restore/单帧退出测试也通过图路径。

## 文件职责与限制

| 文件组 | 职责 |
| --- | --- |
| rendergraph/types、graph、builder | 句柄、单帧所有权、声明和版本 |
| rendergraph/compiler、validation | 存活分析、依赖、排序、屏障与访问校验 |
| rendergraph/context、executor | 准备、受限录制、提交和完成状态发布 |
| rendergraph/pool、diagnostics | 资源复用与计划导出 |
| samples/graph_features、graph_demo | 薄 Feature 和每帧管线组合 |
| samples/graph_acceptance、graph_buffer_checks | GPU 行为验收 |

当前仍是单视图、单 Graphics Queue、单 mip/layer、保守整 Buffer 依赖、单颜色附件和固定 Store。没有全局资源黑板、语义阶段锚点框架、多队列、并行录制、Alias、执行计划缓存、跨设备同步或 GPU 每 Pass Timestamp。相关能力在真实阶段需求出现时扩展。
