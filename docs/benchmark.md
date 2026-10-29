# 场景 Benchmark 与分析契约

Sandbox/Player 共用 CaptureScene 的固定场景路径。`--benchmark N` 按帧测量，`--benchmark-seconds S` 按时长测量，两者互斥，均要求 headless 和项目。支持 0<S≤3600 秒；预热必须同时满足最少帧数和最少时长，默认 5 帧/0 秒，可独立设置为 0。测量时长从预热结束开始，到最后一个完整样本结束；最后一帧可能越过预算。固定场景相机，不接受其他验收 Demo 混入同次 benchmark。

## 计时边界

| 指标 | 单位与范围 |
| --- | --- |
| frameTimeMs | 毫秒；SceneRenderer.Render 开始到该帧 GPU 完成点等待结束，含 CPU 准备/录制/提交与剩余 GPU 等待 |
| cpuRenderMs | 毫秒；Render 调用的同步 CPU 时间，含场景提取、材质、Graph 与提交，不含之后的完成点等待 |
| gpuWaitMs | 毫秒；提交返回后 runtime.Wait 的 CPU 阻塞时间，不能解释成 GPU 执行时间 |
| graphCompileMs / graphPrepareMs / graphRecordMs | 毫秒；Render 内的子区间，与 cpuRenderMs 重叠，不能相加为帧时间 |
| gpuPassesMs | 毫秒；Graph 实际存活 Pass 的 GPU timestamp 范围 |
| gpuPassSumMs | 毫秒；该帧 GPU Pass 范围之和，不含范围之间的屏障、查询解析或完整提交开销 |
| elapsedMs | 测量起点到本帧 GPU 等待结束的单调时间，包含先前样本的采集/输出开销 |
| wallTimeMs / throughputFps | 整个测量循环耗时/实际完成帧数除以该耗时；包含样本采集与流式输出开销 |

此模式逐帧等待，没有 Present、VSync 或窗口两帧重叠。它用于可重复的串行帧延迟比较，不代表交互窗口的最大流水吞吐。逐帧 profile 读取、JSONL 编码/写出和在线统计在 frameTimeMs 之外，仍消耗 wallTimeMs。初始化、预热、最后的流 flush/关闭、参考截图/Graph 导出和汇总写盘均在测量循环之外。profiling 本身的 GPU 查询开销仍在被测程序中。

## 数据与生命周期

非 benchmark 的 `--profile` 仍输出 v1 累计统计，窗口报告标记配置可能改变。Benchmark 报告为 schemaVersion=2，保留原 cpuRender/graph*/gpuPasses/memory 汇总，并增加：

- buildConfiguration、recordedAtUtc、绝对项目路径、GPU vendor/device/software 身份；实际设备名/驱动/validation 与画面配置。
- benchmark.complete、timingMode=`headless-serial`、请求和实际预热预算、请求帧数/时长、实际 sampleCount/wallTimeMs/throughputFps。
- benchmark.metricsMs：每个计时指标的样本数、均值、最小/最大值。
- benchmark.samplesFile：相对报告目录的原始样本路径；sampleSchemaVersion 当前为 1。

顶层 warmupFrames 保留请求帧数的兼容含义；实际预热次数和时间以 benchmark.warmupFrames/warmupMs 为准。

B3 起 headless 报告可包含 `resourceReuse`：`frameUpload` 为 pages/reservedBytes/batchHighWaterBytes/batches，`descriptors` 为 pages/capacitySlots/inUseSlots/highWaterSlots/nativeCreations。在测量结束、截图前采集，包含启动/预热累计量；不同于仅覆盖测量帧的 cpuOperations。描述符项数不代表显存字节；接口与拥有权见 [资源复用](resource-reuse.md)。

默认文件在 OUTPUT/BACKEND/benchmark.json 和 frames.jsonl。`--profile`、`--samples` 可独立指定路径，但不能指向同一文件。每行 JSONL 是一个完成帧：

```json
{"schemaVersion":1,"frame":0,"elapsedMs":4.2,"metricsMs":{"frameTimeMs":4.2,"cpuRenderMs":3.1,"gpuWaitMs":1.1,"gpuPassSumMs":0.05},"gpuPassesMs":{"Forward.Opaque":0.04,"Display.ToneMapping":0.01}}
```

示例省略其他 Graph 指标。Recording 位于 src/benchmark，独立于场景/RHI，只接受有限非负计时值、顺序时间戳和动态名称映射。逐帧流式输出，在线统计仅保留每类指标的 count/mean/min/max，不积累整段运行的帧数组。Close 验证并关闭流之后才能发布 complete 汇总；中途退出的 raw 文件不构成成功运行。不要手工覆盖旧报告后把残留文件当作本轮成功结果，批量工具为每次运行创建全新目录。

新增 CPU 阶段计时直接扩展 metricsMs；GPU Pass 名称自动随 Graph 进入 gpuPassesMs。分析器自动处理新指标；分析器将 GPU Pass 展平为 `gpu/名称`，该前缀保留。若改变单位、帧身份或字段含义，应升级相应 schema，不复用原字段承载不同语义。内存仍为报告时的 RHI/进程快照，不是逐帧 GPU 内存曲线。

## 可选 RHI CPU 分解

