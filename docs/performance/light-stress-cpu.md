# 多物体光照场景 CPU 排查

2026 年 11 月 12 日，Windows Release，RTX 5060，DX12，960×540，`LightStress.rlproject`，固定相机，validation off、CPU profiling on、5 帧预热和 30 帧测量。Forward 直接提交在阴影复用前/后，串行帧均值 29.42/25.32 ms，场景准备均值 20.25/16.22 ms，Graph 命令录制均值 7.42/7.48 ms；两次 BMP 的 SHA-256 都为 `ffc431963978340f652c2d65f22997928144bd0fbca90cd392df53e08b588841`。这是单次短测，不能推断窗口 FPS 或其他硬件性能，CPU profiling 会引入额外成本。

同样条件下未优化版使用 `--submission indexed-direct` 的串行帧为 21.01 ms，场景准备为 14.59 ms；这只是实验选项，不代表所有场景可用。DX12 的 Deferred 优化后图像与旧版本相同，混合管线因累积帧数不同未做像素等价结论。两后端的 `gpu.*.raster_area_light` 和 `gpu.*.area_light_reflection` 共 4 项通过。测试命令参照 [构建手册](../../manual/build.md#多物体光照压测)，添加 `--cpu-profile --validation off --benchmark 30 --warmup-frames 5`；验证时启用 GPU 测试并运行对应 CTest。

当前仍有大量单对象准备和绘制记录开销，不能认定卡顿已经解决。Vulkan 大场景描述符块预算耗尽、窗口交互、长时性能、其他硬件和并行点光源改动组合均未验证。

## CPU 细分探针（2026 年 11 月 12 日）

针对提交 `0be24f3` 的隔离构建，RTX 5060、DX12 Release、960×540、固定相机，关闭图形 API validation，启用 CPU profiling，预热 30 帧、测量 120 帧。测量使用临时源码计时点及逐帧日志，结束后已移除并重建；探针与日志增加了时间，数字不作为无探针性能基准。该提交包含逐实例多灯阴影准备复用，尚未包含并行点光源工作。结果和日志在 `out/cpu-detail/`，原生常规 benchmark 命令见上文。

| 管线 | CPU render | 场景准备 | Graph 构建中管线部分 | Graph 录制 | GPU pass 合计 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Forward | 26.75 ms | 17.52 ms | 10.47 ms | 8.21 ms | 0.51 ms |
| Deferred | 25.11 ms | 16.64 ms | 9.28 ms | 7.43 ms | 0.62 ms |
| 混合 Ray Tracing | 26.80 ms | 17.63 ms | 9.67 ms | 7.75 ms | 2.06 ms |

管线构建是场景准备的子集。Forward 中每帧 145 个方向光阴影、290 个面光源阴影和 145 个主画面绘制；逐对象准备约 5.38 ms，其中 290 次材质 Prepare 约 3.94 ms。进程累计 151 帧（含预热和捕获帧）中，材质 Prepare 共 602 ms，内含 Migrate 451 ms，约每帧 2.99 ms；进程累计可能包含初始化，非逐帧采样。

Forward 的 Graph 构建按 Feature 分解，面光源阴影约 4.33 ms、方向光阴影约 2.19 ms、主画面约 3.34 ms（另一次含更细函数探针的运行）。进程约 151 帧累计声明 138 万次资源访问，其中 136.7 万次命中同一 pass 已声明资源；用途校验累计约 640 ms，约 4.24 ms/帧。Graph 录制中资源解析累计约 372 ms（约 2.46 ms/帧），绑定校验累计约 177 ms（约 1.17 ms/帧）；RHI 绑定组创建约 1.78 ms/帧，原生命令录制约 0.22 ms/帧。这些子计时相互嵌套或来自不同次运行，不能相加当作完整分解；大量高频计时也会改变结果。

代码检查显示用途校验在成功路径上仍拼接诊断字符串，材质 Prepare 每次执行 Migrate；这是值得单独做受控改动和前后对照的候选，不是已验证的优化。验证行为和错误诊断必须保留。窗口交互、探针关闭的长时间测试、其他硬件及合入点光源后的性能均未测；Vulkan 大场景描述符预算限制仍在。

## Graph 热路径首轮优化（2026 年 11 月 12 日）

资源用途、冲突、名称和录制时句柄解析仍执行原有检查，仅在失败时构造详细诊断。RTX 5060 DX12 Release、960×540、validation 和 CPU profiling 关闭，每次预热 30 帧、测量 120 帧，两次按不同顺序执行。串行帧均值（ms）：Forward 32.87/29.29 → 16.72/17.17，Deferred 24.99/24.80 → 16.23/16.65，混合光追 26.93/27.14 → 19.19/19.32。Forward 首次基线波动较大；短测不代表窗口帧率。三管线前后截图 SHA-256 各自相同。Release Graph 契约及重链接后的 DX12/Vulkan 面灯光栅、反射回归 5/5；错误信息专项检查保留 pass 和资源名称。数据 `out/cpu-opt/{before,after,repeat-before,repeat-after}-*.json`。窗口、其他设备和并行点灯代码尚未验证。

## 帧内材质快照复用（2026 年 11 月 12 日）

在 Graph 热路径优化上，使用每帧、每种材质的不可变验证快照复用 `Migrate`；Shader 包与值绑定到快照，下一帧重新读取。与上轮相同 DX12 短测条件，Forward 串行帧均值 16.72/17.17 → 14.13/14.85 ms、Deferred 16.23/16.65 → 13.40/13.87 ms、混合光追 19.19/19.32 → 14.69/15.56 ms。前后非交替成对测量、仅两次短测，不能保证收益或代表窗口帧率；三管线首轮图片前后哈希一致。Release 双后端材质读取/编辑/Shader 热重载及快照隔离、面灯光栅与反射共 6/6；索引场景双后端 2/2 使用短输出路径直接运行通过。CTest 在工作树原路径执行索引测试时写入临时文件失败，短路径复验通过；原因未进一步确认。更多设备、长时交互和点灯组合未测，接口见 [资源复用](../resource-reuse.md)。

## Graph 固定诊断与绑定检查（2026 年 11 月 12 日）

在前两轮基础上为 Graph 字面量诊断增加无分配成功路径，绑定/缓冲范围检查仅在失败时拼接动态消息，保留检查与错误内容。Release Graph 契约及重链接后的 DX12/Vulkan 面灯光栅和反射 5/5。RTX 5060 DX12 Release、960×540、validation 和 CPU profiling 关闭，两次 30 帧预热/120 帧测量并反向执行顺序：Forward 帧均值 14.68/14.77 → 9.55/9.78 ms，Deferred 14.36/14.43 → 9.47/8.98 ms，混合光追 15.57/15.14 → 10.73/10.93 ms。各管线第一组前后截图哈希一致；Graph 录制 Forward 约 5.7 → 2.7 ms。日志 `out/cpu-opt/graph2-*`。这是短时 headless 串行对照；窗口、其他设备和并行点灯组合未验证。

后续试验：`BindBuffer` 已解析句柄后跳过范围检查内部的第二次解析，Release Graph 契约和双后端面灯回归通过。RTX 5060 DX12 Forward 两次交替短测，原版串行帧 9.015/9.078 ms，候选 9.019/9.080 ms，Graph 录制 2.645/2.659 → 2.632/2.624 ms；差异很小且不能证明整体收益，因此撤回候选，保留原有统一校验接口。

## 点光源整合与 Editor 验证（2026 年 11 月 12 日）

将 CPU 优化与 `172b1d0` 的点光源及路径诊断合并后，Release 原生目标及 Editor 构建通过；Graph 契约、DX12/Vulkan 材质与面光源 GPU 回归共 9/9。RTX 5060 DX12、validation on、960×540、10 帧预热/30 帧测量，八点灯场景 Forward、Deferred、混合和 Path Tracing 均运行完成，validation errors 为零；串行帧均值分别为 19.20、15.15、15.94、4.73 ms。验证开销、点灯和以前的测量条件不同，不能将数字直接比较。

实际 Release Editor DX12 Forward 打开八点灯场景，1017×464，validation off、profiling on、target-fps 0，预热 2 秒、测量 5 秒；555 个样本、无丢样，Native P95 10.55 ms、Render P95 10.03 ms、Scene Prepare P95 6.12 ms，报告的完成帧吞吐约 110.9 FPS，关闭报告的资源残留及 worker_failed 均为零。日志位于隔离工作区 `out/merge-validation/` 与 `out/merge-editor-forward/`。这不是用户现有 Debug Editor 的数据；动态交互、其他管线的 Editor 窗口、其他 GPU 与 Vulkan 大场景仍未验证。

## Editor 三管线与 Vulkan 描述符页（2026 年 11 月 12 日）

原 `dev` 的托管 Debug/原生 Release Editor，RTX 5060 DX12、1017×773、validation off、profiling on、固定相机、target-fps 0，预热 2 秒/测量 5 秒：Deferred 与混合光追的 Native P95 分别 8.60 和 8.73 ms，636/620 样本、无丢样，关闭资源残留与 worker_failed 为零。仅固定相机短测；窗口交互仍待验收。日志 `D:/Personal/RenderLab/out/four-step-editor-{deferred,raytracing}`。

Vulkan 使用按 BindingLayout 复用多组 set 的有界描述符页后，RTX 5060 Release、validation on、960×540、10 帧预热/30 帧测量，八点灯 Forward、Deferred、混合与 Path Tracing 均运行，validation errors 和关闭时跟踪内存为零；Forward、Deferred、混合分别创建 21、22、33 个原生池页。Graph 契约、双后端 RHI、描述符数组、面灯光栅与反射回归 9/9，另增加 300 个同时存活组、释放复用与预算耗尽契约，双后端 RHI 2/2。该页预算仍有上限。

隔离 Release Editor 的 Vulkan validation on、profiling on、1017×773、固定相机、target-fps 0、预热 2 秒/测量 5 秒，Forward、Deferred、混合 Native P95 分别 38.21、35.79、37.53 ms，153/161/161 样本、无丢样，退出账本零泄漏、零 validation errors、零 worker_failed。validation on 与上面 DX12 validation off 不可直接比较；Vulkan 性能仍待分解。其他 GPU、窗口交互、超过该场景的规模及长期运行未验收。证据分别为本工作树 `out/vulkan-pages/` 和 `D:/Personal/RenderLab/out/vulkan-page-editor-{forward,deferred,raytracing}`。
