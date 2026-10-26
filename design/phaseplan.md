# RenderLab 阶段实施计划

本文件是实施状态的唯一记录入口。[architecture.md](architecture.md) 定义长期架构，[design-review.md](design-review.md) 解释本轮复查的取舍。阶段按可运行结果验收，只有设计或代码存在都不足以认定完成。

## 当前状态

- 当前实施阶段：M0–M4、R1、B1–B3 DONE；M5 IN_PROGRESS。
- 当前工作项：M5.4 独立光追反射与样例集成。
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
| M5 | Deferred 与光追基础 | M4 | IN_PROGRESS |
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

每个子阶段独立验收。先共享数据和有限材质，避免同时开发任意 Shader 命中调度与完整积分器。

### M5.1 Deferred（可选组合）

状态：DONE，日期：2026 年 10 月 26 日。

结果：RHI/Graph 支持最多八个 MRT 与 D32/D32S8 采样视图；SceneRenderer 提供三张 RGBA32F GBuffer、逆 ViewProjection 深度重建、PBR Deferred Lighting，透明仍走 Forward。Lit/Unlit 增加 GBuffer 角色；样例调节面板与 CLI 可选 Forward/Deferred，菜单选项表可扩展。接口见 [Deferred Pipeline](../docs/deferred-pipeline.md)，架构限制见 [Feature 与管线组合](../docs/forward-pipeline.md)。

验证：MSVC Debug/NMake 构建 `renderlab_sandbox` 成功。RTX 5060 上 CTest `gpu.{dx12,vulkan}.{deferred,tools_window}` 4/4；固定 PBR 640×360 下 Forward/Deferred 两后端均运行，validation errors=0。同后端 RGB 平均绝对差为 0.09/255，每帧有 742/230400 个像素不同。Shadow 场景 Deferred 窗口 DX12/Vulkan 各 54 帧、10 次重建，validation errors=0。DX12 清除值优化及 Vulkan 未消费顶点属性仍有非错误警告。

边界：三张全分辨率 RGBA32F 目标带宽较高；自定义不透明 Shader 必须提供 GBuffer 角色。Path Tracing 与 Forward+ 尚未实现，菜单只显示已实现组合；其他 GPU 未验证。

后续修复：M5.2 增加 Unlit GBuffer 标记，保持 Forward 输出并跳过 PBR 光照；补齐 MRT/采样视图修改后的 FakeRuntime 与共享 LitSurface 改名回归。双后端 surface 验证通过。

### M5.2 加速结构与 Ray Query

状态：DONE，日期：2026 年 10 月 26 日。

结果：双后端 BLAS/TLAS 原生尺寸查询、build/update、设备地址、scratch 保活及 Graph BuildInput/BuildWrite/Read 声明；静态 Mesh 复用 BLAS，独立 allInstances 列表驱动不可变 TLAS 版本重建。RayTracing 组合以独立 Ray Query Pass 生成 ShadowVisibility，接入 Deferred 直接光；面板和 CLI 可选 Forward/Deferred/Ray Tracing。新增 [RayTracing 参考项目](../sample/m4/RayTracing.rlproject)。

验证：MSVC Debug/NMake 构建 Sandbox、Surface、Graph、Core 目标。RTX 5060 上所选 CTest 14 项通过（core/graph；双后端 AS、Ray Query Scene、surface、deferred、rhi、tools_window），validation errors=0；覆盖原生 build/update、空 TLAS、提交后释放 scratch、对象增删/移动、相机复用、两帧在途版本、接收面阴影和 Unlit 对照。新增参考场景 DX12/Vulkan 窗口各 54 帧、10 次重建；640×360 离线输出均通过，validation errors=0。不支持能力诊断由 CPU 合约验证。

边界：仅不透明三角形，Masked 显式拒绝，透明继续 Forward 且不进入 AS；场景 Transform 更新采用新 TLAS 重建，动态顶点/蒙皮、压缩未实现。DX12 清除值优化和 Vulkan 未消费顶点属性仍有非错误警告。其他 GPU 未验证；WARP 软件路径探测未完成，不计通过。TraceRays 与反射集成继续 M5.3/M5.4，Path Tracing 仍按 M8 计划保留。

入口：[接口与生命周期](../docs/raytracing.md)、[运行与验收](../manual/build.md#ray-tracing-组合与-ray-query)。

### M5.3 Ray Tracing Pipeline

状态：DONE，日期：2026 年 10 月 26 日。

结果：双后端 RayGen/Miss/通用三角形 Hit Group、不可变 ShaderTable 与 TraceRays；Graph 增加类型化 RayTracing Pass、显式 ShaderTableRead 与匹配检查。通过 Instance/Mesh/Material 索引访问共享 PBR，有限纹理图集显式 LOD 0；表对齐遵守原生限制，代际变化重建 PSO/表，录制保活旧版本。

验证：MSVC Debug/NMake 构建 Sandbox 成功；RTX 5060 上 `gpu.{dx12,vulkan}.acceleration_structure` 2/2，实际执行 Ray Query 与 TraceRays；覆盖索引表面求值、纹理采样、hit/miss 和两代 PSO/ShaderTable 重建，validation errors=0。Vulkan 顶点用两个 float4 保持 32 字节 ABI，SBT 使用基础 KHR ray tracing 的 ShaderRead 访问，无额外布局/maintenance 扩展要求。

边界：仅一个通用三角形 Hit Group；无 any-hit、callable、程序几何、局部参数或自动文件监视。场景反射集成继续 M5.4，其他 GPU 未验证。入口：[接口、数据 ABI 与表重建规则](../docs/raytracing.md)。

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
