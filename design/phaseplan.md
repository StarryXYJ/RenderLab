# RenderLab 阶段实施计划

本文件是实施状态的唯一记录入口。[architecture.md](architecture.md) 定义长期架构，[design-review.md](design-review.md) 解释本轮复查的取舍。阶段按可运行结果验收，只有设计或代码存在都不足以认定完成。

## 当前状态

- 当前实施阶段：M3 DONE（含 M3.6 标准构建依赖修复）；M4 保持 NOT_STARTED。
- 当前工作项：标准 windows-debug 启动访问冲突已修复，头文件依赖检测和完整重编译已验证。
- 本次授权实现 Render Graph 简化 API 和普通 Forward 组合；Demo 使用空 Custom 组合，不注入 Forward 内置 Feature。Forward+、Deferred、光追、多队列及完整场景仍按后续计划推进。
- M1 已通过双后端三角形、固定种子 Compute 噪声及读回、同步、生命周期和 resize 验收，结果见下文。

| 阶段 | 目标 | 前置依赖 | 状态 |
| --- | --- | --- | --- |
| M0 | 工程与设备基础 | 无 | DONE |
| M1 | 最小双后端 RHI | M0 | DONE |
| M2 | Shader 与材质数据链路 | M1 | DONE |
| M3 | 单队列 Render Graph | M2 | DONE |
| M4 | 场景资源与基础 Renderer | M3 | NOT_STARTED |
| M5 | Deferred 与光追基础 | M4 | NOT_STARTED |
| M6 | GPU Driven 与执行优化 | M4；光追调度测试另需 M5 | NOT_STARTED |
| M7 | C# 编辑器与完整项目工作流 | M4；默认在 M5、M6 的核心目标后开展 | NOT_STARTED |
| M8 | 一个深入的渲染专题与作品交付 | 所选专题依赖已满足 | NOT_STARTED |

默认顺序为 M0 到 M8。M5 和 M6 在 M4 后可以独立推进；单阶段内按下列子阶段顺序执行，不同时建设多个高级系统。完整编辑器后置，M4 只实现运行内容所需的最小项目与资源能力。

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

结果：M0.0 仓库规范、M0.1 C++20/CMake 构建与固定依赖、M0.2 日志/断言/UTF-8/原生窗口和参数、M0.3 DX12/Vulkan 设备创建与 JSON 能力报告全部完成。core、platform、rhi 与 Sandbox 独立组织，尚未实现交换链或绘制。

验证：MSVC 19.51、Windows SDK 10.0.26100、CMake 4.3.1、Ninja 1.13.2；RTX 5060 上 Debug/Release 完整 CTest 各 9/9 通过，双后端严格 Validation 开启且错误数为 0。另验证 Intel Graphics 显式选择、无效索引、窗口缩放/最小化/恢复/关闭、头文件增量构建和无变更构建。自有 C++ 通过 clang-format 22.1.3 检查。

文档：[构建与运行](../manual/build.md)、[基础模块与验收](../docs/m0-foundation.md)、[工程约定](../manual/conventions.md)。原始报告与日志位于各构建树 reports、logs 和 Testing，不提交本机生成物。

限制与后续：M0 交付时不提交 GPU 工作，M1 已补齐完成点、在途回收与展示层；光追等高级能力仍仅查询。中文 MSVC 的首次配置需使用代码页一致的控制台，避免 CMake/Ninja 头文件依赖误解码；复现方式已写入构建说明。

## M1 最小双后端 RHI

状态：DONE，日期：2026 年 10 月 23 日（含维护性重构复验）。

结果：M1.1 交换链、两个独立 FrameContext、真实提交完成点、录制保活/取消/退休与原生命令上下文复用；M1.2 Buffer/Texture/View/Sampler、预算分配、两段上传环、显式屏障与读回；M1.3 不可变绑定、构建时双目标 Shader、顶点/索引绘制与 Compute；M1.4 坐标/矩阵/UV/绕序、Reversed-Z、线性混合/sRGB、资源名称、GPU Marker、Timestamp 和 16 字节 DrawIndirect 参数均已打通。RHI 不依赖样例顶点结构、固定入口名、材质或 Render Graph。

