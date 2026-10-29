# CPU 并行原生命令录制

M6.3 在单 Graphics Queue 内按闭合 Pass 边界拆分冻结的 RHI 命令包。`CompileOptions.recordingThreads`、`SceneRenderOptions.recordingThreads` 范围为 1..16，包含调用线程，默认 1。`--recording-threads N` 将配置传到场景 Graph；Benchmark 配置字段为 `recordingThreads`，分析器拒绝把不同配置合并为同一 case。

## 准备与录制边界

Graph 的编译、资源/视图/不可变绑定组分配、Execute 回调及命令包生成仍在 Pool 的 owner 线程。回调生成的包保有资源、Shader、PSO、绑定组与 AS/ShaderTable 的强引用。所有原生对象准备完毕后，后端只读这些包，工作线程不调用 Graph 回调、不创建描述符、不更新资源池或全局资源状态。

每个 Pass 结束 Marker 后调用 `CommandList::RecordingBoundary()`。边界必须处于 Rendering 和 Marker 外，并清空包生成器的 Pipeline/Group/Geometry 状态，下一块必须显式重新绑定。原生上下文同样不继承前块状态。按命令数量选取已有边界，实际块数不超过请求线程数；单个大 Pass 不拆开，可能没有并行收益。尾部 Export 屏障与 Timestamp Resolve 保持原序，可成为独立尾块。原始 RHI 使用者不设置边界时仍录制一块。

Runtime 按需建立最多 15 个持久工作线程，调用线程负责第 0 块。后端在唤醒工作线程前取得独立 DX12 Allocator/List 或 Vulkan CommandPool/Buffer。所有块录制完成后才按原顺序一次提交，仍由真实 Graphics fence/timeline 完成点保护整组上下文及原始命令包；CPU 录制的并行不改变 GPU Pass 执行依赖。

DX12 使用一次 `ExecuteCommandLists` 提交整组列表；[Microsoft 的状态衰减规则](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12#state-decay-to-common) 区分一次调用中的多个列表和多次独立调用。因此这里保持一次调用，避免分块意外引入新的 Buffer 状态衰减边界。

## 生命周期、失败与计时

上下文缓存只选择 `use_count == 1` 的已退休对象；同帧选择多个上下文时先把它们加入本次 Batch，避免重复选择。在途批次整体保活，取消 Graph 尚未进行原生录制。分配或录制失败时先等待全部录制工作结束，再从缓存移除本批次触及的上下文；不提交部分命令，也不发布图最终状态。工作线程的异常在 join 后传回 owner，同一线程池可继续使用。

每批次共享首上下文的 Timestamp QueryHeap/Pool。Vulkan 首块重置，所有块按原始槽位写时间戳，末尾解析；整个批次退休前不复用。DX12 描述符 HeapBind 计时写入各块私有计数，再由 owner 汇总，避免并发写 CPU 探针。新增 `rhiNativeRecordMs` 是取得上下文之后的录制墙钟时间，包含任务调度/等待；`rhiNativeSubmitMs` 仍包含原生准备、录制与队列提交，二者重叠，不能相加。HeapBind 是跨线程工作时间之和，可能超过录制墙钟。

## 验证入口与限制

`recording.contracts` 验证 16 个工作同时进入、线程持久复用、owner 参与、多个异常后全员 join/复用，以及闭合边界分块完整性。`gpu.{dx12,vulkan}.indexed_scene` 比较串行/4 线程的 Direct、IndexedDirect、CpuIndirect、GpuCulled；覆盖 Forward/Deferred、AS 构建和 Ray Tracing、阴影/Masked/透明、空场景、全部剔除、镜像与在途版本，读取每个 GPU Pass 时间戳。

并行只加速后端原生命令翻译；场景提取、材质准备、Graph 回调及命令包生成仍为串行。线程调度、更多原生命令列表和工作不均衡都有额外成本，默认继续使用 1 线程。它不实现多 GPU 队列或内存 Alias。实测与硬件范围由阶段计划和性能记录给出。

M6.4 另行加入可选 [异步 Compute](async-compute.md)：队列/等待边界会要求额外命令块，块数可超过请求的 CPU 线程数；持久池仍最多用该请求数量的工作线程完成这些块。关闭异步时保持本节所述 M6.3 分块方式。
