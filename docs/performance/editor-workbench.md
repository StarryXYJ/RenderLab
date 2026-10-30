# Editor 工作台验收与观察成本

2026 年 10 月 30 日；RTX 5060 / NVIDIA 591.74，MSVC Release、.NET 10 / Avalonia 12.0.5。阶段状态只见 [计划](../../design/phaseplan.md)。

## 四档观察短测

同一固定 RayTracing 项目、Deferred / GPU Culled、1060×809 原生像素，Validation Off、target FPS 0。每档独立进程预热 2 秒、测量 8 秒；JSON/Profiler 投影每 500 ms，不打开额外 Dock，因此未计入 Canvas 绘制成本。

| 后端 | 模式 | 样本 | Present-return FPS | 间隔 P95 ms | Native Tick P95 ms |
| --- | --- | ---: | ---: | ---: | ---: |
| dx12 | basic | 5944 | 742.90 | 1.700 | 1.697 |
| dx12 | timing | 6476 | 809.26 | 1.482 | 1.476 |
| dx12 | graph | 6407 | 800.73 | 1.550 | 1.533 |
| dx12 | readback | 5293 | 661.54 | 1.999 | 1.976 |
| vulkan | basic | 7766 | 970.69 | 1.229 | 1.227 |
| vulkan | timing | 7428 | 928.33 | 1.337 | 1.328 |
| vulkan | graph | 7687 | 960.69 | 1.271 | 1.258 |
| vulkan | readback | 6494 | 811.59 | 1.571 | 1.546 |

basic 为基础计数；timing 增加可选 RHI/GPU timing；graph 增加实际图快照与模型投影；readback 增加 GPU 可见数 Copy 和性能投影。八次运行均无丢样，关闭后账本/validation errors/worker_failed 为 0，实际配置与源 Session/配置身份一致。原始结果位于本机 `out/m72e/cost-{dx12,vulkan}-{basic,timing,graph,readback}`。

这是顺序单次短测，有热状态/调度噪声；例如 DX12 timing 比 basic 更快，不能解释为 instrumentation 带来性能收益。数字不是扫描输出或输入延迟，也不能推广为后端固有优劣；需要稳定结论时另做交错重复与长测。

## 集成边界

源 Scene/Material/Project 文件保持只读。人工画面、系统输入、日志故障/轮转、跨屏 DPI 与每后端 30 分钟记录由 M7.2f 追加；此处不提前认定通过。
