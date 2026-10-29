# Buffer feeder 异步 Compute

M6.4 提供默认关闭的独立 Compute Queue。`CompileOptions.asyncCompute` / `SceneRenderOptions.asyncCompute`、场景 CLI `--async-compute` 与 Benchmark 布尔字段 `asyncCompute` 控制同一执行器；`--recording-threads` 独立控制 CPU 录制。未开启时，所有 Pass 仍在原 Graphics Queue。

## 允许的子图

`ComputePassBuilder::AllowAsyncCompute()` 是提示，启用异步后编译器检查完整访问计划。首版只接受只读 Upload Buffer 输入（Uniform / ShaderRead）和瞬态 Device Storage Buffer，异步 Pass 只能以 Storage 用途访问这些输出。禁止纹理、AS、导入 Device/Readback Buffer，以及 Graphics → Async 的资源或显式顺序依赖。初始化、版本和环检查仍适用，不能通过队列提示绕开。关闭开关时提示不改变既有图。

当前 Renderer 标记 `Indexed.ClearCounts` / `Indexed.FrustumCulling`；透明、光栅阴影、GBuffer/Forward、AS/光追和后处理继续在 Graphics。零候选时没有异步 Pass，报告的请求开关不意味着实际使用了第二队列。运行时必须支持当前范围，缺少第二队列会在准备阶段报错，不静默回落。

## 交接与完成点

Graphics prologue 按池中实际初态，把异步瞬态输出准备为 Storage；Compute 等待这一真实 prologue 完成点及前一帧的整帧完成点。DX12 Buffer 在独立 ExecuteCommandLists 完成时衰减到 COMMON，因此此处借助原生完成点交接，不让 Compute 队列从包含 Pixel Shader 的图形状态直接转换。Vulkan 两条队列同族，无队列族所有权转移；同步与内存依赖仍通过 timeline semaphore 和图屏障显式表达。

Compute feeder 间的 Storage 写后读保持原序；不依赖结果的 Graphics Pass（例如光栅阴影）可以先运行。第一个消费者等待生产者，后续 Graphics 依赖继承这一等待。Graphics 尾部始终汇合全部 Compute 工作，再 Signal 对外整帧 fence/timeline；导入完成点、历史、上传页、池租约和原生命令上下文因此继续由包含两个队列的真实完成点保护。前帧整点等待较保守，首版不追求跨帧异步重叠。

DX12 创建真实 COMPUTE 类型队列及独立 Allocator/List；Vulkan 仅在 Graphics family 至少有两条队列时创建该族 queue 1，各原生上下文仍有独立 CommandPool/Buffer。它不声称独立硬件引擎，也不实现跨族或 Copy Queue。队列边界要求额外命令块，块数可能超过 CPU 工作线程数；持久工作池用最多请求的 1..16 个线程完成所有块。

## 失败与诊断

准备/录制失败不提交，录制异常先 join 全部任务。第一次可能入队前，Runtime 先在已预留 Pending 中保有整组上下文和命令。部分原生提交失败后保持引用、停止后续提交、不增加 LastSubmitted、不发布图最终状态；WaitIdle 必须排空实际队列后才能释放这些引用。排空再次失败继续保留。只等待已成功入队的信号，避免失败时留下等待未来未入队信号的队列。设备丢失恢复仍不支持，CPU 注入测试不能当成真实驱动故障验证。

GPU profiling 使用每队列各自的 Timestamp 频率。DX12 用 GetClockCalibration 映射到共同 QPC 域；Vulkan 在可用时启用 KHR 或 EXT calibrated_timestamps，以保证各队列共用设备时间域；同族本身不足以保证可比。缺少该扩展时，异步绘制可在不计时模式使用，异步 profiling 明确报错。读取时先减去本帧首槽的整数 tick 再转换为 double，避免巨大绝对计数损失微小区间精度，其余 Pass 起点可为负。prologue 先重置共享查询池，Compute 等待它；最终解析等待全部生产者。`queueTimeline` 保存最后一个完成测量帧的各 Pass 起止时间，`queueSubmissions` 捕获实际成功入队的执行/等待/信号事件及原生计数，图 dump 给出队列与消费者等待。事件的 value 在 execute 时为命令列表数，在 wait/signal 时为对应 fence/timeline 值；range=null 表示整组或尾部操作。

`gpuQueueSpanMs` 是已计时 Pass 的最早开始到最晚结束，包含中间依赖/空闲；`gpuQueueOverlapMs` 是不同队列 Pass 区间的交集和，不包含未计时屏障。DX12 跨队列校准有误差，不能把接近时间戳/校准精度的微小交集认定为有效重叠。CPU、GPU Pass 合计、GPU span 与整帧时间不是可相加的独立成本，实测需分别报告。

依据：[D3D12 状态衰减](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12#state-decay-to-common)、[跨队列访问与 fence](https://learn.microsoft.com/en-us/windows/win32/direct3d12/executing-and-synchronizing-command-lists)、[Vulkan 查询顺序](https://docs.vulkan.org/spec/latest/chapters/queries.html)、[Vulkan calibrated 设备时间域](https://docs.vulkan.org/refpages/latest/refpages/source/VkTimeDomainKHR.html)。单队列与并行录制的生命周期继续见 [并行录制](parallel-recording.md)。
