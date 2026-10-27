# Editor 窗口帧调度测量

2026 年 10 月 27 日，在 NVIDIA GeForce RTX 4060 Laptop GPU、驱动 610.47 的当前 Windows 桌面采集。Release/MSVC 19.51，.NET 10.0.401，DXC 1.8；固定 RayTracing.rlproject、Deferred、1077×960 客户区、144 DPI、GPU profiling 开启。每组预热 2 秒、采样 5 秒，串行运行，无输入。原始数据位于 `out/m7/baseline/<backend>-<on|off>/{performance,frames}.json`，out 不纳入 Git。

## UI Timer 基线

| 后端 | 验证 | FPS | 帧间隔 P95 ms | Native P95 ms | Render P95 ms | GPU Pass P95 ms |
| --- | --- | --- | --- | --- | --- | --- |
| DX12 | off | 26.02 | 82.00 | 3.72 | 3.00 | 3.06 |
| DX12 | on | 31.46 | 37.08 | 6.95 | 5.74 | 3.71 |
| Vulkan | off | 28.79 | 47.32 | 4.63 | 4.06 | 3.18 |
| Vulkan | on | 32.00 | 36.57 | 5.12 | 4.06 | 1.75 |

四组关闭均为资源账本剩余 0、验证错误 0。FPS 为成功渲染回调之间的间隔，不代表扫描输出；GPU 为已完成较早帧的 Pass 时间之和。帧间隔明显大于原生 Tick，是移出 DispatcherTimer 的依据；短测受桌面负载、预热与时钟影响，不能据此声称验证开启更快，或将全部间隔差归因于单一原因。

## 诊断与小开销修复

已有 250 ms 状态发布门独立于累计帧数，暂停间隔排除、重建清空历史，5 项 NUnit 回归通过。增加 SceneRenderer 在图 Compile 前的 CPU 场景/材质准备与建图计时，区别于 Graph 的 Prepare、Record 与整段 Render。C ABI v3 Stats 为 192 字节。

DX12 验证警告按消息 ID 保留首条，并在关闭汇总重复总数；最多记录 128 个 ID，超过上限的新 ID 继续输出。所有错误仍逐条报告。它减少日志 flush，未修正 optimized clear value 性能警告的根因，不能宣称零警告。Vulkan 的未消费顶点属性警告仍保留。

原生 Release、托管 Debug/Release 构建、5 项 NUnit、editor.input 通过。`out/m7/diagnostics/smoke-{dx12,vulkan}` 的 validation on/profile off 自动自检通过，各两次关闭均账本 0、验证错误 0。144 DPI 为本机固定缩放自动检查，未验证跨屏切换或人工画面质量。

## 独立渲染线程与高精度调度

UI 保留 HWND/输入和统计轮询，MTA 渲染线程拥有 Device、Runtime、SceneRenderer、Swapchain 与 GPU 等待；线程检查未放宽。普通定时等待的初版在同配置下约 48–58 FPS、P95 29–33 ms，因此改为 steady_clock 截止时间和 Win32 高精度 waitable timer，由邮箱事件提前唤醒处理输入/停止。

最终同项目、Deferred、1077×960、144 DPI、目标 60 FPS、GPU profiling on，仍为预热 2 秒、采样 5 秒、串行运行。原始证据在 `out/m7/scheduling/highres-benchmark-<backend>-<on|off>`。

| 后端 | 验证 | 样本 | FPS | 帧间隔 P95 ms | Native P95 ms | CPU 准备 P95 ms | GPU Pass P95 ms |
| --- | --- | --- | --- | --- | --- | --- | --- |
| DX12 | off | 301 | 59.99 | 17.67 | 4.00 | 2.80 | 0.22 |
| DX12 | on | 299 | 59.68 | 18.01 | 7.58 | 5.04 | 2.27 |
| Vulkan | off | 301 | 59.97 | 18.24 | 5.70 | 4.34 | 1.35 |
| Vulkan | on | 301 | 60.00 | 18.27 | 8.23 | 4.89 | 1.39 |

四组均无计时丢样，关闭剩余账本、验证错误、worker_failed 全为 0。Fence P95 为 0.55–0.96 ms，Present P95 为 0.18–0.83 ms。当前固定 Deferred 的主要 Native 工作在 CPU 准备/Render，原先较大的帧间隔不是由本次测量中的 GPU/展示等待独占；移除 UI 回调调度并改善定时等待后，帧间隔靠近目标周期。

基线记录 UI 渲染回调完成间隔，新版直接记录原生 Present 返回间隔，通过固定 512 项队列批量交给 UI。二者均不是扫描输出 FPS；新队列避免 UI 轮询漏采真实完成帧，并保留丢样计数。GPU 源帧、驱动时钟与短期桌面负载会波动，不将各组 GPU 数字之差解释为渲染算法吞吐提升；不同计时区间的 P95 也不能相加。

自动自检覆盖不同 UI/render thread ID、管线/配置确认、暂停确认后零新帧、静态光栅 Idle、曝光触发重绘、光追连续累积、六次连续尺寸变化至多两次 Swapchain 重建、重建与初始化中立即关闭。光追调度引用 `renderer::MaxReflectionSamples`，不把 2048 spp 显示滤波旁路误作累积结束。新计时的第六项 NUnit 回归验证 UI 批量交付不改变原生帧间隔，空闲间隔不计入 P95。

这些数据确认了该固定窗口场景的调度收益，没有进行长时间压力、设备丢失、全采样上限或所有光追场景性能验收；仍不宣称卡顿已完全消除。固定 150% 缩放与程序内合成输入不能替代跨屏 DPI、Docking、人工交互和画面检查。

最终原生 Release、托管 Debug/Release、6 项 NUnit 和 editor.input 通过；双后端 profile on 的证据为 `out/m7/scheduling/verified-*`，最终 profile off 的完整自检为 `out/m7/scheduling/profile-off-*`，每组两次常规关闭加一次初始化中立即关闭均账本 0、验证错误 0、worker_failed 0。另以 DX12/validation off/profile off/目标 30 FPS 预热 1 秒、采样 3 秒，得到 29.99 FPS、帧间隔 P95 35.40 ms、GPU P95 null、无丢样，证据 `out/m7/scheduling/target-30`。C++ clang-format 22.1.3、C# whitespace、UTF-8 无 BOM/LF 与 git diff --check 通过。

## 无上限模式

--target-fps 0 取消 CPU 限速，并通过 RHI 创建参数关闭强制 VSync；暂停、静态空闲与 Resize 等待仍休眠。DX12 使用 Present(0)，支持时允许 tearing，重建保留创建标志。Vulkan 优先 Immediate，其次 Mailbox，不能满足则明确报错，不静默落回 FIFO。默认启动依旧为目标 60 FPS。

同项目 Deferred、1077×960/144 DPI、validation off/profile on、预热 1 秒采样 3 秒的功能短测：DX12 186.96 FPS、P95 6.89 ms，Vulkan 225.72 FPS、P95 5.66 ms。无丢样，关闭账本、验证错误和线程失败为 0。期间存在构建活动，数值只用于说明已解除软件帧率/VSync 限制，不用于比较后端或声称稳定峰值；扫描输出仍受硬件/驱动/合成器影响。证据 out/m7/uncapped/benchmark-{dx12,vulkan}。

out/m7/uncapped/smoke-{dx12,vulkan} 的 target 0/validation on 自检通过，覆盖静态 Idle、连续光追、暂停确认、Resize 合并、重建和启动中关闭；6 项托管测试、editor.input、rendergraph.contracts 与 Debug/Release 托管构建、原生 Release 通过。
