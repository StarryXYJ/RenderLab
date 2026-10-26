# RenderLab 阶段实施计划

本文件是实施状态的唯一记录入口。[architecture.md](architecture.md) 定义长期架构，[design-review.md](design-review.md) 解释本轮复查的取舍。阶段按可运行结果验收，只有设计或代码存在都不足以认定完成。

## 当前状态

- 当前实施阶段：M0–M3 DONE；M4 IN_PROGRESS，M4.1–M4.3 DONE。
- 当前工作项：M4.3 交付及窗口交互修复 DONE；用户确认完整 PBR 场景恢复交互，Debug/Release 完整回归各 27/27。M4.4 本轮不启动。
- 用户确认本轮先交付 M4.3 以检查基础着色；M4.4、M4.5 本轮未启动，不进入 M5 或编辑器。
- Demo 的 Custom 示例作为回归入口保留；M4 场景示例使用 Forward 组合与现有 Shader/Material 链路。

| 阶段 | 目标 | 前置依赖 | 状态 |
| --- | --- | --- | --- |
| M0 | 工程与设备基础 | 无 | DONE |
| M1 | 最小双后端 RHI | M0 | DONE |
| M2 | Shader 与材质数据链路 | M1 | DONE |
| M3 | 单队列 Render Graph | M2 | DONE |
| M4 | 场景资源与基础 Renderer | M3 | IN_PROGRESS |
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

边界：单队列/单线程/单视图、单 mip/layer、单颜色附件与固定 Store；无 Alias、多队列、计划缓存、深度采样或每 Pass GPU Timestamp。Feature 依赖显式声明，Full 写覆盖由作者保证；普通 Forward 目前消费已准备 DrawPacket，场景、Material 适配和光照属于 M4。

