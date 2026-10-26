# Render Graph 与 Feature

Graph 每帧构建，当前使用单 Graphics Queue 串行执行。资源依赖、剔除、屏障和提交属于 Graph；普通 Forward 的绘制与阶段组合属于 [Renderer](forward-pipeline.md)。Demo 使用空 Custom 组合注册 Feature，不使用 Forward 预设。

## 资源与名称

| 接口 | 含义 |
| --- | --- |
| `CreateTexture(name, desc)` / `CreateBuffer(name, desc)` | 声明未初始化瞬态资源，物理分配延后 |
| `ImportTexture(name, object, info)` / `ImportBuffer(...)` | 导入外部对象，声明状态、初始化情况和完成点 |
| `GetTexture(name)` / `GetBuffer(name)` | 建图时取得名称所指资源的最新版本 |
| `SetTexture(name, handle)` / `SetBuffer(...)` | 创建别名或重定向名称；要求最新版本，不复制内容 |
| `GetTextureDesc(handle)` / `GetBufferDesc(handle)` | 建图时查询描述 |
| `Export(handle或name, finalState)` | 保留输出，指定图外交接状态 |

名称区分大小写、非空，在单图内 Texture/Buffer 共用命名空间。Create/Import 不允许重名，Set 允许同类型重定向。名称仅在建图时解析，执行时使用类型化 handle，没有全局名称表或 Context 名称查询。

Handle 包含图身份、资源索引与逻辑版本。写入生成新版本，名称查询自动取得最新版本，已经取得的 handle 仍指向原版本。跨图、无效版本、旧版本写入和 Export 后写入会拒绝。

版本共享物理资源。旧版本读取通过 WAR 排在覆盖之前；保存旧 handle 不会保存旧画面。持久快照必须复制到独立纹理。

```cpp
rendergraph::Graph graph;
graph.CreateTexture("Noise", {257, 131, rhi::Format::Rgba8, true, false});
auto pass = graph.AddComputePass("Noise.Initialize");
auto output = pass.WriteTexture("Noise");
pass.Execute([pipeline, output](rendergraph::ComputeContext& context)
{
    context.Bind(pipeline);
    context.BindTexture(0, output);
    context.Dispatch(33, 17);
});
graph.Export("Noise", rhi::State::Sampled);
graph.Compile();
auto submission = graph.Execute(pool);
```

## Pass 声明与执行

AddRasterPass / AddComputePass / AddCopyPass 返回对应 Builder。直接声明访问，再用一次 Execute(callback) 封闭 Pass，之后才能添加下一个 Pass。未封闭 Builder 禁止 Compile。回调延迟到 Graph::Execute 录制。

| 声明 | 语义 |
| --- | --- |
| ReadTexture(handle或name) | Raster/Compute 默认 Sampled；Copy 默认 CopySource |
| ReadBuffer(handle或name) | Raster/Compute 默认 ShaderRead；Copy 默认 CopySource；Uniform/Vertex/Index/Indirect 显式指定 |
| WriteTexture / WriteBuffer | 默认 Storage，Copy 默认 CopyDestination；完整覆盖，不保留旧内容 |
| ReadWriteTexture / ReadWriteBuffer | Storage 保留写，依赖旧内容，Buffer 可指定部分范围 |
| ColorTarget(handle或name, load, clear) | Raster 颜色附件；Clear 完整覆盖，Load 保留 |
| DepthTarget(handle或name, load, access) | Raster 深度附件；ReadOnly 要求 Load，禁止深度写入 PSO |
| SideEffect() | 保留无图内输出的操作 |
| Handle() | 取得 PassHandle 用于 Before/After |

一个 Pass 可访问多个资源。同一资源只允许一种用途和一个版本关系；同版本、同用途的重复只读可合并，Buffer 只读范围合并为覆盖区间。ReadWrite 直接声明保留写，不分别 Read 和 Write。Buffer 调度保守按整资源处理，范围用于校验；TextureRange 目前只允许 mip0/layer0 与匹配 aspect。

完整覆盖是作者承诺，图无法证明 Shader 写满资源；局部写入必须保留旧内容。Raster 支持单颜色、可选一个 D32、固定 Store，不支持 Discard。深度只读仍使用保守的 RHI DepthWrite 附件状态，通过 PSO 禁止实际写入，不能同时采样深度。

| Context | 命令 |
| --- | --- |
| RasterContext | Bind、BindTexture/Buffer/Sampler、Geometry、Draw/DrawIndexed/DrawIndirect |
| ComputeContext | Bind、BindTexture/Buffer/Sampler、Dispatch |
| CopyContext | Buffer Copy、Buffer↔Texture CopyTexture、同尺寸同格式颜色 Texture→Texture CopyTexture |