验证：MSVC 19.51、Windows SDK 10.0.26100、CMake 4.3.1、Ninja 1.13.2、DXC 1.9.0.5512；RTX 5060、NVIDIA 591.74 上 Debug/Release 完整 CTest 各 13/13 通过。DX12 Debug Layer 与 Vulkan synchronization validation 开启，错误数均为 0。每后端/配置窗口测试呈现 54 帧、重建/暂停变化 10 次、两次最小化恢复和单帧立即退出通过；正常帧无 DeviceIdle/QueueIdle。

读回与生命周期：宽 1/63/64/65/257、奇数行、非零 512 字节 offset 与 256 字节 pitch 的纹理/Buffer 往返通过；两段上传环跨六次提交正确；取消、提前释放应用引用、延迟提交、完成回收及场景退出占用归零通过。257×131 噪声的两次 Dispatch 经过 Storage→Storage 内存依赖，全部 33,667 个像素与 CPU 整数参考逐字节一致；四份双后端/配置 `noise.rgba` 的 SHA-256 均为 `ed59cd2517b5c5c076a93c45fda5a61aa1f6f43ff52f96af600bec1059be9527`。三角形、采样坐标、深度、混合、间接绘制与 Timestamp 探针通过；已查看读回图像。

工程验证：自有 C++ 通过 clang-format 22.1.3 检查，手工文本通过 UTF-8 无 BOM/LF 检查。WinPixEventRuntime 1.0.240308001 的 NuGet 正常下载、固定哈希和 CMake 集成已验证。原始图像/数据见各构建树 `acceptance/{dx12,vulkan}`，日志与 CTest 结果见 `logs` 和 `Testing`，不提交本机生成物。

