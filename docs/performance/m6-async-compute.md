# M6.4 GPU 多队列验证与测量

日期：2026 年 10 月 29 日。调度、完成点、失败事务与时间域契约见 [异步 Compute](../async-compute.md)。首版只把索引清零/剔除的 Buffer feeder 放到可选 Compute Queue，默认关闭。

## 环境与复现

Windows x64、MSVC 19.51 / CMake NMake、NVIDIA GeForce RTX 5060；Debug/Release 构建树为 `out/build/m5-nmake`、`out/build/m5-release`。DX12 使用原生 COMPUTE queue；Vulkan 使用 Graphics family 0 的第二条 queue，没有跨族所有权转移，也不声称专用硬件。Vulkan 最终构建启用 `VK_KHR_calibrated_timestamps`，保证已计时队列使用共同 DEVICE 时间域。

最终性能套件 `out/m64-benchmark-clock`：Release、validation off、CPU profiling off、GPU profiling on、257×193 Deferred、固定相机。每次预热至少 4 帧与 0.25 秒，测量至少 2 秒，每配置重复两次。比较 64/4096 PBR 候选、128 mesh object 的 Shadow 场景，1/4 个 CPU 录制线程与单/双队列。Shadow 仍含透明 fallback，物体数不是索引批次候选数。

```powershell
ctest --test-dir out/build/m5-release -R 'recording.contracts|rendergraph.contracts|indexed_scene' --output-on-failure
python tools/benchmark/run.py sample/benchmark/async-compute.json --exe out/build/m5-release/renderlab_player.exe --output out/benchmarks/async-compute
```

示例默认使用 `windows-release` 的生成场景；本机实际配置为 `out/m64-suite.json`。输入、二进制、Shader、样本、报告、截图 hash 和每次完整指标见 [summary.json](m6-async-compute/summary.json)。sourceRevision 是父提交，trackedChanges=true，实际二进制由 SHA-256 标识。

首轮完整套件 `out/m64-benchmark` 未启用 Vulkan calibrated 时间域扩展，不能证明跨队列时间戳可比；整轮排除，原始数据与排除理由保留在 summary 中，不混入最终数字。Shadow 256-object 验证曾触及既有 Vulkan 256 descriptor-block 上限，失败日志保留为 `out/m64-release-history-tests.log`，未扩大容量；最终使用 128-object fixture。

## 正确性与窗口验证

Debug/Release 全目标构建成功，所选 CPU/CLI 与双后端专项各 26/26；时间域修正后各追加 6/6。最终 Shadow128 专项在两配置双后端通过。Python Benchmark 校验 15/15。GPU 测试要求 validation errors=0，场景销毁后资源账本为 0。

覆盖单/双队列 × 1/4 线程像素与时间戳、实际提交事件，1/64/65/1024/4096 候选的 GPU 可见 ID 逐项等于 CPU，连续在途相机版本、新 Renderer 的 TLAS + TraceRays，以及两个在途 progressive History 的最终 3 spp 像素等于独立串行结果。CPU 注入覆盖首次入队前/后异常、排空再次失败、保活与禁止伪造完成点；这不代表真实设备丢失验证。大绝对时间戳合约检查整数差值、125 ns 区间和负起点。

Release 窗口 validation on、GpuCulled、异步、4 线程、640×360：两后端各 54 帧、10 次重建、10 次挂起，错误为 0。最终日志与报告在 `out/m64-window-clock`，Vulkan 明确报告共同设备时钟扩展。未完成人工可视检查、窗口长测、其他 GPU 或多视口验收。

## 原生 Capture 等待点

官方 RenderDoc 1.46 便携包放在忽略的 `out/tools`；临时 headless wrapper 链接生产 Release 库，没有增加生产捕获入口。原始 `.rdc`、转换 XML 和编译来源在 `out/m64-capture-final`，工具/捕获/source hash、原生队列与逐项提交见 [capture-summary.json](m6-async-compute/capture-summary.json)。两后端捕获内部单/双队列像素相等，记录了四个 CPU 录制线程。没有执行图形 Replay；捕获内耗时不计入性能结果。

- DX12：Direct queue 8、Compute queue 402；Compute 先等前帧 fence 279=12。Graphics prologue signal 405=1，Compute wait 后执行并 signal 404=2；独立 Shadow 在 Graphics 上无 Compute 等待，消费者 wait 404=2，尾部发布 frame fence 279=13。
- Vulkan：family 0，queue 0=91、queue 1=92。submit 464 的 Graphics prologue signal timeline 410=1；465 的 Compute 等 410=1 和 frame 93=12 后 signal 411=2；466 的独立 Graphics 无等待，467 的消费者等 411=2，468 的尾部汇合后 signal frame 93=13。

Capture 说明等待确实落到原生队列。它不能单独证明硬件执行重叠；重叠依据下面的共同时间域测量。

## 性能与默认策略

最终 48 次运行、32394 个测量帧通过完整性校验，六组后端/场景中各八张截图逐字节一致。表中为两个重复均值，单位 ms，箭头为单队列 → 双队列；Overlap 是双队列已计时 Pass 区间交集。GPU span 包含已计时 Pass 之间的依赖/空闲，不是 GPU Pass 时间总和。

| 后端 | 场景 | 线程 | 帧时间 | GPU span | Overlap |
| --- | --- | --- | --- | --- | --- |
| DX12 | 64 | 1 | 1.0035 → 1.3186 | .0419 → .1076 | .000871 |
| DX12 | 64 | 4 | 1.2371 → 1.5019 | .0455 → .1155 | .000849 |
| DX12 | 4096 | 1 | 22.1342 → 18.5112 | .9018 → .9624 | .002670 |
| DX12 | 4096 | 4 | 18.3327 → 18.7102 | .9003 → .9691 | .002687 |
| DX12 | Shadow128 | 1 | 6.5340 → 6.7851 | .1310 → .1811 | .008165 |
| DX12 | Shadow128 | 4 | 6.4295 → 6.7936 | .1330 → .1735 | .004803 |
| Vulkan | 64 | 1 | .8602 → 1.5781 | .0451 → .2389 | 0 |
| Vulkan | 64 | 4 | 1.1695 → 1.5302 | .0490 → .2471 | 0 |
| Vulkan | 4096 | 1 | 22.8726 → 18.1904 | 1.0088 → 1.2126 | 0 |
| Vulkan | 4096 | 4 | 18.0725 → 18.8742 | 1.0098 → 1.2156 | 0 |
| Vulkan | Shadow128 | 1 | 6.2422 → 6.1251 | .1382 → .3121 | 0 |
| Vulkan | Shadow128 | 4 | 5.9911 → 6.1665 | .1414 → .3078 | 0 |

所有配置 GPU span 增加。Vulkan 已计时 Pass 没有重叠；DX12 交集约 0.8–8.2 μs，不能排除校准误差或极小区间影响，不据此宣称可靠的有效重叠。1 线程大场景帧均值下降远大于 GPU 时间变化，4 线程对应配置没有收益，不能把下降归因于异步。

CPU 场景准备仍主导，更多原生上下文、提交和等待有成本，默认保持 `asyncCompute=false`。这是短时 headless 串行运行，GPU 探针本身会影响调度，不推广为窗口 FPS、长期吞吐或其他硬件的结论。首版不支持任意 Compute 图、纹理跨队列、独立 Copy Queue 或跨 family 所有权转移。
