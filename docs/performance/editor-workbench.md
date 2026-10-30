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

## 集成方法与日志故障

### 30 分钟实机结果

| 后端 | 实际秒数 | 观察帧数 | 记录数 | Native 采样峰值 MiB | RHI 关闭峰值 MiB | Managed 采样峰值 MiB | Private 采样峰值 MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| DX12 | 1800.87 | 53776 | 355 | 300.5 | 300.6 | 60.0 | 950.6 |
| Vulkan | 1800.98 | 53779 | 353 | 298.0 | 298.0 | 74.3 | 1100.7 |

两进程退出码均为 0，完成帧与原生日志丢样均为 0；每后端 4 份关闭报告的 RemainingBytes/ValidationErrors/WorkerFailed 全为 0，23 份源文件 hash 不变。历史最多 64 份，逐秒检查的 JSON 投影/日志/曲线预算均未超限；大快照期间会按 8 MiB 字节预算先淘汰历史，不能只检查条目数。日志行峰值分别为 90/86，洪峰由下述专门测试覆盖。两块实际屏幕均为 96 DPI，迁移前后原生像素尺寸匹配。

证据为 `out/m72f/soak-{dx12,vulkan}/{soak.json,soak-samples.jsonl}`，最终 ABI v10 自检为 `out/m72f/{final-release-dx12,final-debug-vulkan}`，各 23 项、30 次正常 Session 关闭与一次初始化中关闭均通过。长测没有执行 API 切换；长测启动后新增的取消事务后端标识同步另有最终状态机回归与集成自检，不能声称该取消路径经过了 30 分钟压力。

持续运行使用 `--soak-seconds 1800 --validation on`，每后端独立进程、30 FPS、GPU Culled；两进程并行，因此是稳定性检查，不能用作后端性能比较。每 300 秒切换 Forward/Deferred/RayTracing、timing/可见数读回/Alias/录制线程，每 45 秒切换诊断页，每 600 秒重建布局；每秒检查预算、约每 5 秒流式记录。最终帧数为宿主观察到的累计计数，不是显示器扫描数；内存高水位为采样值，进程 Private Bytes 包含驱动、Avalonia/Skia 等，不能与 RHI 账本相等。

原生 Core 用故障 `ostream` 验证 editor 文件写失败后镜像继续、严格宿主仍抛错。托管集成测试实际锁定日志文件，并用同名目录使文件打开失败且无文件落盘，验证两种情况下 SelfLog 可见且内存 sink 继续；生产配置写入约 185 MiB 触发 16 MiB × 10 文件轮转，10500 条洪峰后显示缓存仍受 10000 条/8 MiB 约束，Dispose 后文件保留末尾 sentinel。锁文件时 Serilog 允许改开序号文件，该用例不代表磁盘耗尽或目录权限失败。两配置各 22 项托管测试与原生 Core 合约通过，结果在 `out/m72f/tests`。

可见桌面检查实际看到了默认 Scene/Render Settings、固定场景球体/方块/地面与阴影，以及已恢复诊断布局下的 Profiler 曲线、Graph 矩阵和 Window 菜单；系统鼠标点击可切换菜单/诊断页。截图观察不等于用户画质验收。窗口最小化后停止界面输入；持续按键、右键拖动/捕获释放、完整浮动窗口的系统输入与人工画质仍需用户复验。已有自检覆盖合成输入、Dock 浮动/停回和 API 切换，不将其当作物理操作通过。

实际连接两个 1920×1080 显示器，均为 100%/96 DPI；验收程序移动根窗口并检查 DIP→原生像素。150% 和混合缩放尚未运行，也没有更改系统显示设置。设备丢失、其他 GPU、完整资源/场景编辑和任意 RT 像素回放均不在本轮实测覆盖内。源 Scene/Material/Project 文件只读并校验 hash。