先 Bind 已准备好的 RHI Pipeline，再按 slot/space 绑定已声明 handle。上下文根据 Layout 判断用途，内部建立 BindingGroup，在 Draw/Dispatch 前校验完整性、版本和范围。Bind 新 Pipeline 会清空资源和几何绑定。公共 API 不提供 PrepareContext、PassData、原生资源解析、原生命令列表、手工屏障或内部提交。Shader/PSO 事先准备，Material 适配属于 Renderer。

图拥有闭包值，调用者仍须保证嵌套引用、裸指针和 span 的生命周期；C++ 不自动把引用变成拥有数据。内置 Feature 会复制本帧绘制数据并保存名称解析后的 handle。Context 仅在回调期间有效。

## 编译与提交

Compile 仅分析 CPU 图，不访问 GPU。Execute(pool, optionalSwapchain) 在 Runtime 所有线程完成物理分配、View/BindingGroup 建立、录制与提交，一张图只能执行一次。失败使图失效，取消未提交命令，不发布假状态；被剔除 Pass 不分配资源、不执行回调。返回 Submission 的完成点、导出物理对象和最终状态用于图外交接。

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

Export 仅接受当前最新版本和有效 final state。Execute 成功提交后返回 completion、导出物理资源与最终状态；调用方以此交给下一张图。应用在下一帧 CPU 读取读回缓冲或改写上传缓冲前，仍必须满足对应完成要求。图不会替应用把 Host 访问变成异步完成。

上传缓冲允许 HostWrite→GPU Read，以及不同 GPU Read 用途之间的显式依赖。读回缓冲允许 CopyDest/HostRead 交接和重复拷贝；它们仍使用各后端正确的 Host/Transfer 访问掩码。

## 有界资源池与生命周期

Pool 在 Runtime 所有线程创建和使用，Runtime 和 Pool 都必须比使用它们的 Graph 活得更久。资源按描述符兼容性复用，名称不参与匹配。默认最多 64 个资源、64MiB 估算内容大小；RHI 的原生分配预算继续独立生效，估算值不是显存实际占用。

纹理估算使用 FormatBytes（RGBA32F 为 16 字节/texel），Graph 计划与 Pool 使用相同规则。SceneRenderer 为两帧高分辨率阴影配置 224MiB 的估算上限，通用 Pool 默认值不变；RHI 的 256MiB 原生预算仍独立检查。

仅在没有图租约、没有额外资源引用且提交已完成时复用。执行中的 Graph 仍持有租约；取消或销毁它不会发布虚假的最终状态。Submit 成功后才更新池中的状态和完成点，命令包继续保活 GPU 使用。

容量不足时淘汰已完成且空闲的缓存资源；全部在用时明确失败，由帧管理层释放引用并等待所需完成点。Trim 只释放已完成空闲项，不执行 DeviceIdle。M3 不在同一图中复用不同资源的存储，不做 Alias 或资源重命名。

## 诊断与验证

`Plan::ToJson` 导出 Pass 类型/源位置/存活原因、各访问的输入输出版本、Buffer 范围、RAW/WAR/WAW/显式顺序、稳定执行顺序、每个 Pass 和导出的屏障、资源估算大小和首次/最后使用，以及 CPU compile/prepare/record 时间。Execute 之后导出才包含该次录制时间；这些时间不代表 GPU 成本。每个 Pass 另有 GPU Marker，便于后续 Capture。

CompileOptions.profile 可为存活 Pass 开启起止 Timestamp（最多 32 个 Pass），返回 Submission.profile；调用方确认完成后使用 profile.Read(runtime) 读取纳秒值，未完成或其他 Runtime 均拒绝。Graph 自己录制查询解析和 HostRead 屏障，读回由提交结果保活；不开启时没有该读回分配。计时不包括 Pass 间屏障或 Present；完整约定见 [M4 工具](m4-tools.md#gpu-timestamp-与完成点)。

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
| samples/graph_features、graph_demo | Demo Feature 实例与 Custom 组合 |
| samples/graph_acceptance、graph_buffer_checks、forward_acceptance | GPU 行为验收 |

当前仍是单视图、单 Graphics Queue、单 mip/layer、保守整 Buffer 依赖、单颜色附件和固定 Store。名称表仅属于单张图，阶段枚举由 Renderer 提供。没有全局资源黑板、多队列、并行录制、Alias、执行计划缓存或跨设备同步。相关能力在真实阶段需求出现时扩展。

普通 Forward 验收另检查透明稳定排序、阶段顺序与剔除、回调输入所有权、双后端图像和独立阶段快照。实际配置见 [阶段计划](../design/phaseplan.md)。

Feature 注册、Create 依赖解析和逐帧 Build 由 Renderer 提供，见 [Feature 与管线组合](forward-pipeline.md)。Create 不执行 Graph；资源名称解析发生在 Build，Graph 的资源合约和编译职责不变。
