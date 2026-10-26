# RenderLab 阶段实施计划

本文件是实施状态的唯一记录入口。[architecture.md](architecture.md) 定义长期架构，[design-review.md](design-review.md) 解释本轮复查的取舍。阶段按可运行结果验收，只有设计或代码存在都不足以认定完成。

## 当前状态

- 当前实施阶段：M0–M4、R1、B1、B2 DONE。
- 当前工作项：B3.2 DONE，下一项 B3.3 描述符分配复用；M5 NOT_STARTED。
- 用户约定：每个子阶段验收完成即提交 Git，无阻塞时在当前授权范围内继续推进；M4.3 提交 c50ee6a、M4.4 提交 40d0c12、M4.5 提交 9a4b2e4。
- Demo 的 Custom 示例作为回归入口保留；M4 场景示例使用 Forward 组合与现有 Shader/Material 链路。

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
| B3 | 逐帧资源复用优化 | B2 | IN_PROGRESS |
| M5 | Deferred 与光追基础 | M4 | NOT_STARTED |
| M6 | GPU Driven 与执行优化 | M4；光追调度测试另需 M5 | NOT_STARTED |
| M7 | C# 编辑器与完整项目工作流 | M4；默认在 M5、M6 的核心目标后开展 | NOT_STARTED |
| M8 | 一个深入的渲染专题与作品交付 | 所选专题依赖已满足 | NOT_STARTED |

默认顺序为 M0 到 M8。M5 和 M6 在 M4 后可以独立推进；单阶段内按下列子阶段顺序执行，不同时建设多个高级系统。完整编辑器后置，M4 只实现运行内容所需的最小项目与资源能力。

R1 契约修订、B1 样例整理/Benchmark 和 B2 调查已完成。本轮已授权实施 B3 优化方案，逐子阶段验证与提交；M5 不在本轮实施范围。

## 状态与维护规则

状态使用 NOT_STARTED、IN_PROGRESS、DONE、BLOCKED、DEFERRED。通常只保留一个 IN_PROGRESS 主阶段，并在当前状态标出具体子阶段。BLOCKED 必须写明阻塞条件与恢复所需事项；DEFERRED 表示明确推迟，不能算作完成。

开始工作时更新当前阶段和工作项，按本次用户授权推进。任务复选框只表示子项完成，主阶段必须满足全部退出条件。代码未运行、测试未执行、硬件功能未验证时，直接写明，不能把“应该可用”记为通过。

每完成一个子阶段，在同一次文档更新中：

1. 核对该子阶段的交付和验收，记下实际使用的构建配置、后端和设备。
2. 把已完成子阶段的详细待办替换为简短完成记录，保留结果、验证入口及限制。未完成的后续部分继续保留详细计划。
3. 将长期接口说明放入 docs，构建运行步骤放入 manual；计划只链接入口，不复制整份说明或测试日志。
4. 更新主阶段状态和下一工作项。整个主阶段验收后，把全部详细步骤压缩为一个完成摘要，保留稳定的阶段标题与编号。

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

结果：工程规范、C++20/CMake 固定依赖、日志/断言/原生窗口/参数，以及 DX12/Vulkan 设备与能力报告。

验证：RTX 5060 上 Debug/Release 各 9/9，双后端 Validation 错误为 0；Intel 显式设备选择和窗口生命周期通过。后续头文件依赖修复见 M3.6；光追仍仅查询能力。

入口：[基础模块](../docs/m0-foundation.md)、[构建运行](../manual/build.md)、[工程约定](../manual/conventions.md)。

## M1 最小双后端 RHI

状态：DONE，日期：2026 年 10 月 23 日。

结果：交换链与两帧上下文、真实完成点与录制保活、资源/上传/读回、不可变绑定与 PSO、光栅/Compute/间接绘制、Reversed-Z/sRGB、Marker/Timestamp。

验证：RTX 5060 上 Debug/Release 各 13/13，双后端 Validation 错误为 0；固定噪声逐字节参考、取消/退休、池与上传复用、resize/minimize/restore 和退出通过。后续标准目录回归见 M3。

边界：单 Graphics Queue、单 mip/layer、单流顶点、有限纹理格式、committed/dedicated 分配；没有 Alias、Bindless、设备丢失恢复或其他 GPU 的完整验收。完整约束见 [RHI](../docs/m1-rhi.md)，运行入口见 [构建手册](../manual/build.md)。