维护性重构：公共 RHI 分为 resources、commands、runtime、swapchain、upload；双后端实现分为设备/能力、资源、绑定/管线、命令、展示与完成点，仍由同一 Runtime 维护提交所有权。场景准备/录制、验收、Shader 文件读取与 Sandbox 展示循环分离，错误检查复用，未增加状态管理层。`runtime.hpp` 从 460 行降到 83 行，DX12/Vulkan 的 `runtime.cpp` 分别从 752/1119 行降到 66/76 行。上述相同工具链和 RTX 5060 上重新通过 Debug/Release 各 13/13，验证错误为 0，四份噪声数据保持相同 SHA-256；25 个自有头文件逐一独立编译通过，双配置增量构建通过。文件职责见 [M1 文档](../docs/m1-rhi.md#文件职责与依赖)。

文档：[最小 RHI API 与约束](../docs/m1-rhi.md)、[构建与验收命令](../manual/build.md)。

限制与后续：最小分配采用 committed/dedicated，未做 VMA/D3D12MA 子分配；纹理限单 mip/layer，顶点限单流 float2/3/4，Storage 纹理限 RGBA8_UNORM，只有 Graphics Queue。Vulkan Host 内存要求 coherent、Timestamp 要求 64 个有效位。CPU 矩阵上传/真实相机、其他 GPU 的 M1、设备丢失注入和 PIX/RenderDoc 人工 Capture 尚未验证；测试中的 Shader 常量矩阵和原始计时不能替代这些事项。M1 交付时尚未进入 M2；现有 Shader 资产工具链、反射、材质与缓冲绑定扩展见 M2 完成记录。

## M2 Shader 与材质数据链路

状态：DONE，日期：2026 年 10 月 23 日。

结果：M2.1 `.shader` 唯一作者声明、`.params` 共享 cbuffer、普通 HLSL 工具 Include、独立 CPU DXC 双目标编译与内容缓存；M2.2 固定材质布局、完整参数元数据、PackingPlan 和双目标反射、Package 只读 Template；M2.3 逻辑材质创建/编辑/保存加载、有限具名变体、资源解析、不可变 GPU 快照与统一有界 PSO 缓存；M2.4 后台合并编译/丢弃旧代、事务准备、帧边界发布、失败保留旧代及完成点退休全部完成。RHI 补充 ConstantBuffer、ReadOnlyBuffer、StorageBuffer、四个逻辑 space 与显式 UniformRead/ShaderRead，不引入隐式资源状态管理。

验证：MSVC 19.51、Windows SDK 10.0.26100、CMake 4.3.1、Ninja 1.13.2、DXC 1.9.0.5512；RTX 5060、NVIDIA 591.74 上，隔离构建 `windows-m2-debug` / `windows-m2-release` 完整 CTest 各 16/16 通过。最终热重载图像行为测试追加后，两配置双后端材质专项各 2/2 再次通过。DX12 Debug Layer 和 Vulkan synchronization validation 开启，错误为 0，材质验收日志无警告，样例退出的 GPU 资源占用归零。

参数与编译：`shader.contracts` 覆盖源位置诊断、共享声明环、缺失 Include、未声明绑定、未使用字段保留、标量/向量/方阵/非方阵/数组、矩阵尾部接标量、单元素数组与矩阵数组、缓存命中/传递 Include 和模块失效/损坏恢复、材质值校验/保存加载/新增默认值与类型冲突、后台连续请求/失败恢复。`renderlab_shaderc` 实际导出 3 Pass、2 变体、5 项依赖的完整 Schema，第二次编译命中缓存。

GPU 与热重载：`gpu.{dx12,vulkan}.material` 验证同一材质多 Pass、参数/纹理/变体/状态变化的 PSO 复用或失效、sRGB、四个 scope，以及非零 256 字节 offset 的外部 cbuffer、结构化 SRV/UAV 和纹理 UAV。Compute 读回 20 个分量检查 float/int/uint/bool、float3、数组、非方阵和共享数据，重复写入经过 Storage→Storage。热重载修改工具 Include 后新代输出颜色减半，已录制旧代仍输出原色；类型冲突、缺失 Pass、编译失败不发布，恢复后输出恢复，旧 PSO 完成后释放，运行中数值编辑复用 PSO。

工程与回归：声明解析/布局/编译/反射/Schema/热重载和材质值/打包/序列化/GPU 准备分别编译，自有 C++ 已格式化，40 个头文件逐一独立编译通过。手工文本通过 UTF-8 无 BOM/LF 和 `git diff --check`。M1 全部回归保持通过，双后端/配置四份 noise.rgba 的 SHA-256 仍为 `ed59cd2517b5c5c076a93c45fda5a61aa1f6f43ff52f96af600bec1059be9527`。已查看材质默认色读回图像，产物保存在构建树 `material/后端`、`shader-tests`、`Testing`，不提交本机生成物。

文档：[Shader 与材质 API/语法/边界](../docs/m2-shader-material.md)、[构建与运行](../manual/build.md#shader-与材质工具)。

限制与后续：M2 为代码驱动的无窗口材质样例，不包含编辑器或操作系统文件监视器；变更侦测方通过依赖表请求重编译。仅 SM 6.0、Vulkan 1.3、16KiB 常量块、32 绑定、每 Shader 16 Pass/16 变体；纹理仍为单 mip/layer，Storage 纹理为 RGBA8_UNORM，结构化缓冲元素限数值标量或 2/4 分量向量。不支持资源数组、自定义结构体/三分量结构化元素、动态无界关键字组合、后台创建 GPU PSO 或强制中断正在执行的 DXC。当前缓存属于本机可重建产物；其他 GPU 和跨机器发行包尚未验证。M3 的后续接入见下文。

## M3 单队列 Render Graph

状态：DONE，日期：2026 年 10 月 24 日（含 M3.6 标准构建修复复验）。

结果：类型化图/资源/Pass 句柄、版本与初始化校验、RAW/WAR/WAW、根剔除、稳定拓扑排序、Before/After 和环诊断、自动屏障、Import/Export 完成点交接、有界资源池与计划诊断。公共 API 改为 Graph-local Create/Import/Get/Set/Export、直接 Raster/Compute/Copy Builder 和按值 Execute，内部统一分配、绑定、录制与提交。图不依赖具体 Shader/Material/Renderer。

### M3.4 简化 API 与普通 Forward 组合

状态：DONE，日期：2026 年 10 月 24 日。

结果：移除公共 PassData、PrepareContext 和多段执行组合；类型化 Context 按已声明 handle 绑定。名称只在建图时解析到最新版本，别名重定向不复制内容。Demo 继续直接组图。Renderer 新增独立 Opaque/Transparent Feature、普通 Forward 组合、阶段插入与可选真实颜色快照。Transparent 使用稳定距离排序、混合和只读深度；RHI 新增独立 depthWrite 与双后端纹理复制。

验证：MSVC 19.51、DXC 1.8，隔离 Debug 构建 graph-refactor-debug，NVIDIA GeForce RTX 4060 Laptop GPU；完整 CTest 19/19 通过，包括 CPU/CLI、双后端 Graph、Material、RHI、初始化、窗口 resize/minimize/restore 与单帧退出。DX12/Vulkan Graph 日志验证错误均为 0。普通 Forward 全部颜色字节与直接 RHI 基线一致，OpaqueColor 在透明后保持独立、AfterTransparentColor 与最终颜色一致。CPU 验证名称/版本、用途/范围、取消/完成保护、池复用，以及阶段构图、死 Feature 顺序、透明稳定排序、输入复制和只读深度拒绝写入。原 Noise SHA-256 和 Buffer 参考比较继续通过。

工程：完整自有目标 Debug 编译通过；27 个改动/新增 C++ 文件通过 clang-format，34 个改动/新增文本通过 UTF-8 无 BOM/LF 检查，git diff --check 通过。生成产物在该构建树 graph、logs、Testing，不提交本机路径配置。

文档：[Graph API/边界](../docs/m3-rendergraph.md)、[普通 Forward](../docs/forward-pipeline.md)、[运行命令](../manual/build.md#render-graph-与-feature)。

限制：当前仍为单队列、串行录制、单视图、单 mip/layer、单颜色附件和固定 Store；无 Alias、多队列、计划缓存或每 Pass GPU Timestamp。Full 覆盖是作者承诺。DepthRead 仍使用保守附件状态，实际写入由 PSO 禁止，不支持深度采样。普通 Forward 接收准备好的 DrawPacket，没有场景、Material 适配、光源或 PBR；这些属于 M4。此次未重跑 Release 或其他 GPU，不能沿用历史配置成功充当此次验收。

历史：M3.1–M3.3 于 2026 年 10 月 23 日在 RTX 5060 上通过旧 API 的 Debug/Release 各 19/19；该历史结果不代表新 API 的 Release 已验证。

### M3.5 Feature 实例与流式管线统一

状态：DONE，日期：2026 年 10 月 24 日。

结果：RenderFeature 保存稳定名称与显式 Feature 依赖，唯一 Build 负责声明 Pass。RenderPipelineBuilder 流式注册实例，Create 校验依赖、阶段并拓扑排序，不执行 Build/GPU 工作；注册顺序无关，无依赖时按名称确定顺序。生成的 RenderPipeline 固定持有实例组合，可跨帧 Build。Custom/Forward 仅预先注入不同 Feature，统一 Create/Build，没有独立执行逻辑。Feature 与 DrawPacket 都按资源名称连接，执行回调保存解析后的本帧 handle。

Demo：先注册 Scene 再注册 Noise，Create 依赖解析后正确建图；可选 Noise、Readback 也使用实例，不再串传输出 handle。Demo 使用空 Custom；普通 Forward 内置 Opaque/Transparent/快照也都是同一 RenderFeature。旧函数式 Feature 和闭包注册接口已移除。

验证：graph-refactor-debug 完整 Debug 构建和 CTest 19/19 通过；RTX 4060 Laptop GPU 上 DX12/Vulkan Graph 验证错误均为 0，Demo 与普通 Forward 图像、噪声、Buffer、快照和窗口回归保持通过。CPU 合约验证三 Feature 全部六种注册排列、Create 不提前 Build、缺失/重名/自依赖/循环、阶段冲突、Custom 与 Forward 同阶段语义及创建后的组合隔离。格式、UTF-8 无 BOM/LF 和 git diff --check 通过。

文档：[Feature 与管线组合](../docs/forward-pipeline.md)、[Graph](../docs/m3-rendergraph.md)、[构建运行](../manual/build.md#render-graph-与-feature)。

限制：依赖为显式 Feature 名称，不从资源或延迟回调自动反射；Graph 仍独立校验本帧资源访问。当前单线程 Build，Feature 的可变状态并发安全由调用方负责。本次未验证 Release 或其他 GPU。Forward+、Deferred、光追与完整场景仍在后续计划，没有提前实现。

### M3.6 既有构建树依赖修复

状态：DONE，日期：2026 年 10 月 24 日。

原因：标准 windows-debug 原 exe 启动四帧时复现 0xC0000005。旧 rules.ninja 的 showIncludes 前缀为 CP936，当前编译器输出 UTF-8，导致头文件依赖漏记；runtime.cpp.obj 早于 commands.hpp，但 commands.cpp.obj 已更新。新旧 Command variant 类型布局混用，在 shared_ptr 析构时访问错误内存。

修复：配置时由 cmake/MsvcIncludes.cmake 调用当前 MSVC 并以 UTF-8 解析实际依赖前缀，应用到 Ninja，不硬编码语言和前缀；修正前缀后对标准构建做完整重编译。已确认 Runtime、DemoGeometry、Graph Executor 的 Ninja 依赖记录包含 commands.hpp。旧 exe 曾被 VS 调试会话占用，停止调试后完成替换。

验证：标准 windows-debug、MSVC 19.51、DXC 1.8、RTX 4060 Laptop GPU；Visual Studio 自带 CMake 4.3 与独立 CMake 4.4 均配置成功。最终标准目录 Debug CTest 19/19 通过，DX12/Vulkan 各直接启动四帧正常退出且验证错误为 0。该结果验证用户实际启动目录，未以之前隔离构建替代。文本与差异检查通过。

本地限制：标准目录遇到 PDB 管理器 C1902，使用 Embedded 调试信息（/Z7）并清除缓存中的旧 /Zi 参数完成重编译；这些仅为本机构建缓存，未硬编码共享预设。未复验 Release 或其他 GPU。构建依赖与一遍完整重编译的恢复方式见 [构建手册](../manual/build.md#工具链)。

## M4 场景资源与基础 Renderer

目标是形成可展示画面和可保存内容，完整工程 UI 仍后置。

### M4.1 最小项目与资源

- [ ] ProjectManifest、引擎/项目挂载根、UUID 和稳定子资源 ID。
- [ ] 轻量 AssetDatabase、导入依赖、版本化缓存和资源重命名。
- [ ] 导入 glTF 的 Mesh、纹理、材质映射；手工材质覆盖不被重导入破坏。

验收清空 Library 后能重建、移动资源后引用保留、重复 UUID 有诊断。暂不建设通用流送系统。

### M4.2 场景与 GPU 数据

- [ ] Scene 保存加载、层级 Transform、Camera、MeshRenderer 和 Light。
- [ ] 渲染快照、Instance/Mesh/Material/Light 的稳定索引与必要 GPU 上传。
- [ ] CPU Frustum Culling 和直接绘制，保留当前/历史变换语义。

验收场景保存重开、实例增删与材质替换，Renderer 不读取正在修改的文档。

### M4.3 Forward PBR

- [ ] 内置 Lit/Unlit，基本纹理、Normal Map、Metallic-Roughness 与 IBL。
- [ ] 线性 HDR、曝光、Tone Mapping 和显示编码。
- [ ] MaterialSurface 与材质求值代码隔离照明，明确导数和纹理采样上下文。

用受控光源、材质球和测试纹理验证，建立后续光追共用的基础参数模型。

### M4.4 阴影与透明

- [ ] 先实现一种确定的方向光阴影；级联在需要时作为子任务增加。
- [ ] 将普通 Opaque/Transparent Feature 接入场景与材质，增加 Masked；按需加入独立 DepthPrepass/Sky Feature 和后处理阶段，定义快照位置。
- [ ] 可选 Bloom 等基础后处理，按实际画面需求增加，不先堆齐所有效果。

### M4.5 可重复演示

- [ ] 相机控制、原生调试面板、Texture Viewer 与 Pass Timestamp。
- [ ] 固定相机参考场景，支持运行参数选择后端和画质配置。
- [ ] 最小独立 Player 读取同一项目内容，不依赖 C#。

退出条件：同一项目在双后端得到合理一致的画面；场景与材质能保存重载；有初步 CPU/GPU 和内存基线。

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
