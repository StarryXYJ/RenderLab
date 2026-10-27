# RenderLab 阶段实施计划

本文件是实施状态的唯一记录入口。[architecture.md](architecture.md) 定义长期架构，[design-review.md](design-review.md) 解释本轮复查的取舍。阶段按可运行结果验收，只有设计或代码存在都不足以认定完成。

## 当前状态

- 当前实施阶段：M0–M5、R1、B1–B3 DONE。
- 当前工作项：M5 运动反射稳定性修复已完成并验证，保留高光偏差/细节模糊边界；M6/M8 未启动。
- 用户约定：每个子阶段验收完成即提交 Git，无阻塞时在当前授权范围内继续推进。

| 阶段 | 目标 | 前置依赖 | 状态 |
| --- | --- | --- | --- |
| M0 | 工程与设备基础 | 无 | DONE |
| M1 | 最小双后端 RHI | M0 | DONE |
| M2 | Shader 与材质数据链路 | M1 | DONE |
| M3 | 单队列 Render Graph | M2 | DONE |
| M4 | 场景资源与基础 Renderer | M3 | DONE |
| R1 | Shader/Material 契约修订 | M4 | DONE |
| B1 | 样例整理与可扩展 Benchmark | M4、R1 | DONE |
| B2 | 双后端性能差异调查 | B1 | DONE |
| B3 | 逐帧资源复用优化 | B2 | DONE |
| M5 | Deferred 与光追基础 | M4 | DONE |
| M6 | GPU Driven 与执行优化 | M4；光追调度测试另需 M5 | NOT_STARTED |
| M7 | C# 编辑器与完整项目工作流 | M4；默认在 M5、M6 的核心目标后开展 | NOT_STARTED |
| M8 | 一个深入的渲染专题与作品交付 | 所选专题依赖已满足 | NOT_STARTED |

默认顺序为 M0 到 M8。M5 和 M6 在 M4 后可以独立推进；单阶段内按下列子阶段顺序执行，不同时建设多个高级系统。完整编辑器后置，M4 只实现运行内容所需的最小项目与资源能力。

R1 契约修订、B1 样例整理/Benchmark、B2 调查和 B3 资源复用已完成，均按子阶段验证与提交。

## 状态与维护规则

状态使用 NOT_STARTED、IN_PROGRESS、DONE、BLOCKED、DEFERRED。通常只保留一个 IN_PROGRESS 主阶段，并在当前状态标出具体子阶段。BLOCKED 必须写明阻塞条件与恢复所需事项；DEFERRED 表示明确推迟，不能算作完成。

开始工作时更新当前阶段和工作项，按本次用户授权推进。任务复选框只表示子项完成，主阶段必须满足全部退出条件。代码未运行、测试未执行、硬件功能未验证时，直接写明，不能把“应该可用”记为通过。

每完成一个子阶段，在同一次文档更新中：

1. 核对该子阶段的交付和验收，记下实际使用的构建配置、后端和设备。
2. 把已完成子阶段的详细待办替换为简短完成记录，保留结果、验证入口及限制。未完成的后续部分继续保留详细计划。
3. 将长期接口说明放入 docs，构建运行步骤放入 manual；计划只链接入口，不复制整份说明或测试日志。
4. 更新主阶段状态和下一工作项。整个主阶段验收后，只保留一个简短完成摘要及主阶段标题；删除子阶段清单、提交列表和逐轮数据，细节通过 docs、manual 与 Git 历史追溯。

完成摘要格式如下，尖括号仅为模板占位，不能作为真实验证结果：

```text
### Mx.y 子阶段名称
状态：DONE，日期：<实际日期>
结果：<已实现能力和关键限制>
验证：<实际命令或复现入口、后端、设备和结论>
文档：<接口或使用说明路径>
后续：<仍需跟进的问题或无>
```