## M2 Shader 与材质数据链路

状态：DONE，日期：2026 年 10 月 23 日。

结果：.shader/.params 声明、DXIL/SPIR-V 编译和内容缓存、固定布局/反射/Schema、材质编辑保存与不可变 GPU 快照、有界 PSO 缓存及后台热重载/帧边界发布。

验证：RTX 5060 上 Debug/Release 各 16/16；最终追加的材质热重载专项双后端均通过，Validation 错误为 0。后续完整回归见 M3。

边界：无编辑器/文件监视器；SM 6.0/Vulkan 1.3、有限绑定/变体和数值布局；无资源数组、任意结构化元素或跨机器发行验收。入口：[Shader/材质契约](../docs/m2-shader-material.md)、[构建运行](../manual/build.md#shader-与材质工具)。

## M3 单队列 Render Graph

状态：DONE，日期：2026 年 10 月 24 日（含标准构建修复复验）。

结果：类型化版本句柄、名称资源声明、Raster/Compute/Copy Builder 与受限 Execute、RAW/WAR/WAW/剔除/拓扑排序/屏障、Import/Export、有界池；RenderFeature 实例、注册后 Create 依赖解析、Custom/Forward 组合及真实阶段快照。

验证：RTX 4060 Laptop GPU、MSVC 19.51、DXC 1.8，最终标准 windows-debug 完整 CTest 19/19；DX12/Vulkan 窗口直接启动与图像/噪声/Buffer/快照参考通过，验证错误为 0。原接口曾在 RTX 5060 上通过 Debug/Release 各 19/19；新接口未复验 Release 或其他 GPU，历史结果不替代新配置验收。

边界：单队列/单线程/单视图、单 mip/layer、单颜色附件与固定 Store；无 Alias、多队列、计划缓存或深度采样；可选逐 Pass GPU Timestamp 后由 M4.5 补齐。Feature 依赖显式声明，Full 写覆盖由作者保证；普通 Forward 目前消费已准备 DrawPacket，场景、Material 适配和光照属于 M4。

入口：[Graph API](../docs/m3-rendergraph.md)、[Feature/Create/Forward](../docs/forward-pipeline.md)、[构建与恢复](../manual/build.md#render-graph-与-feature)。产物位于构建树 graph、logs、Testing，不纳入源码。

### M3.4 简化 API 与普通 Forward 组合

已完成：名称查询、直接 Builder 与统一 Execute；Opaque/Transparent、只读深度和真实快照。接口与验收汇总见上述入口。

### M3.5 Feature 实例与流式管线统一

已完成：Feature 实例及名称连接、无序注册后 Create；依赖缺失/重名/环/阶段冲突与固定组合隔离均验证。

### M3.6 既有构建树依赖修复

已完成：定位 showIncludes 编码导致旧目标文件混用，自动检测前缀并完整重编译标准目录；依赖表与双后端启动已验证。本地 PDB C1902 用缓存中的 /Z7 处理，未写死共享预设；恢复方法见 [工具链](../manual/build.md#工具链)。

## M4 场景资源与基础 Renderer

状态：DONE，日期：2026 年 10 月 26 日。

结果：最小 Project/UUID/资源缓存、静态 glTF/GLB、可保存 Scene/Material、层级变换/相机/主方向光/CPU 剔除；普通 Shader/Material 驱动的 Lit/Unlit、HDR/ACES/IBL、单张方向光阴影、Masked 与排序透明；原生调试面板、Graph 纹理查看、逐 Pass GPU Timestamp、固定配置/性能报告和独立原生 Player。原 Custom/RHI 示例与项目 Copper Shader 保留。

最终验证：RTX 5060、MSVC 19.51.36257、DXC 1.9.0.5512，标准 windows-debug/windows-release 构建及完整 CTest 各 37/37（11 CPU/CLI、26 GPU）通过，DX12/Vulkan validation error 为 0。content/scene/material/graph/RHI 原有回归通过，场景/材质保存重载、窗口 STA/两帧提交/resize/minimize/restore/退出和泄漏检查均覆盖。独立安装目录 out/player-m45-validation 的双后端 headless 与各 4 帧窗口启动通过；Library 从安装内容重建。

图像：两配置各 16 张 PBR 参考图双后端逐字节一致，24 张 surface 专项图最大差异 2 个显示 code；透明线性混合→ACES→sRGB 与独立 CPU 参考误差不超过一个 code。1280×720 Release 基线中 Pbr 有 2 个像素相差一个 code，Shadow 有 19 个像素的局部 PCF 差异、最大 16 个 code，比较数据已保留，不以小尺寸结果代替高分辨率验收。

基线：两场景 × 两后端，Release/validation off、1280×720/Medium、预热 5 帧后各 120 帧。CPU render 均值 1.770–7.635ms，已测 GPU Pass 范围均值合计约 0.037–0.068ms，RHI 当前约 22.88–28.38MiB；进程 working set/private 和逐 Pass/min/max/count 见 [原始记录与范围](../docs/performance/m4-rtx5060.md)。GPU 范围合计不等于含同步/Present 的完整帧耗时。

边界：单 Graphics Queue、单视图、静态三角形、point-clamp、单 mip/layer；低分辨率 CPU IBL 与 LDR 环境源，无 .hdr/.exr；导数切线框架非 authored TANGENT/MikkTSpace 保证。单张阴影，无级联/完全稳定拟合；透明不投影，无相交透明/OIT/折射。项目 Shader 需显式提供 Shadow Pass 才投影；Sky、DepthPrepass、Bloom 按需增加。Vulkan 检查并启用 shaderDemoteToHelperInvocation，保留未消费顶点属性提示；DX12 保留优化 clear 值不匹配的性能提示，无验证错误。仅本 NVIDIA 设备完整验收，新面板的手动键鼠操作/其他 GPU 未验收；原始完整 PBR Debug DX12 交互已由用户确认恢复。

入口：[场景/资源](../docs/m4-scene.md)、[PBR/HDR/IBL](../docs/m4-forward-pbr.md)、[阴影/Alpha](../docs/m4-shadows-alpha.md)、[工具/Player](../docs/m4-tools.md)、[构建运行](../manual/build.md#m4-项目场景)。构建树 pbr、surface、tools 保存图像/Graph/日志/隔离资产与 backend-comparison.json；各配置 Testing/Temporary/LastTest.log 保存最终完整验收。

### M4.1 最小项目与资源

DONE（2026 年 10 月 24 日）：Manifest/挂载根、UUID/.meta/子资源/依赖移动、静态 glTF/GLB/WIC 与可重建缓存；用户材质与导入产物分离。content.contracts 持续回归缓存、身份、依赖和非法内容；PBR/Alpha 导入在 M4.3–M4.4 补齐。

### M4.2 场景与 GPU 数据

DONE（2026 年 10 月 24 日）：层级 Scene/相机/方向光、值快照/剔除/历史/法线与镜像、Material→Forward/Graph；项目 Shader 共用内置 Core/Lighting。相机俯仰使用局部右轴，主光按稳定 UUID 选择；最终 scene/content 与双后端窗口回归覆盖。

### M4.3 Forward PBR

DONE（2026 年 10 月 26 日）：Lit/Unlit、Surface/Math/BRDF、MR/Normal/AO/Emission、RGBA32F HDR/曝光/ACES 与单次 sRGB、确定性预滤波图集和 split-sum LUT；Pbr.rlproject 固定材质球参考。最初 Debug/Release 各 25/25，COM/窗口修复后各 27/27，最终完整回归见本阶段汇总。

交互修复：窗口宿主持有完整生命周期的 COM STA，WIC 解码复用调用者 apartment；用户在 VS Code 终端重跑原始 Debug DX12 完整 PBR 命令，确认蓝圈与交互问题消失。另修复 generation 未驻留 PSO 导致退休帧后重复创建的问题，保留独立生命周期回归。

### M4.4 阴影与透明

DONE（2026 年 10 月 26 日）：BeforeOpaque ShadowFeature、RGBA32F 记录 Reversed-Z 深度与 D32 可见性、3×3 PCF/接收平面修正、相机视野外投影；Lit/Unlit Masked/Transparent variant、glTF alphaMode/alphaCutoff 与 v3 缓存、HDR straight alpha/只读深度/排序。Shadow.rlproject 为固定参考；子阶段 Debug/Release 各 32/32，最终完整回归见汇总。

### M4.5 可重复演示

DONE（2026 年 10 月 26 日）：同线程 modeless Win32 面板、F1、冻结/重置相机、曝光/阴影质量、最终/HDR/阴影/环境/LUT 查看；Graph 可选逐 Pass Timestamp，64 slot/最多 32 个存活 Pass，完成后读取。固定宽高/质量/项目与 5 帧预热测量，独立 Player 与安装目录，共用现有宿主和渲染流程。

补充修复：Graph/Pool 改用真实 FormatBytes 估算，修正 HDR 按 4 字节低估的问题；SceneRenderer 的池上限明确设为 64 项/224MiB，RHI 仍独立检查 256MiB。CPU 回归覆盖格式预算/溢出/未完成计时读取；双后端验证连续提交的 2048² 阴影、计时不改变画面、查看器图剔除和控件命令/布局边界。最终 Debug/Release 各 37/37 与固定基线见汇总；不把自动控件测试当作手动 UI 验收。

## R1 Shader/Material 契约修订

状态：DONE，日期：2026 年 10 月 26 日。

结果：Pass 用途、材质分类、固定功能状态、静态轴与 GPU 参数接口分别声明；Shader 名称/路径与 HLSL symbol 不再作为宿主行为约定。保留固定布局、不可变快照、整代热重载、有界 PSO 缓存与原 M4 渲染链路。

最终验证：RTX 5060 / MSVC 19.51.36257 / DXC 1.9.0.5512，标准 windows-debug/windows-release 构建与完整 CTest 各 40/40（12 CPU/CLI、28 GPU），DX12/Vulkan Validation 错误为 0。覆盖真实状态像素、生成 C++ 契约编译、静态程序选择、改名等价、材质保存/迁移、热重载与退休、M4 PBR/HDR/IBL/阴影/透明、窗口生命周期和工具。自动窗口验证不等于本轮手动键鼠验收，其他 GPU 未完整复验。

独立安装：`cmake --install out/build/windows-release --prefix out/r1-player`，从安装目录分别用 DX12/Vulkan、validation on、257×193/Low、headless benchmark 3 运行 project/Pbr.rlproject 和 project/Shadow.rlproject，四次均成功，Validation 错误为 0；引擎 sidecar 随包安装，Library 从部署内容重建。日志/报告见 out/r1-player/validation，完整测试记录见各配置 Testing/Temporary/LastTest.log；该短跑不作为新性能基线。

限制：每 Pass 最多 64 个静态组合，固定 Material 布局、单 Graphics Queue；组合深度模板附件保守跟踪，无深度模板采样/复制或独立平面调度。材质 v1/旧具名 variant 语法明确拒绝，无自动迁移器；无 sidecar 的工具临时 ID 随路径改变。Schema 为后续 Inspector 提供信息，尚无材质编辑器 UI。M4 既有 IBL/阴影/透明与硬件边界保持，M5 不在本轮实施范围。

设计与接口：[设计理由](shader-material-v2.md)、[渲染状态](../docs/render-states.md)、[静态轴](../docs/shader-features.md)、[参数接口](../docs/shader-parameters.md)、[资产身份与迁移](../docs/shader-assets.md)。

### R1.1 类型化 Pass 与固定功能状态

DONE：Raster/Depth/Blend/Stencil、动态 StencilRef、可选附件格式与 D32S8 双后端映射；Graph 各面 Load/Clear/权限。类型化 role/代际 PassHandle、名义类型属性引用、有限配方与显式覆盖，Surface/Blend/AlphaClip 分离，队列优先排序；镜像只翻转 FrontFace。原子阶段 Debug/Release 各 39/39，最终回归见汇总；提交 a1147d5/f09c321。

### R1.2 类型化属性与静态轴

DONE：普通 bool/显式 uint32 enum 为 uniform，keyword 属性才生成每 Pass features/稳定 VariantKey；逻辑属性与 GPU 字段分离，静态值不占 cbuffer。材质序列化/工具/热重载只存同一份属性值，旧 variant 字符串与具名宏集合入口已删除。shader.contracts 与双后端 render_state/material 验证预算、精确组合、实际像素、固定布局、非法值和迁移；基础提交 65879ad，最终迁移随 R1.4 交付。

### R1.3 参数接口与显式 HLSL 库

DONE：Schema ID/version/type/布局 hash、模块绑定别名、生成 C++ POD/Pack 与纯类型 HLSL 头、代际 BindingHandle/外部资源语义。Scene 按包/Pass 缓存适配，View/Object/Lighting/Shadow 分块、Object 跨 Pass 复用；移除 Frame 逐字段匹配与资源 symbol 分支。库通过显式值/纹理参数求值，入口在裁剪前计算导数，Unlit 无 Lighting 接口。生成头真实编译、ABI/代际/范围检查与双后端改名像素回归通过；提交 6f20062/97c9059。

### R1.4 全量迁移与回归

DONE：Shader sidecar UUID、材质 v2、类型化 MaterialSemantic 导入桥；内置/项目 Shader、M2 工具和 M4 样例统一迁移，缓存生成器升级，旧入口明确拒绝。Schema JSON 输出属性语义。shader.contracts 覆盖 sidecar 依赖/身份冲突、语义类型/唯一性、v1 拒绝；gpu.*.surface 在 Shader 显示名、路径、Pass、属性 symbol、模块/资源别名与 HLSL 文件全部改名、camera/shadow 启用恒等 stencil 后像素一致。完整 Debug/Release 与独立安装验收均通过，结果和限制见主阶段汇总。

## B1 样例整理与可扩展 Benchmark

状态：DONE，日期：2026 年 10 月 26 日。

结果：清理一次性生成/迁移脚本，样例分类；按帧/时长的固定场景测量、独立预热预算、流式原始数据与版本化汇总，Python 配置驱动重复运行、严格聚合和 Matplotlib 图表。M5 不在本轮范围。

最终验证：RTX 5060 / MSVC 19.51 / DXC 1.9，标准 Debug/Release 构建与完整 CTest 各 41/41，双后端 Validation 错误为 0；Python 数据完整性、分位数、重复等权、来源/配置、预算与失败处理测试 12/12。Debug/Validation、1280×720/Medium 的 PBR 按时长双后端端到端通过；独立安装 out/b1-player-validation 的 Release/Validation、257×193/Low 按秒测量、默认 raw 路径与独立报告分析也通过，错误为 0。记录见各配置 Testing/Temporary/LastTest.log、out/benchmarks/b1-debug-smoke 和安装目录 validation。

实测：Release/validation off、1280×720/Medium/Final，PBR 两后端各 3 次、每次预热 ≥5 帧且 ≥2 秒、测量 ≥10 秒，交替顺序，共 6908 帧；原始样本与汇总一致、exe/Shader/项目源内容保持，完整清单/JSONL/报告/图在 out/benchmarks/b1-pbr-release。串行帧均值 DX12 14.2542ms、Vulkan 6.3030ms，重复间标准差 2.7365/1.4499ms；GPU Pass 合计均值 0.0435/0.0385ms。波动和长尾较大，未归因于 API 固有差异；数值与图的版本化证据见 [实测记录](../docs/performance/b1-pbr-rtx5060.md)。

边界：headless 逐帧等待完成点，无 Present/窗口帧重叠；CPU render、GPU wait、GPU 范围之和与墙钟吞吐各有边界，不互换。原始记录器内存随指标种类而非帧数增长，Python 为精确分位数加载样本；图表依赖可选、固定版本的 Matplotlib。相同 case 的来源/设备/预算/画面配置不一致时拒绝聚合。后台负载/电源/温度未受控，其他 GPU 与窗口流水基准未验收。

入口：[接口与格式](../docs/benchmark.md)、[运行手册](../manual/build.md#benchmark-与双后端分析)、[PBR 配置](../sample/benchmark/pbr.json)。

### B1.1 清理与目录整理

DONE：删除 out 顶层 18 个一次性 Python 脚本；28 个样例文件分类到 common/rhi/graph/material/scene，同步 include/CMake/文档。Debug 原有全量 40/40 通过；提交 3cc0ce2。

### B1.2 运行时测量与原始数据

DONE：按秒/按帧与双预热预算、独立 src/benchmark 流式 JSONL/在线计时统计、v2 报告及完成契约。CPU/等待/串行帧/Graph 子区间/逐 GPU Pass 分开，窗口仍沿用原有两帧流程。Debug/Release 全量各 41/41、按时长 PBR 双后端验证通过；提交 7222aff。

### B1.3 批量执行与分析

DONE：tools/benchmark 正式工具与 PBR 配置，顺序/交替后端及重复，完整进程清单/内容 hash 与失败保留；JSON/CSV/Markdown、逐次分位数与重复间差异、PNG/SVG 帧曲线/分布/Pass 图表。Python 12/12、实际六次 Release 运行、原始数据复核与图表检查通过。结果和限制见本阶段汇总。

## B2 双后端性能差异调查

状态：DONE，日期：2026 年 10 月 26 日。

结果：在同一原 Release 二进制上扩大 PBR 基线至每后端 8 次、5 秒预热/20 秒测量，共 16 次、39,808 帧；差距在 8 组配对都复现。增加默认关闭的 RHI CPU scopes/调用计数、CPU 报告和图表；完整 PBR/低分辨率/BRDF 剔除/原始较小场景各两后端 3 次，共 24 次、20,816 帧。主要测量合计 40 次、60,624 帧，保留所有停顿样本。

验证：RTX 5060 / MSVC 19.51 / DXC 1.9，标准 Debug/Release 完整构建和 CTest 各 41/41，双后端 Validation 错误为 0；Python 13/13，覆盖探针总时长与逐帧值一致性、开关配置不混用。另以 Debug/Validation、完整 720p PBR 在两后端开启探针，验证导出与调用计数；结果一致、错误为 0。探针关闭时不读取计时钟，不改变创建/执行/回收策略。

发现：扩展基线帧均值 DX12 13.4026ms、Vulkan 5.6008ms；CPU render 9.1965/2.4398ms，GPU Pass 合计 0.0435/0.0392ms。完整 PBR 每帧 23 次 Buffer/10 次 BindingGroup 创建；DX12 使用独立 committed Buffer 和每组新建 shader-visible heap，回收也耗费 CPU。典型原生 Fence 等待约 0.2ms，原 gpuWaitMs 的几毫秒大多在 Collect 回收。降低分辨率不减少这些创建次数，剔除场景绘制的 BRDF 查看将绑定组降至 1 个，较少对象场景降低至 11 Buffer/4 组，支持定位到逐帧资源生命周期。

限制与后续：短诊断出现过一次 Vulkan 严重回收停顿及其他波动，未删除运行；典型诊断图明确使用 3 次均值的中位数，性能比较仍以前述更长基线为主。探针为包含驱动/系统的墙钟区间，不能区分全部内核/调度原因，也不覆盖所有局部析构。CPU 策略优化（完成点保护的上传区、不可变材质数据复用、描述符空间/读回复用）需另行实现与在途/画面/性能验收；本轮没有将诊断代码冒充性能修复。

入口：[数值、图与定位理由](../docs/performance/b2-pbr-rtx5060.md)、[探针接口](../docs/benchmark.md#可选-rhi-cpu-分解)、[对照配置](../sample/benchmark/pbr-diagnostics.json)。完整原始/诊断组分别在 out/benchmarks/b2-pbr-extended 与 out/benchmarks/b2-cpu-diagnostics；正确性专项在 out/benchmarks/b2-probe-validation，完整测试在各配置 Testing/Temporary/LastTest.log。

## B3 逐帧资源复用优化

状态：IN_PROGRESS。B3.1–B3.2 DONE，B3.3–B3.4 尚未验收。

方案：[逐帧资源复用](resource-reuse.md)。拟按 B3.1 帧上传页、B3.2 不可变材质常量、B3.3 描述符分配复用、B3.4 读回/View 与回收收尾分别验证、提交；每步实测后调整后续优先级。不扩展到 M5/M6 的渲染算法、多线程或多队列。

退出条件：旧 CPU 快照、已录制未提交命令、双帧在途及 profiling 延后读取保持正确；稳定场景预热后消除目标路径持续的原生分配，内存有界；双后端画面/Validation 回归通过，并以相同口径的优化前后配对基线报告耗时、长尾与内存。未运行的专项保持未验收，不能用设计文档代替实现完成。

### B3.1 帧上传页

DONE：有界 UploadPool/Batch 以整页引用和完成点保护复用，取消不伪造提交；SceneRenderer 的帧/对象/Display 常量保留非零 offset/size，未改变 Shader 和 Graph 调度。Debug/Release 完整 CTest 各 41/41，含双后端 Validation、窗口/双帧/resize、取消后保留 CPU 快照与八批次后提交旧命令、页预算/非法 Seal 验收。Release PBR 三次短测量各后端均值 9.1022/2.9743ms；独立探针 Buffer 创建由 23 降至 10 次/帧，包含材质和 timestamp。两后端截图与 B2 对应 BMP 逐字节相同。短测量不替代最终长基线，数据在 out/benchmarks/b3-1-upload{,-probe}；[接口](../docs/resource-reuse.md)、[结果](../docs/performance/b3-resource-reuse.md)。

### B3.2 不可变材质常量

DONE：同 GPU 包代际下，按布局 hash 与完整打包字节缓存至多 64 个只读常量版本；外部块和纹理解析保持独立，淘汰不修改旧快照。Debug 全套 41 项中的 39 项首次通过，修正验收误将非材质块也要求复用的断言后，两后端材质重跑均通过；Release 全套 41/41。覆盖 70 次编辑淘汰、延后提交/重用旧 Prepared、纹理编辑和整代热重载；Validation 通过。PBR 三次短测量各后端均值 5.4230/1.2512ms；独立探针 Buffer 创建降至 1 次/帧（timestamp），两后端 BMP 与 B2 相同。原始组 out/benchmarks/b3-2-material{,-probe}，阶段二进制 b3-2-player.exe；短测量仍不代替最终配对长基线。

## M5 Deferred 与光追基础

每个子阶段独立验收。先共享数据和有限材质，避免同时开发任意 Shader 命中调度与完整积分器。

### M5.1 Deferred（可选组合）

- [ ] GBuffer 契约、深度重建、Deferred Lighting，透明继续 Forward。
- [ ] 验证同一材质的 Forward/GBuffer/Shadow 角色与固定布局。
- [ ] 比较同一受控场景的 Forward 和 Deferred，解释预期差异。

### M5.2 加速结构与 Ray Query

- [ ] BLAS/TLAS 尺寸查询、构建、scratch 与图访问声明。
- [ ] 静态网格复用 BLAS，Transform 变化更新 TLAS，正确管理在途版本。
- [ ] 用 Ray Query 实现独立 RayTracedShadow 或 AO Feature，共用实例索引，以 ShadowVisibility 等明确资源接入光栅照明。

验收构建后查询、对象增删/移动、相机变化以及不支持能力时的诊断。动态顶点与蒙皮先报告不支持。

### M5.3 Ray Tracing Pipeline

- [ ] RayGen、Miss、通用 Hit Group、ShaderTable 和 TraceRays。
- [ ] 通过 Mesh/Instance/Material 索引访问基础 PBR，不为每个材质资产生成命中程序。
- [ ] 明确显式纹理 LOD、Payload、ShaderTable 对齐和热重载重建规则。

验收在支持的双后端设备上运行同一最小不透明三角形场景；不能用 Ray Query 成功代替此项。

### M5.4 混合效果

- [ ] 选择独立光追反射或 GI Feature，声明 Depth/Normal/Material/AS 输入和 Radiance 输出；按算法由 Forward 表面预通道或 GBuffer 提供输入。
- [ ] GI 的追踪、时域重投影、空间过滤和照明合成按依赖分 Pass，保留无降噪参考，不作为照明内部隐式副作用。
- [ ] 按 ViewID 管理历史与版本，定义相机/场景/材质/尺寸变化的失效规则。
- [ ] 记录支持材质范围与真实 GPU 成本，保留纯光栅对照。

退出条件：Deferred、Ray Query 和 TraceRays 分别有验证入口；至少一个光追效果接入渲染流程。硬件未验证的后端明确记录阻塞，不能把主阶段直接标为全部通过。

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

## 已完成阶段

已完成：M0 工程与设备基础、M1 最小双后端 RHI、M2 Shader 与材质数据链路、M3 单队列 Render Graph 与普通 Forward。摘要保留在对应章节，本节只维护索引。
