# M4.5：演示、纹理查看与 Player

Sandbox 与 Player 共用 `renderlab_app_host` 的项目加载、原生展示、两帧完成点和 COM 生命周期。Player 是独立原生 exe，必须指定 .rlproject，拒绝 Sandbox 的 RHI/Graph/Scene 验收示例；不需要 C#。安装目录包含 exe、DXC/PIX 运行库、内置 HLSL 和相同格式的示例项目，Library 由运行时重建。

## 原生调试面板

`DebugPanel` 是与主窗口同线程、以主窗口为 owner 的 Win32 modeless 工具窗口。关闭面板只隐藏它；F1 显示/隐藏。宿主在帧间读取控件值，不从窗口过程录制渲染命令或修改 GPU 对象。主窗口失去焦点时清除移动按键，面板操作不会持续驱动相机。

面板提供最终画面/HDR/阴影/环境图集/BRDF LUT 选择，Scene/Low/Medium/High 阴影分辨率、主光阴影开关、曝光 ±0.5 stop、冻结与重置相机。重置恢复启动时的相机 TRS；控件调整仅改变当前运行，不写回场景资产。统计按约 250ms 刷新，显示已完成样本的 CPU render、Graph compile/prepare/record、可见/总实例、RHI 占用/高水位和各 GPU Pass 累计均值。窗口交互可能改变样本配置；性能基线应使用固定 headless 配置。

## 纹理查看

`SceneRenderer::Render` 最后的 `SceneRenderOptions` 可选参数包含：

| 字段 | 语义 |
| --- | --- |
| view | Final、Hdr、Shadow、Environment 或 Brdf |
| profile | 为本图的存活 Pass 开启 GPU Timestamp |
| shadowResolution | 0 使用场景值；其他值为 [128,2048] 的 2 次幂，仅覆盖提取快照 |

Final 保留原 HDR→曝光→ACES→sRGB。其他视图用普通 ToneMapping Shader 的预览分支，在同一 DisplayColor 附件显示选定资源：Hdr/Shadow/Brdf 直接显示 clamp 后的线性 RGB，Environment 使用曝光/ACES。所有视图只由 sRGB 附件编码一次，按源图宽高比居中，边缘填充暗灰，使用 texel Load 查看。

资源通过显式 Graph 读取连接；没有占位图像或图外偷读。切换到 Shadow/Environment/Brdf 时，未被消费的 Forward Pass 会正常剔除，计时仅列实际存活工作。当前查看器提供这些固定入口，不提供任意资源浏览、像素拾取、D32 采样或 mip/layer 选择。

## GPU Timestamp 与完成点

```cpp
graph.Compile({.profile = true});
auto submitted = graph.Execute(pool);
runtime.Wait(submitted.completion); // Offline measurement; a window collects retired frames instead.
for (const auto& timing : submitted.profile.Read(runtime))
    Print(timing.name, timing.nanoseconds);
```

RHI 命令列表具有 64 个 timestamp slot；WriteTimestamp 不允许越界或重复写同一 slot，ReadTimestamps 指定 1–64 个连续已写 slot，默认仍解析两个。读回容量必须足够。每个退休后复用的原生命令上下文拥有自己的 query heap/pool，防止覆盖在途结果。

Graph 为每个存活 Pass 写入起止各一个 timestamp，因此单次开启 profiling 的图最多 32 个存活 Pass，超限明确诊断；默认不开启。起点在本 Pass 屏障后、BeginRendering 前，终点在 EndRendering 后。最终解析和 HostRead 屏障由 Graph 录制，Submission.profile 持有读回缓冲、执行顺序名称、周期和真实完成点。Read 必须使用同一 Runtime，且完成点已退休；不会隐式等待或提前 Map。关闭 profiling 时不创建读回缓冲，Read 返回空结果。

逐 Pass 时间不包括 Pass 之间的屏障、查询解析、CPU 提交等待或 Present，因此相加不等于完整帧耗时。验证层开启的数值仅用于正确性回归。窗口在两帧上下文复用前收集已退休结果，不为读计时新增逐帧 WaitIdle；离线 headless 基线明确逐帧等待，以独立采集 CPU 准备和 GPU 范围。

## 固定配置与测量

命令行选择后端、项目、宽高、阴影质量和显示资源；Low/Medium/High 对应 128/512/1024，Scene 保留场景值。窗口可冻结相机；headless 始终使用固定场景相机。`--benchmark N` 先预热 5 帧，再测量 N 帧；资产导入、首次 Shader/PSO/纹理准备由预热吸收，最后单独读回参考图，不把截图拷贝加入测量。

JSON 记录真实设备/驱动/validation、启动配置、CPU render 和 Graph 各准备步骤、逐 Pass GPU 的 count/mean/min/max、RHI 原生分配的当前/高水位/预算，以及进程 working set/private commit（查询失败为 null）。CPU render 包含同步的场景提取、材质与 Graph 准备、录制/提交，计时不包含等待 GPU 退休。RHI 占用不等于进程或驱动总显存；进程内存也不是 GPU 分配的重复统计。窗口 JSON 标记 configurationMayChange，不将交互运行当成固定基准。

Graph 与 Pool 按 FormatBytes 估算纹理内容，RGBA32F 为 16 字节/texel；SceneRenderer 的 Pool 上限为 64 项/224MiB，以容纳两帧 2048² 阴影与 720p HDR/深度，RHI 仍独立执行 256MiB 原生预算。更高画面尺寸或更多资产可能触及预算并明确失败，不隐式无限扩容。

`core.contracts` 验证命令行和 64-slot 边界，`rendergraph.contracts` 验证 profile 容量及未完成读取，`gpu.*.surface` 验证计时不改画面、每个存活 Pass 有限时间与查看器剔除。`gpu.*.tools_window` 验证原生控件命令、布局边界、STA 与窗口重建；`gpu.*.player` 验证固定 headless 项目和 JSON/图像输出。实际结果见 [阶段计划](../design/phaseplan.md)，性能数据见 [M4 基线](performance/m4-rtx5060.md)。运行和安装命令集中在 [构建手册](../manual/build.md#m45-演示与-player)。