`--cpu-profile` 仅用于 benchmark，默认关闭。它在测量循环中启用 Runtime 的 owner-thread 累计计时，记录 Buffer 创建、BindingGroup 创建、TextureView 创建、原生录制/提交、Collect 回收和实际 Fence 等待。逐帧增量分别作为 rhiBufferCreateMs/rhiGroupCreateMs/rhiViewCreateMs/rhiNativeSubmitMs/rhiRetireMs/rhiFenceWaitMs 进入 metricsMs；报告 benchmark.cpuOperations 同时保存每项 calls/totalMs。预热、截图和测量结束后的清理不计入这些计数。

这些是包含调用内部驱动/系统工作的 CPU 墙钟区间，有额外计时开销，适合定位而不替代关闭探针的基线。Buffer 创建不含调用方之后的 Map/拷贝；Retire 是 Collect 的调用时间和调用次数，包含已完成命令释放及资源析构，不是“释放资源数量”。原生 Submit 不含前后 Collect。gpuWaitMs 包含 Runtime.Wait 中的 Fence 等待及 Collect，因此不能直接视为 GPU 执行时间。CPU 开启/关闭状态参与分析器配置一致性检查。

B3 另外记录 `rhiDescriptorPageCreateMs` 和 `rhiDescriptorHeapBindMs`，分别为实际原生描述符 heap/pool 创建和 DX12 SetDescriptorHeaps。前者包含在 GroupCreate 内，后者包含在 NativeSubmit 内，不能重复相加。稳定场景的逻辑 GroupCreate 可以保持非零，同时原生描述符创建次数为零。

B3.4 的默认 View 缓存命中不进入 `rhiViewCreateMs`，该项计数表示实际构造次数。CPU 计时开关不改变缓存策略；timestamp 池也继续保护 CPU 结果的持有期，不在 GPU 完成后立即覆盖仍存活的结果。

CPU 区间不覆盖所有对象析构，例如被 Graph 剔除的资源可能在 Render 返回前释放；它们不能作为完整线程 CPU profile。分析器核对各项 totalMs 与逐帧增量之和，并在报告/可选图表中展示操作时间和 calls/frame。扩展样本与实际定位见 [B2 记录](performance/b2-pbr-rtx5060.md)。

`sample/benchmark/pbr-diagnostics.json` 提供完整 PBR 720p、低分辨率 257×193、BRDF 查看和较小的原始 RenderLab 场景。BRDF 查看仍经过场景参数/材质准备，但无消费者的场景绘制在 Graph 编译时剔除，可以用实际调用计数区分准备开销和 Draw/描述符开销。原始场景用于对照较少 Draw 的创建计数；CPU 探针不改变资源分配与帧提交策略。

## 批量执行与聚合

正式工具位于 tools/benchmark，示例配置为 sample/benchmark/pbr.json。配置 schemaVersion=1，以 defaults 加 cases 的局部覆盖描述场景；executable/project 相对配置文件解析，CLI 覆盖路径相对当前目录解析。可增加场景、后端子集、重复次数、时长/预热、尺寸、质量、显示视图与显式 validation。

M6 增加 pipeline（默认 forward）与 submission（默认 direct）设置。Runner 将选择传入 CLI 并核对报告；分析器拒绝把同一 case 下不同管线/提交方式混为可比较配置。GPU Driven 配置与生成规模场景见 sample/benchmark/gpu-driven.json 和 [测量记录](performance/m6-gpu-driven.md)。

Runner 顺序执行子进程，不并发占用 GPU；每次重复反转后端顺序。每次运行保存命令数组、cwd、退出码、console/player 日志、最终设备报告、计时报告、逐帧数据及测量外截图。manifest 保存工作区 revision/脏状态、真实 exe SHA-256、部署 Shader 与项目源资产 hash；工作区 revision 不冒充编译时 revision。整组结束再次核对内容，变化则拒绝将整组标记 complete。失败/超时/中断记录到清单并退出，不用成功样本掩盖失败。

分析器可接受一个或多个完成清单/单次 v2 报告。它检查样本 schema、连续帧号、有限值、单调时间、完整样本数、预热/测量预算和 GPU Pass 合计与 C++ 汇总的一致性；相同 case 必须具有相同项目、尺寸/质量/视图、设备、构建、validation 和测量预算。清单聚合还核对 exe/Shader/项目输入 hash，同后端驱动版本必须一致。独立 report 没有整组来源 hash，不与带来源的清单混合；需要跨版本比较时应明确使用不同 case 标签。缺失/失败/混合配置拒绝比较。输入文件必须保留引用结构；移动结果时整体移动每次运行目录/清单。

分析输出 analysis.json、长表 metrics.csv、report.md。每次运行从完整原始样本计算 min/max/mean/stdev、P50/P95/P99，分位数按 `(N-1)×p` 线性插值。重复间等权聚合每次均值和 P95，单独报告 run mean 标准差；不会把帧数更多的运行隐式赋予更高权重。只有一次重复时，重复间标准差为 null/n/a。没有假定统计显著性，电源/温度/后台负载仍影响结果。

核心运行与聚合仅用 Python 3.10+ 标准库；`--plots` 可选 Matplotlib，已验证的 Python 3.12/Windows x64 依赖固定在 requirements.txt。图表输出 PNG/SVG，包括帧时间序列/经验累积分布及逐 GPU Pass 比较。仅显示层最多约 2000 个点，数值分析始终使用全部样本。原始数据可由 pandas、其他绘图库或后续分析器直接消费。

验证入口：benchmark.contracts、core.contracts、gpu.*.player，以及 `python -m unittest discover -s tools/benchmark -p "test_*.py"`。构建、直接运行与批量命令见 [手册](../manual/build.md#benchmark-与双后端分析)，实际验收与硬件结果见阶段计划。