主阶段压缩后仍保留交付摘要、完成日期、验证入口和未解决问题。不要再建立一份复制全部待办的历史计划；过程可由版本历史追溯。代码修改使已完成能力失效时，重新标记受影响阶段或登记明确的修复工作项，不能保留过时的 DONE 结论。

## 通用验收约定

- 公共 RHI、Shader 与基础渲染能力需要在 DX12 和 Vulkan 验证；可选能力按能力矩阵记录实际支持与运行情况。
- 基础功能使用 Debug/Validation 检查，性能结果使用注明配置的构建；不混用两类数字。
- 保留小型、稳定的参考场景，固定相机、曝光和随机种子。失败应可缩小到一个资源或 Pass。
- 测试针对行为和边界，例如写后读、延迟回收、缓存失效和图剔除；不为简单包装或文档修改新增测试。
- 未支持的 GPU 能力可以明确列为硬件阻塞或推迟，不能用其他后端的成功替代该项验收。
- 阶段内发现接口不合理时，先修订最小设计再继续，不堆叠兼容分支维持尚未发布的旧接口。

## M0 工程与设备基础

状态：DONE，日期：2026 年 10 月 23 日。

结果：C++20/CMake 固定依赖、日志/断言/原生窗口/参数，以及 DX12/Vulkan 设备与能力报告。

验证：RTX 5060 上 Debug/Release 各 9/9，双后端 Validation 错误为 0；Intel 设备选择和窗口生命周期通过。光追当时仅查询能力，后续工具链修复见 M3。

入口：[基础模块](../docs/m0-foundation.md)、[构建运行](../manual/build.md)、[工程约定](../manual/conventions.md)。

## M1 最小双后端 RHI

状态：DONE，日期：2026 年 10 月 23 日。

结果：交换链与两帧上下文、完成点与录制保活、资源/上传/读回、不可变绑定与 PSO、光栅/Compute/间接绘制、Reversed-Z/sRGB、Marker/Timestamp。

验证：RTX 5060 上 Debug/Release 各 13/13，双后端 Validation 错误为 0；固定参考、取消/退休、上传复用和窗口生命周期通过，后续全量回归见 B3。

边界：单 Graphics Queue、单 mip/layer、单流顶点与有限格式；无 Alias、Bindless 或设备丢失恢复。入口：[RHI](../docs/m1-rhi.md)、[构建运行](../manual/build.md)。

## M2 Shader 与材质数据链路

状态：DONE，日期：2026 年 10 月 23 日。

结果：Shader 声明、DXIL/SPIR-V 编译与缓存、固定布局/反射/Schema、材质保存与不可变 GPU 快照、有界 PSO 缓存及后台热重载/帧边界发布。

验证：RTX 5060 上 Debug/Release 各 16/16，追加的双后端热重载专项通过，Validation 错误为 0；后续契约修订见 R1。