入口：[Graph API](../docs/m3-rendergraph.md)、[Feature/Create/Forward](../docs/forward-pipeline.md)、[构建与恢复](../manual/build.md#render-graph-与-feature)。产物位于构建树 graph、logs、Testing，不纳入源码。

### M3.4 简化 API 与普通 Forward 组合

已完成：名称查询、直接 Builder 与统一 Execute；Opaque/Transparent、只读深度和真实快照。接口与验收汇总见上述入口。

### M3.5 Feature 实例与流式管线统一

已完成：Feature 实例及名称连接、无序注册后 Create；依赖缺失/重名/环/阶段冲突与固定组合隔离均验证。

### M3.6 既有构建树依赖修复

已完成：定位 showIncludes 编码导致旧目标文件混用，自动检测前缀并完整重编译标准目录；依赖表与双后端启动已验证。本地 PDB C1902 用缓存中的 /Z7 处理，未写死共享预设；恢复方法见 [工具链](../manual/build.md#工具链)。

## M4 场景资源与基础 Renderer

状态：IN_PROGRESS。

目标是形成可展示画面和可保存内容，完整工程 UI 仍后置。M4.1–M4.2 打通资产 → 场景 → 材质 → Forward → 显示，M4.3 接入 PBR/HDR/IBL；阴影、透明和可重复演示工具继续保留后续计划。

### M4.1 最小项目与资源

状态：DONE，日期：2026 年 10 月 24 日。

结果：Manifest/挂载根、UUID/.meta 索引、稳定子资源与依赖映射、重命名；静态 glTF/GLB、WIC 图像、内容/Importer 版本化 CBOR 缓存。用户材质覆盖与导入产物分离。

验证：标准 windows-debug 的 content.contracts 通过，覆盖缓存命中/损坏重建、依赖失效、清缓存后移动资源及外部依赖、重名 UUID、稳定子资源、glTF/GLB 和用户材质保留；DX12/Vulkan 场景专项已通过首轮导入与显示。

入口：[数据契约与导入范围](../docs/m4-scene.md)、[启动与资源使用](../manual/build.md#m4-项目场景)。范围限静态三角形、float32 顶点和 Opaque，完整 glTF 外观、任意结构变化后的身份推断、实时重导入与流送均不在本阶段。

### M4.2 场景与 GPU 数据

状态：DONE，日期：2026 年 10 月 24 日。

结果：层级 Scene/保存加载、Camera、MeshRenderer、方向光组件与确定性主光选择；值快照、CPU 视锥剔除、历史变换、法线/镜像处理；Material 资源适配到既有 Forward/Graph。内置 Core/Lighting HLSL 和 Blinn–Phong，项目 Shader 可引用引擎参数与变换/光照辅助；Sandbox 项目加载及相机控制。

验证：RTX 4060 Laptop GPU、MSVC 19.51、DXC 1.8，标准 windows-debug 完整构建及 CTest 22/22 通过。场景专项双后端覆盖选光/旋转/强度、相机、实例增删、材质替换、镜像和保存重开；固定参考图像逐字节一致。另以 --project 加 --window-test 验证 DX12/Vulkan 各 54 帧、resize/minimize/restore，Validation 错误为 0。矩阵、层级、非均匀缩放、缓存与剔除的 CPU 合约同时通过。

入口：[场景、资源与 HLSL 契约](../docs/m4-scene.md)、[启动与控制](../manual/build.md#m4-项目场景)；参考项目 sample/m4/RenderLab.rlproject，图像与日志保留在构建树 scene、logs、Testing。

边界：本轮只验收 Debug 与该 NVIDIA 设备；Release、其他 GPU 和实际键盘手动操作未验收。单视图、静态 Opaque、point-clamp、LDR；模型/Shader 无实时重导入，常量按帧创建，历史指上次提取。PBR/HDR/IBL、阴影与高级透明仍在后续；整个 M4 保持 IN_PROGRESS。

补充修正（2026 年 10 月 24 日）：相机俯仰改为右乘局部旋转，绕自身右轴；转向保留父空间上轴。windows-debug Sandbox 重建通过；本次未运行测试或手动键盘验收。

### M4.3 Forward PBR

状态：DONE，日期：2026 年 10 月 26 日。

结果：普通 Shader/Material 驱动的 Lit/Unlit、glTF MR/Normal/Occlusion/Emission 参数与图像、独立 MaterialSurface/光栅 SampleGrad/BRDF 库；确定性预滤波 2D IBL 图集与 split-sum LUT；RGBA32F HDR、曝光、ACES fitted Tone Mapping 与单次 sRGB 显示编码。新增固定材质球参考项目，保留项目 Copper Shader 与原 Custom/RHI 示例。

验证：RTX 5060、MSVC 19.51.36257、DXC 1.9.0.5512，标准 windows-debug/windows-release 完整构建及最终 CTest 各 27/27 通过；DX12/Vulkan 严格 Validation 错误为 0。两配置各 16 张 PBR 参考 BMP 双后端逐字节一致；Unlit HDR/曝光/ACES/sRGB 数值参考误差不超过一个显示 code；RGBA32F 非整齐行间距上传读回逐位一致。完整 PBR 项目的双后端窗口回归纳入 CTest，覆盖 STA 保持、54 帧、10 次重建及 minimize/restore。原 content/scene/material/graph/RHI 回归均通过。

交互修复（同日）：首帧独立纹理 WIC 解码不再临时创建并卸载窗口线程的最后一个 COM apartment；Sandbox 在窗口/设备创建前初始化 STA，持有至宿主对象销毁，解码复用调用线程 apartment。用户在 VS Code 终端重跑原始 Debug DX12 完整 PBR 命令，确认蓝圈与交互问题消失。另修复材质 generation 未驻留 PSO 导致退休帧后重复创建的问题，保留独立生命周期回归。content.contracts 覆盖独立导入的临时 COM 清理，以及成功/失败解码均保持调用者 STA。

入口：[表面、IBL 与 HDR 契约](../docs/m4-forward-pbr.md)、[构建运行](../manual/build.md#m43-pbr-参考场景)；参考项目 sample/m4/Pbr.rlproject。构建树 pbr 下保留 BMP/Graph/日志/隔离资产，windows-debug/pbr/backend-comparison.json 保存后端比较；Testing/Temporary/LastTest.log 为完整测试结果。

限制：环境源为 sRGB lat-long 图加线性强度，无 .hdr/.exr 导入；低分辨率 IBL 与 split-sum 近似、导数切线框架（非 authored TANGENT/MikkTSpace 保证）、point-clamp/单 mip/layer/静态 Opaque。Vulkan 保留 Unlit 未消费 NORMAL 的提示，无验证错误。未验收其他 GPU、Vulkan/Release 的手动键盘与拖动或本子阶段的性能基准；阴影、透明与演示工具未因此标记完成，M4 保持 IN_PROGRESS。

### M4.4 阴影与透明

- [ ] 先实现一种确定的方向光阴影；级联在需要时作为子任务增加。
- [ ] 在 M4.2 场景接入基础上增加 Masked 与透明材质角色；按需加入独立 DepthPrepass/Sky Feature 和后处理阶段，定义快照位置。
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
