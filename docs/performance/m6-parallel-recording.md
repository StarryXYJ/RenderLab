# M6.3 CPU 并行录制验证与测量

日期：2026 年 10 月 29 日。接口、分块、生命周期与计时含义见 [并行录制](../parallel-recording.md)。M6.3 只改变后端原生命令翻译，单 Graphics Queue 按原序执行。

## 环境与输入

Windows x64、MSVC 19.51 / CMake NMake、NVIDIA GeForce RTX 5060；Debug/Release 分别使用 `out/build/m5-nmake` 与 `out/build/m5-release`。性能使用 Release、validation off、CPU profiling on、257×193、Deferred、固定相机、最终颜色；每次至少预热 4 帧与 0.25 秒，测量至少 2 秒，两个重复。CPU 探针包含自身墙钟读数与跨线程汇总开销，不能与 profiling off 的 M6.2 数字直接比较。

配置 [parallel-recording.json](../../sample/benchmark/parallel-recording.json) 比较 64 候选 Direct/GpuCulled、4096 候选 IndexedDirect/GpuCulled，以及请求 1/4 线程。场景来自 `gpu.dx12.indexed_scene` 生成的固定规模项目，没有另改相机、照明、材质或物体布局来制造并行收益。一个 Pass 内不拆绘制；实际分块数由各次捕获的 `scene-plan.json` 核对，可能低于请求线程数。

首轮在 4096 候选的经典 Direct 路径触及既有 DX12 descriptor page budget 上限，完整失败清单与前八次结果保留在 `out/m63-benchmark`，不计入最终套件。没有扩大预算或修改此上限；改用已有索引场景表的 IndexedDirect 对照，仍逐对象调用 Draw，避免把描述符容量限制当成并行录制的结果。4096 候选 Direct 不属于本轮支持或性能结论。

本机执行：

```powershell
ctest --test-dir out/build/m5-release -R 'recording.contracts|indexed_scene' --output-on-failure
python tools/benchmark/run.py sample/benchmark/parallel-recording.json --exe out/build/m5-release/renderlab_player.exe --output out/benchmarks/parallel-recording
```

示例配置默认引用 `windows-release` 中生成的场景；本轮实际把路径对应为 `m5-release` 并保存为 `out/m63-suite-prepared.json`，最终原始运行在 `out/m63-benchmark-prepared`。输入清单、样本/报告/截图 hash、构建二进制与 Shader hash，以及每次完整均值保存于 [summary.json](m6-parallel-recording/summary.json)。其中 sourceRevision 是父提交，trackedChanges=true；实际测试二进制由其 SHA-256 标识，不宣称使用已提交干净树构建。

## 实际结果

Debug/Release 全目标构建成功；两配置所选 Core/Content/Graph/CLI/Benchmark/录制合约及双后端索引、数组、间接、状态、RHI、Graph、PBR、Deferred、AS-Ray Query、反射各 26/26。最终头文件补全后 Release 追加 8/8，4 线程取消的 Graph 合约在两配置追加通过；新建 Renderer 的并行 TLAS + TraceRays 专项在两配置双后端另行通过。GPU 专项要求 validation errors=0、关闭后场景资源账本为 0。Python Benchmark 校验 15/15。

最终套件 32 次、26139 测量帧均通过完整性检查，8 组串行/并行各 4 张截图逐字节一致。请求 4 线程时 Direct/IndexedDirect 实际为 2 块，GPU 剔除为 4 块；较大的几何 Pass 无法跨上下文拆开。

下表均为两个重复的均值，单位 ms，顺序为 1 线程 → 请求 4 线程。NativeRecord 是录制墙钟，已经包含于 NativeSubmit。

| 后端 | 候选/提交 | 帧时间 | NativeRecord |
| --- | --- | --- | --- |
| DX12 | 64 Direct | 2.269 → 2.355 | 0.0427 → 0.0556 |
| Vulkan | 64 Direct | 2.090 → 2.087 | 0.0383 → 0.0391 |
| DX12 | 4096 IndexedDirect | 20.780 → 20.502 | 0.1039 → 0.0930 |
| Vulkan | 4096 IndexedDirect | 20.439 → 19.446 | 0.0970 → 0.0930 |
| DX12 | 64 GpuCulled | 0.930 → 0.989 | 0.0414 → 0.0563 |
| Vulkan | 64 GpuCulled | 0.848 → 0.851 | 0.0318 → 0.0433 |
| DX12 | 4096 GpuCulled | 17.953 → 18.050 | 0.0949 → 0.0963 |
| Vulkan | 4096 GpuCulled | 17.313 → 18.170 | 0.0656 → 0.0756 |

原生命令录制只占这些场景总帧时间的一小部分，场景准备/命令包生成仍为串行。4096 IndexedDirect 的录制均值略低，但其他配置持平或增加，部分帧时间变化也远大于录制变化，不能单独归因于录制并行。这是短时配对证据，不证明统计显著的普遍收益；默认保持 1 线程。没有增加按物体拆 Pass 或改变场景来取得更有利的结果。

## 验证范围与限制

CPU 合约检查 16 个任务同时进入、调用线程参与、持久线程复用、多个异常后全部 join/复用、非法线程数量和闭合分块边界。Graph 检查 owner 回调及实际分块数，取消不发布状态与资源保活沿用既有回归。场景专项逐字节比较 Direct、IndexedDirect、CpuIndirect、GpuCulled 在串行/4 线程下的输出，并读取全部 GPU Pass 时间戳；包含 Forward、Deferred、AS/TraceRays、阴影/Masked/透明、空/全剔除、镜像与在途版本。

静态 BLAS 仍由 EnsureMeshes 中的独立同步 Graph 构建，本场景的并行 AS 证据针对 TLAS；未把同步 BLAS 算作并行验证。本轮未模拟原生驱动故障或设备丢失，也未验证其他 GPU 或多视口长期压力。线程池异常测试验证 CPU 任务收尾，不能当作原生设备故障恢复证据。窗口吞吐与编辑器交互不由这次 headless 串行测量代表；后续多队列与 Alias 仍需分别实施和验收。