边界：有限绑定/变体与数值布局，无编辑器/文件监视器或资源数组。入口：[Shader/材质](../docs/m2-shader-material.md)、[工具运行](../manual/build.md#shader-与材质工具)。

## M3 单队列 Render Graph

状态：DONE，日期：2026 年 10 月 24 日。

结果：类型化资源版本、Builder/受限 Execute、依赖/剔除/屏障、Import/Export、有界池，以及 Feature/Create/Custom/Forward 组合与真实快照；标准构建依赖修复完成。

验证：RTX 4060 Laptop GPU 上 Debug 完整 CTest 19/19，双后端图像/噪声/Buffer/快照和窗口启动通过，Validation 错误为 0；后续 RTX 5060 双配置全量回归见 B3。

边界：单队列/线程/视图、单颜色附件，无 Alias、计划缓存或深度采样。入口：[Graph](../docs/m3-rendergraph.md)、[Feature/Forward](../docs/forward-pipeline.md)、[构建与恢复](../manual/build.md#render-graph-与-feature)。

## M4 场景资源与基础 Renderer

状态：DONE，日期：2026 年 10 月 26 日。

结果：Project/UUID/资源缓存、静态 glTF/Scene/Material、层级变换/相机/方向光/CPU 剔除；Lit/Unlit PBR、HDR/ACES/IBL、阴影/Masked/排序透明，以及原生调试面板、纹理查看、GPU 计时和独立 Player。

验证：RTX 5060 上 Debug/Release 各 37/37，双后端 Validation 错误为 0；参考像素、保存重载、窗口/COM、独立安装运行通过。用户确认原始 PBR Debug DX12 交互恢复；高分辨率后端差异见基线记录。

边界：静态几何、单视图/mip/layer、低分辨率 CPU IBL/LDR 环境、单张阴影和排序透明；无级联/OIT，其他 GPU 与新面板手动操作未完整验收。

入口：[场景](../docs/m4-scene.md)、[PBR](../docs/m4-forward-pbr.md)、[阴影/Alpha](../docs/m4-shadows-alpha.md)、[工具/Player](../docs/m4-tools.md)、[基线](../docs/performance/m4-rtx5060.md)。

## R1 Shader/Material 契约修订

状态：DONE，日期：2026 年 10 月 26 日。

结果：类型化 Pass/渲染状态、类型化属性与显式静态轴、Schema 生成 CPU/HLSL 参数契约、代际绑定、HLSL 显式传参，以及 Shader UUID/材质 v2/导入语义统一迁移。

验证：RTX 5060 上 Debug/Release 各 40/40，双后端 Validation 错误为 0；真实状态/改名像素、生成契约、材质迁移/热重载、M4 和独立安装运行通过。

边界：每 Pass 最多 64 个静态组合、固定 Material 布局；旧格式明确拒绝，无编辑器、深度模板采样或独立平面调度，其他 GPU 未完整验收。入口：[设计与接口索引](shader-material-v2.md)、[参数契约](../docs/shader-parameters.md)、[资产与迁移](../docs/shader-assets.md)。

## B1 样例整理与可扩展 Benchmark

状态：DONE，日期：2026 年 10 月 26 日。

结果：清理一次性脚本并分类样例；固定场景按帧/时长测量、独立预热、流式 JSONL/版本化报告，以及 Python 配置批量运行、聚合与图表。

验证：RTX 5060 上 Debug/Release 各 41/41，双后端 Validation 错误为 0，Python 12/12；双后端按时长测量、独立安装及六次 Release 基线通过。

边界：串行 headless，不等同窗口流水吞吐；精确分位数分析加载样本，Matplotlib 可选。入口：[接口](../docs/benchmark.md)、[手册](../manual/build.md#benchmark-与双后端分析)、[实测](../docs/performance/b1-pbr-rtx5060.md)。

## B2 双后端性能差异调查

状态：DONE，日期：2026 年 10 月 26 日。

结果：扩大 PBR 基线与诊断场景，增加可选 RHI CPU 计时/调用计数，定位主要差距为逐帧 Buffer、描述符创建及回收；优化由 B3 完成。

验证：RTX 5060 上 Debug/Release 各 41/41，双后端 Validation 错误为 0，Python 13/13；40 次、60,624 帧测量保留全部样本并复现差距。

边界：墙钟探针非完整 CPU profile，不能确定全部系统/驱动停顿原因或归因于 API 固有差异。入口：[定位证据](../docs/performance/b2-pbr-rtx5060.md)、[探针](../docs/benchmark.md#可选-rhi-cpu-分解)。

## B3 逐帧资源复用优化

状态：DONE，日期：2026 年 10 月 26 日。

结果：帧上传页、不可变材质常量、双后端描述符、timestamp 读回和默认 View 有界复用；CPU 引用与完成点保护旧快照/在途资源，稳定 PBR 预热后目标原生创建归零。

验证：RTX 5060 上 Debug/Release 各 41/41，双后端 Validation 错误为 0，Python 13/13；32 次配对长测共 360,600 帧，32 张同后端参考图逐字节一致。DX12/Vulkan 串行帧均值由 14.36/5.71ms 降至 1.04/0.93ms。

边界：仍有偶发长尾，其他 GPU 与窗口性能未验收；不把串行延迟当窗口 FPS。入口：[设计](resource-reuse.md)、[接口](../docs/resource-reuse.md)、[结果与完整统计](../docs/performance/b3-resource-reuse.md)。

## M5 Deferred 与光追基础

状态：DONE，日期：2026 年 10 月 26 日。

结果：双后端 MRT/可采样深度、PBR GBuffer 与 Deferred Lighting；BLAS/TLAS build/update、Graph AS 用途与静态 BLAS/不可变 TLAS 版本；Ray Query 阴影、原生 RayGen/Miss/通用 Hit Group/不可变 ShaderTable/TraceRays。一次 GGX specular 反射通过索引 Mesh/Instance/Material 与有限纹理图集求值，追踪、ViewID 静止历史累积和 IBL specular 替换独立成 Pass。样例面板/CLI 可选 Forward、Deferred、Ray Tracing，并查看原始/累积反射；不兼容材质选择给出诊断并恢复原管线。

验证：MSVC Debug/Release NMake 构建。RTX 5060 上 Debug 所选 16 项通过（Core/Graph；双后端 AS/TraceRays、Ray Query Scene、反射参考、surface、deferred、rhi、tools_window），追加 ShaderTable 范围/尺寸边界后 Graph 与双后端 AS/反射 5/5。Release 双后端 AS/TraceRays 与反射专项 4/4；上述 GPU 验收 validation errors=0。独立窗口各 54 帧、10 次重建、10 次挂起，实际渲染三种菜单选择。反射覆盖确定性 raw、静止累积、相机/场景/材质/尺寸/照明失效、ViewID 隔离、空场景与在途版本。Release/validation off 的 1280×720 短测各预热 64 帧、测量 240 帧，保留两后端三管线成本和参考图；不是长测或窗口 FPS 结论。

边界：静态不透明三角形；反射仅内置 Lit/Unlit，Masked 显式拒绝，透明走 Forward 且不进入 AS。三张 RGBA32F GBuffer 与浮点历史占用较高；有限图集为 64×64/显式 LOD 0，最多 62 个图像/色彩空间/文件版本；无动态几何/压缩、any-hit/callable、对象运动矢量、通用曲面反射重投影或自动光追 Shader 文件监视。自定义 Copper 需保留 Forward，原始资源不做隐式 PBR 映射。其他 GPU 未验证，WARP 探测未完成。Path Tracing 按 M8、Forward+ 按 M6 保留，未提前实施。

入口：[Deferred](../docs/deferred-pipeline.md)、[光追接口/生命周期/ShaderTable 重建](../docs/raytracing.md)、[样例与运行](../manual/build.md#ray-tracing-组合与-ray-query)、[GPU 成本/参考图](../docs/performance/m5-reflections.md)。独立验证入口分别为 `--pipeline deferred`、`--as-test`（Ray Query 与 TraceRays 分别记录）、`--scene-test --pipeline raytracing`；每个子阶段按验收提交，过程由 Git 历史追溯。

### M5 修复：反射收敛

状态：DONE，日期：2026 年 10 月 27 日。

结果：修正 64 帧后固定 1/64 EMA 的残留噪声，改为持续 1/N 平均；VNDF 与匹配 PDF 避免 GGX NDF 掠射角高权重，配合逐像素扰乱 Sobol 序列。达到 2^24 float 精确整数上限后保持结果，无提前回绕/EMA；面板和报告显示实际 spp/重置计数。保留 raw 单样本与原有历史失效规则，无光强/曝光削弱、亮度裁剪或降噪。

验证：RTX 5060，MSVC Debug 所选 16/16、Release 光追专项 4/4；双后端窗口各 54 帧/10 次重建，validation errors=0。新增 GPU 六组视角/粗糙度共 24576 次有限有界权重/分层检查，真实 Accumulate Shader 验证 reset、1/128、1/2048 和最大计数；130 帧历史增长及相机移动后零输入保持通过。固定场景 256→2048 spp 对 8192 spp 的 sRGB MSE 在两后端约下降 95.5%，方向光从 3 提高至 12 后约下降 95.2%；不是独立真值或任意场景完全无噪声保证。入口：[原因、图像与数值](../docs/performance/m5-convergence.md)、[接口/历史规则](../docs/raytracing.md)。

### M5 扩展：反射空间降噪

状态：DONE，日期：2026 年 10 月 27 日。

结果：Accumulate 后独立 5×5 空间降噪，主表面深度/法线/粗糙度/材质和本帧随机命中距离约束邻域；仅供显示、不回写历史。Final/HDR 默认开启，可由面板/CLI 切换，保留 Raw / History / Denoised。镜面旁路，高 spp 逐渐减弱至 2048 spp 完全旁路；开关/普通反射视图切换不重置有效历史。

验证：MSVC Debug/Release Sandbox/Player 构建。RTX 5060 上 Debug 所选 16/16、Release 光追专项 4/4，双后端额外 RT 窗口各 54 帧/10 次重建，257×193 Player 降噪诊断通过，validation errors=0。真实 GPU 合约覆盖 9×7 尺寸、常量/alpha、孤立噪点、镜面/高 spp、背景/Unlit、深度/法线与命中距离；场景覆盖滤波与历史独立及开关/视图保持 spp。两后端、两光强的 2/16/64/256/2048 spp 对未滤波 8192 spp 图像比较：16 spp 原场景 MSE 约降 64.8%、强光约降 54.2%，2048 spp 开关 RGB 逐字节相同。1280×720、预热 32/测量 128 帧单次短测，降噪 GPU 均值 DX12/Vulkan 0.248/0.222ms，不作为窗口 FPS/长测结论。

边界：有限空间邻域有低 spp 偏差与细节模糊，随机命中引导不能完整识别反射边界；本子阶段仅空间滤波，后续相机时间重投影见下；没有对象运动矢量或时间矩历史，其他 GPU 未验证。额外两张 RGBA32F 瞬态目标，关闭时仍写命中引导；未提前推进 M6/M8 其他能力。入口：[接口](../docs/raytracing.md#spatial-denoising)、[操作](../manual/build.md#ray-tracing-组合与-ray-query)、[图像、数值、成本与复现](../docs/performance/m5-denoise.md)。

### M5 修复：高分辨率 RT 分配预算

状态：DONE，日期：2026 年 10 月 27 日。

原因与结果：用户报告后在 Vulkan 1920×1080 RT 窗口复现原 256 MiB ledger 上限失败；先前 640×360 窗口验收未覆盖高分辨率。场景宿主改为显式 2048 MiB tracked native 上限，CLI 可配置 64..16384 MiB；独立 RHI Demo 仍默认 256 MiB。SceneRenderer 缓存上限为预算的 3/4，为图外资源留余量；预算错误包含 requested/used/limit，拒绝零值、低于当前占用或非 owner-thread 修改。没有压缩图像或改变降噪 Shader。

验证：MSVC Debug/Release Sandbox/Player 构建；Debug 所选 18/18、Release 光追/1080p 窗口 6/6，通过新增持续 1080p 16 帧回归和 CPU 预算合约。双后端另外在 1080p/1440p 各连续 32 帧（native 高水位约 365–872 MiB），原生 resize/minimize/三管线各 54 帧/10 次重建，Release Player 1080p 各 16 帧，validation errors=0，关闭后 ledger 归零。

边界：限额不是显存预留或驱动实时预算；更大分辨率/场景仍可能达到显式上限或硬件容量，其他 GPU/4K 未验证。入口：[接口](../docs/m1-rhi.md)、[操作](../manual/build.md#ray-tracing-组合与-ray-query)、[复现/内存数据](../docs/performance/m5-denoise.md#高分辨率窗口预算修复)。

### M5 扩展：反射重投影时间降噪

状态：DONE，日期：2026 年 10 月 27 日。

本条记录最初的主表面时间滤波；后续运动闪烁修复替换了其显示链路，当前契约与结果见下条修复。

结果：独立 Temporal Pass 在空间滤波后以当前深度和前帧矩阵重投影静态主表面，逐 bilinear tap 用世界位置/法线/粗糙度验证，配合随机命中距离/视角置信度、3×3 邻域 RGB 裁剪和最多 16 的像素历史长度。场景/材质/光照/投影/尺寸/ViewID 或大幅相机跳转失效；仅活动 View 持有两套三张 RGBA32F 引导，提交成功才发布。未滤波 1/N 与 Raw 保持独立，2048 spp 时淡出滤波；同场景相机运动继续采样序列，Raw 仍固定样本零。面板/CLI 可单独切回空间基线，来源帧计数不表示 GPU 接受像素数。

验证：MSVC Debug/Release Sandbox/Player 构建，DXIL/SPIR-V 编译。RTX 5060 上 Debug 18 项通过（1+17）、Release AS/反射/持续 1080p 窗口 6/6；补齐公共头显式 Include 后追加双后端 GPU AS/Temporal 2/2。真实 GPU 合约覆盖 9×7 尺寸、常量、噪声、源重置、整数/亚像素重投影、出屏/深度/法线/粗糙度拒绝、历史裁剪、长度上限与镜面/高 spp/背景/Unlit。两场景强光慢/快移动三模式终点对 2048 spp 参考；RayTracing 样例比仅空间滤波 MSE 再降约 21.5%/27.0%（不是全序列闪烁或任意拖影保证）。双后端 2048 spp 时间开关 RGB 逐字节一致。720p/validation off 短测 Temporal GPU 均值 DX12/Vulkan 0.219/0.133ms；1440p Player 各 32 帧 native 高水位约 1142/1141 MiB，257×193 诊断和窗口重建通过，validation errors=0，关闭后 ledger 归零。window smoke 渲染前会先变为 640/641×360，持续高分辨率由独立固定尺寸窗口验证。

边界：首版静态相机重投影；没有对象运动矢量、反射虚拟位置、时间矩/自适应采样或完整 SVGF/NRD。主表面重投影不完整表达反射运动，仍可能残余噪声/拖影与 HDR 裁剪偏差。六张持久 RGBA32F 增加 96 字节/像素，尚未压缩；其他 GPU/4K 未验证。入口：[契约](../docs/temporal-reflections.md)、[图像/误差/成本/复现](../docs/performance/m5-temporal.md)。

### M5 修复：运动反射白点闪烁

状态：DONE，日期：2026 年 10 月 27 日。

结果：参考 AMD 反射降噪、SVGF 和 NRD 输入契约，实现独立单帧去调制/稳定命中引导、接收表面与局部平面反射双候选重投影、时间亮度矩与最多 32 的有效历史长度、三尺度方差引导重建及材质 Resolve。逐像素诊断可选；普通帧仅用 1×1 诊断目标。两张版本化中间纹理显式交替，不增加通用内存 Alias。原始采样与 progressive 1/N 保持独立，高 spp、镜面等精确旁路；拒绝损失真实高光的输入亮度截断。

验证：MSVC Debug/Release Sandbox/Player 构建。RTX 5060 / NVIDIA 591.74 上 Debug Core 与双后端场景/窗口/RHI 等 17/17，追加 RenderGraph/双后端 AS 与新增 shader 合约 3/3；Release 双后端 AS/反射完整序列/持续 1080p 6/6。包含独立虚拟 UV、部分 bilinear 支持归一化、矩统计、去调制往返及旁路检查。强光慢/快/显露轨迹各移动 16 帧、停止 8 帧，逐姿态参考与终点参考：Release 使用 256/2048 spp，默认 Debug 使用 32/128 spp；修改前完整序列基线为 DX12 慢/快。当前慢/快平均 RGB MSE 从约 8.40/8.18 降到 5.48/7.04，残差差分 MSE 从 6.85/7.90 降到 .295/2.440；同时记录有符号亮度误差与逐帧图。资源复用前后 586 张 BMP 逐字节一致。两后端 257×193 高 spp 时间开关截图相同（测量帧 2048、额外截图 2049；shader 合约直接检查 2048）。1440p Player 各固定 32 帧 native 高水位约 1312.5/1311.5 MiB，低于默认 2048 MiB。上述 GPU/窗口 validation errors=0，关闭后 ledger 归零。720p/validation off 新重建六个 GPU Pass 合计约 .992/.862ms，短测非窗口 FPS。

边界：固定静态样例的运动稳定性改善，不保证任意场景消除闪烁。快速运动末段/停止后部分帧误差仍高于旧版，负亮度偏差有所增加；细节模糊、局部高光损失、反射遮挡与曲面拖影仍有取舍。没有动态对象运动矢量、完整 NRD/SVGF、辐射缓存或自适应采样。其他 GPU/4K 未验证。入口：[接口](../docs/temporal-reflections.md)、[设计依据](design-review.md#运动反射白点闪烁设计复查)、[完整序列/图像/指标/成本/复现](../docs/performance/m5-reflection-stability.md)。

## M6 GPU Driven 与执行优化

按 CPU 基线、GPU 提交、并行录制、多队列、Alias 的顺序分别评估。M6.1 与 M6.2 完成可构成最小 GPU Driven 成果，后续优化不要求一定提升所有场景。

### M6.0 Forward+（可选组合）

- [ ] 在普通 Forward 与场景光源数据基础上增加 Tiled/Clustered 光源分类 Compute Feature，声明深度、光源 Buffer、索引表和容量。
- [ ] Opaque/Transparent 显式读取分类结果，保留普通 Forward 基线；处理空光源、溢出、透明覆盖与不同分辨率。
- [ ] 相同场景比较图像、显存和 GPU 成本，不预设一定更快。

### M6.1 可索引资源与间接提交

- [ ] 让已有 GPU Scene 的对象、材质和纹理通过明确索引消费。
- [ ] 实现所需的描述符数组能力和在途安全，不建立无需求的全资源 Bindless 框架。
- [ ] 用 CPU 生成已知 Indirect Args，验证 DrawID/InstanceID/firstInstance 和 Count 映射。

验收结果与直接绘制一致。先隔离间接提交语义，再接入 GPU 剔除。

### M6.2 GPU 剔除

- [ ] Compute Frustum Culling、可见实例压缩、Indirect Args 和 Count。
- [ ] 图中声明清零、写入和 Indirect Read；CPU 只遍历有限 Pipeline 批次。
- [ ] 零物体、全剔除、全部可见、容量溢出有确定处理，禁止本帧同步读回数量。

验收与 CPU 剔除结果对照并测量不同规模场景。Hi-Z 和 LOD 后置，不能混入首个正确性样例。

### M6.3 CPU 并行录制

- [ ] 固化编译后执行计划、线程独立 CommandContext 和分配上下文。
- [ ] 录制只读的冻结访问计划与闭包数据，Shader/PSO 事先准备，绑定分配使用线程安全上下文。
- [ ] 保持单队列，比较串行/并行录制输出和 CPU 时间。

### M6.4 GPU 多队列

- [ ] 首先增加一个适合异步执行的 Compute 工作，再考虑独立 Copy Queue。
- [ ] 队列能力、所有权转移、提交完成点和跨队列等待。
- [ ] 正确消费导入上传资源，跨帧 History 带完整完成信息。

验收竞争资源、只读共享和连续帧情况，Capture 中能解释等待点。记录有无实际重叠和收益，不强制所有 Compute 异步化。

### M6.5 内存 Alias

- [ ] 在已有资源池之上增加 placed/alias 分配、兼容性与 alias 屏障。
- [ ] 基于真实队列 happens-before 关系判断重叠，不只看拓扑位置。
- [ ] 与关闭 Alias 的同一执行器比较画面、内存和时间。

退出条件：各子能力有独立开关和证据；GPU Driven 在双后端验证，调度与 Alias 正确性独立成立。优化没有收益时保留测量和默认策略，不为制造收益增加无效场景限制。

## M7 C# 编辑器与完整项目工作流

先做一条材质编辑工作流，再扩展资源面板。运行时规则继续由 C++ 服务维护。

### M7.1 桥接与视口试验

- [ ] C ABI、Handle 代际、字符串/缓冲区所有权和错误返回。
- [ ] Avalonia 原生视口托管，验证双后端 Present、DPI、resize、最小化和关闭。
- [ ] 输入焦点、捕获与视口内覆盖层，验证 Docking 方案的关键行为。

先完成此集成试验再投入完整 UI。若宿主技术需调整，仅替换编辑器宿主，保持 C++ 服务接口。

### M7.2 材质编辑与事务

- [ ] Schema 自动 Inspector、未保存材质预览、参数验证。
- [ ] Begin/Update/Commit/Cancel 编辑事务，连续拖动形成一条 Undo。
- [ ] UI 快照和命令队列不直接暴露 GPU 对象，编译事件显示错误位置。

### M7.3 项目与资源操作

- [ ] 创建/打开/关闭项目、资源树、搜索、导入进度。
- [ ] Move/Rename/Copy/Delete 调用统一资源服务，处理引用与冲突。
- [ ] Library 重建、损坏缓存恢复、编辑资源与外部变更冲突提示。

### M7.4 场景编辑

- [ ] Hierarchy、选择、Transform、拾取、Gizmo、组件属性与保存。
- [ ] Undo、dirty 状态、关闭未保存文档和重开场景。
- [ ] GPU Scene 随编辑正确更新，历史效果在必要时失效。

### M7.5 运行与构建

- [ ] 独立 Player 和内容处理，按配置收集资源与必需 Shader 变体。
- [ ] 从导出目录独立运行，不依赖原项目 Library 或开发机器路径。
- [ ] 原生崩溃保留诊断；首版不开发进程隔离和 IPC 恢复系统。

退出条件：创建项目、导入模型、从 Shader 创建材质、编辑场景、保存重开、导出运行全流程通过。

## M8 专题深化与作品交付

先选择一个主专题并写清目标、参考基线和必要输入，其他专题保持 DEFERRED，不默认全部实施。

候选为 TAA 与运动矢量、进阶光追反射与降噪、基础路径追踪、Hi-Z/LOD GPU Driven。Path Tracing 使用独立组合：场景/AS → Trace/采样 → History 累积 → 可选降噪 → ToneMapping；替代普通光栅主体，共用资源图和基础材质，不插在 Transparent 阶段。路径追踪先验证采样、BSDF/PDF、累积和收敛，再据测量决定是否建设 Wavefront 队列；降噪前保留原始结果作为正确性入口。

- [ ] 明确选择、支持范围与对照条件，拆成可验收的小步。
- [ ] 建立正确性参考，固定随机序列与相机并处理历史失效。
- [ ] 提供不同场景/规模的 CPU、GPU、内存数据与 Capture。
- [ ] 记录局限、误差和退化情况，避免只展示一个有利场景。
- [ ] 发布可运行演示、构建说明、操作说明、录屏和技术文章。

退出条件：所选专题完成且可复现，作品能由他人运行，讲解能从画面追踪到程序、资源和 GPU 执行。
